#include "NeoCoreJobs.h"
#include "NeoCore.h"
#include <algorithm>
#include <condition_variable>
#include <climits>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace NeoCore
{
namespace
{
using NeoCoreBackend::JobResult;

// 기본 실행기는 모든 풀에서 공유한다. 실제 요청이 늘 때만 스레드를 만든다.
class ThreadExecutor final : public IJobExecutor
{
public:
    ThreadExecutor() : limit_(std::max(1u, std::thread::hardware_concurrency())) {}
    ~ThreadExecutor()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        for (auto& thread : threads_) thread.join();
    }
    unsigned Concurrency() const override { return limit_; }
    void Post(std::function<void()> task) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(task));
        try
        {
            if (threads_.size() < limit_ && queue_.size() + busy_ > threads_.size())
                threads_.emplace_back(&ThreadExecutor::Run, this);
        }
        catch (...) { queue_.pop_back(); throw; }
        wake_.notify_one();
    }
private:
    void Run()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;)
        {
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) return;
            auto task = std::move(queue_.front());
            queue_.pop_front();
            ++busy_;
            lock.unlock();
            task();
            lock.lock();
            --busy_;
        }
    }
    const unsigned limit_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> threads_;
    size_t busy_ = 0;
    bool stopping_ = false;
};

std::shared_ptr<IJobExecutor> DefaultExecutor()
{
    static auto executor = std::make_shared<ThreadExecutor>();
    return executor;
}

std::string Describe(const Error& error)
{
    if (error.sourceName.empty()) return error.message;
    return error.sourceName + ":" + std::to_string(error.line) + ": " + error.message;
}

struct InvocationData
{
    std::string name;
    std::vector<double> input;
};
struct Initializer : InvocationData
{
    std::uint64_t serial;
};
using Setup = std::vector<std::shared_ptr<const Initializer>>;
// 배치 결과는 입력 인덱스에 직접 넣고 마지막 항목이 완료되면 호출자를 깨운다.
struct Batch
{
    explicit Batch(size_t count) : results(count), remaining(count) {}
    void Finish(size_t index, JobResult result)
    {
        results[index] = std::move(result);
        if (--remaining == 0) ready.notify_one();
    }
    std::vector<JobResult> results;
    size_t remaining;
    std::condition_variable ready;
};
struct Task : InvocationData
{
    std::int64_t id = 0;
    std::uint64_t epoch = 0;
    bool compact = false;
    std::shared_ptr<const Setup> setup;
    std::shared_ptr<Batch> batch;
    size_t index = 0;
};
struct Worker
{
    std::unique_ptr<Program> program;
    std::uint64_t serial = 0;
};

void Complete(const Result& result)
{
    if (!result.ok()) throw std::runtime_error(Describe(result.error));
    if (result.suspended()) throw std::runtime_error("worker export suspended");
}

Scalar MakeArray(Program& program, const char* type)
{
    Error error;
    auto array = program.CreateValue(type, error);
    if (!error.ok()) throw std::runtime_error(Describe(error));
    return array;
}

template<class T> void ReadOutput(const Scalar& array, std::vector<T>& out)
{
    out.resize(array.Count());
    if (!out.empty()) array.ReadArray(out.data(), out.size());
}

void Invoke(Program& program, const InvocationData& call, JobResult* result, bool compact)
{
    std::vector<Scalar> args;
    args.push_back(MakeArray(program, "array<float>"));
    if (!call.input.empty())
    {
        args.front().Resize(call.input.size());
        args.front().WriteArray(call.input.data(), call.input.size());
    }
    if (result)
    {
        args.push_back(MakeArray(program, "array<float>"));
        args.push_back(MakeArray(program, compact ? "array32<int>" : "array<int>"));
    }
    Complete(program.Call(call.name, args));
    if (result)
    {
        ReadOutput(args[1], result->floats);
        if (compact) ReadOutput(args[2], result->compactInts);
        else ReadOutput(args[2], result->ints);
    }
}

class Pool final : public NeoCoreBackend::JobPool, public std::enable_shared_from_this<Pool>
{
public:
    Pool(std::vector<std::uint8_t> image, std::shared_ptr<IJobExecutor> executor, int limit)
        : image_(std::move(image)), executor_(std::move(executor)), limit_(limit), setup_(new Setup())
    { idle_.reserve(limit); }

    int Workers() const override { return limit_; }
    void Broadcast(std::string name, std::vector<double> args) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        RequireOpen();
        auto item = std::make_shared<Initializer>();
        item->serial = ++serial_;
        item->name = std::move(name);
        item->input = std::move(args);
        auto next = std::make_shared<Setup>();
        for (const auto& old : *setup_)
            if (old->name != item->name) next->push_back(old);
        next->push_back(item);
        setup_ = std::move(next);
    }

    std::int64_t Post(std::string name, std::vector<double> args, bool compact) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        RequireOpen();
        if (id_ == (std::numeric_limits<std::int64_t>::max)()) throw std::overflow_error("job id exhausted");
        Task task;
        task.name = std::move(name);
        task.input = std::move(args);
        task.id = ++id_;
        task.epoch = epoch_;
        task.compact = compact;
        task.setup = setup_;
        queue_.push_back(std::move(task));
        try { StartWorkers(); }
        catch (...) { queue_.pop_back(); throw; }
        return id_;
    }

    std::vector<JobResult> Run(const std::string& name, std::vector<std::vector<double>> inputs) override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        RequireOpen();
        if (inputs.empty()) return {};
        auto batch = std::make_shared<Batch>(inputs.size());
        const size_t previous = queue_.size();
        try
        {
            for (size_t i = 0; i < inputs.size(); ++i)
            {
                Task task;
                task.name = name;
                task.input = std::move(inputs[i]);
                task.epoch = epoch_;
                task.setup = setup_;
                task.batch = batch;
                task.index = i;
                queue_.push_back(std::move(task));
            }
            StartWorkers();
        }
        catch (...)
        {
            // 잠금을 보유하므로 이번 호출이 추가한 일은 아직 다른 스레드가 가져갈 수 없다.
            while (queue_.size() > previous) queue_.pop_back();
            throw;
        }
        batch->ready.wait(lock, [&batch] { return batch->remaining == 0; });
        return std::move(batch->results);
    }

    const JobResult* Peek(bool compact) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& queue = results_[compact];
        return queue.empty() ? nullptr : &queue.front();
    }
    void Consume(bool compact) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& queue = results_[compact];
        lastError_ = std::move(queue.front().error);
        queue.pop_front();
    }
    const std::string& ErrorText() const override { return lastError_; }
    std::int64_t Pending() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size() + active_ + results_[0].size() + results_[1].size();
    }
    void Clear() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Discard();
    }
    void Close() override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        closed_ = true;
        Discard();
        finished_.wait(lock, [this] { return active_ == 0; });
        idle_.clear();
        setup_.reset();
    }

private:
    void StartWorkers()
    {
        std::weak_ptr<Pool> weak = shared_from_this();
        while (scheduled_ < limit_ && static_cast<size_t>(scheduled_) < active_ + queue_.size())
        {
            executor_->Post([weak] { if (auto pool = weak.lock()) pool->Drain(); });
            ++scheduled_;
        }
    }

    void RequireOpen() const
    {
        if (closed_) throw std::runtime_error("job pool is closed");
    }
    void Discard()
    {
        ++epoch_;
        for (const Task& task : queue_)
        {
            if (!task.batch) continue;
            JobResult canceled;
            canceled.error = "job cleared before execution";
            task.batch->Finish(task.index, std::move(canceled));
        }
        queue_.clear();
        results_[0].clear();
        results_[1].clear();
        lastError_.clear();
    }

    void Execute(Worker& worker, const Task& task, JobResult& result) const
    {
        if (!worker.program)
        {
            Error error;
            worker.program = Program::LoadImage(image_, error);
            if (!worker.program) throw std::runtime_error(Describe(error));
            Complete(worker.program->Run());
        }
        // 제출 시점의 준비 상태를 같은 VM에 한 번씩 적용한다.
        for (const auto& init : *task.setup)
        {
            if (init->serial <= worker.serial) continue;
            Invoke(*worker.program, *init, nullptr, false);
            worker.serial = init->serial;
        }
        Invoke(*worker.program, task, &result, task.compact);
    }

    void Drain()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        Worker worker;
        if (!idle_.empty()) { worker = std::move(idle_.back()); idle_.pop_back(); }
        while (!queue_.empty())
        {
            Task task = std::move(queue_.front());
            queue_.pop_front();
            ++active_;
            lock.unlock();
            JobResult result;
            result.id = task.id;
            try { Execute(worker, task, result); }
            catch (const std::exception& error)
            {
                result = JobResult();
                result.id = task.id;
                result.error = task.name + ": " + error.what();
                worker = Worker();
            }
            lock.lock();
            if (task.batch) task.batch->Finish(task.index, std::move(result));
            else if (epoch_ == task.epoch) results_[task.compact].push_back(std::move(result));
            --active_;
        }
        if (!closed_ && worker.program) idle_.push_back(std::move(worker));
        --scheduled_;
        finished_.notify_all();
    }
    const std::vector<std::uint8_t> image_;
    const std::shared_ptr<IJobExecutor> executor_;
    const int limit_;
    mutable std::mutex mutex_;
    std::condition_variable finished_;
    std::deque<Task> queue_;
    std::deque<JobResult> results_[2];
    std::vector<Worker> idle_;
    std::shared_ptr<const Setup> setup_;
    std::string lastError_;
    std::int64_t id_ = 0;
    std::uint64_t serial_ = 0, epoch_ = 0;
    size_t active_ = 0;
    int scheduled_ = 0;
    bool closed_ = false;
};
}

std::shared_ptr<NeoCoreBackend::JobPool> InstanceJobs::Open(const std::string& module, std::int64_t workers)
{
    if (workers < 0) throw std::runtime_error("jobs.open worker count must be nonnegative");
    if (module.empty() || module.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos ||
        (module.front() >= '0' && module.front() <= '9'))
        throw std::runtime_error("jobs.open expects an import module name");
    auto executor = options_.jobExecutor ? options_.jobExecutor : DefaultExecutor();
    const unsigned available = executor->Concurrency();
    if (!available) throw std::runtime_error("job executor has no workers");
    const auto limit = std::min<std::int64_t>(workers ? workers : available, std::min<unsigned>(available, INT_MAX));
    Error error;
    ModuleSource entry;
    std::vector<std::uint8_t> image;
    if (!options_.moduleLoader(module, requester_, entry, error))
        throw std::runtime_error("cannot load worker module " + module + ": " + Describe(error));
    if (!Program::CompileImage(entry.source, image, error, entry.sourceName.empty() ? module : entry.sourceName, true, options_))
        throw std::runtime_error(Describe(error));
    auto pool = std::make_shared<Pool>(std::move(image), std::move(executor), static_cast<int>(limit));
    pools_.erase(std::remove_if(pools_.begin(), pools_.end(),
        [](const std::weak_ptr<NeoCoreBackend::JobPool>& p) { return p.expired(); }), pools_.end());
    pools_.push_back(pool);
    return pool;
}

void InstanceJobs::Close()
{
    for (const auto& weak : pools_)
        if (auto pool = weak.lock()) pool->Close();
    pools_.clear();
}
}
