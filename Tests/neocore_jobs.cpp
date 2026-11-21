#include "NeoCore.h"
#include "NeoScript.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace
{
using namespace NeoCore;
std::mutex mutex;
std::condition_variable wake;
std::set<std::thread::id> inside;
bool together = false;
std::thread::id batchHead, batchTail;
bool batchOverlap = false;

void Check(bool ok, const std::string& message)
{
    if (!ok) throw std::runtime_error(message);
}

// 서로 다른 VM이 export 본문에 동시에 도달해야 풀린다. 실행 시간으로 병렬성을 추측하지 않는다.
void Log(const char* message)
{
    const bool head = std::strstr(message, "batch-head") != nullptr;
    const bool tail = std::strstr(message, "batch-tail") != nullptr;
    if (head || tail)
    {
        // 세 번째 입력까지 도달했으면 두 번째 결과는 이미 저장됐다. 첫 번째 입력은 여기서 기다린다.
        std::unique_lock<std::mutex> lock(mutex);
        (head ? batchHead : batchTail) = std::this_thread::get_id();
        if (batchHead != std::thread::id() && batchTail != std::thread::id() && batchHead != batchTail)
        {
            batchOverlap = true;
            wake.notify_all();
        }
        wake.wait_for(lock, std::chrono::seconds(5), [] { return batchOverlap; });
        return;
    }
    if (!std::strstr(message, "parallel-barrier")) return;
    std::unique_lock<std::mutex> lock(mutex);
    inside.insert(std::this_thread::get_id());
    if (inside.size() == 2) { together = true; wake.notify_all(); }
    wake.wait_for(lock, std::chrono::seconds(5), [] { return together; });
}

const char* worker = R"(
import arithmetic;
int count = 0;
float bias = 0.0;
export void Setup(array<float> a) { bias = a[0]; }
export void Work(array<float> a, array<float> f, array<int> i) {
    count += 1;
    print("parallel-barrier");
    f.resize(1); i.resize(1);
    f[0] = arithmetic.Add(a[0], bias); i[0] = count;
}
export void BatchItem(array<float> a, array<float> f, array<int> i) {
    int value = int(a[0]);
    if (value == 1) print("batch-head");
    if (value == 3) print("batch-tail");
    f.resize(1); i.resize(1);
    if (value == 4) { f[0] = a[9]; }
    f[0] = a[0] + bias; i[0] = value;
}
export void Small(array<float> a, array<float> f, array32<int> i) {
    i.resize(1); i[0] = -123;
}
)";

const char* mainSource = R"(
import jobs;
JobPool pool = jobs.open("worker", 2);
array<float> a = array<float>(1);
array<float> f = array<float>(0);
array<int> i = array<int>(0);
array32<int> small = array32<int>(1);
array<int> info = array<int>(3);
export void Start() {
    a[0] = 5.0; pool.broadcast("Setup", a);
    a[0] = 10.0; pool.post("Work", a);
    a[0] = 20.0; pool.post("Work", a);
}
export int Take() {
    int id = pool.take(f, i, info);
    if (id == 0) return 0;
    if (id < 0) {
        if (info[1] != 1 || info[2] != 1) return -10;
        f.resize(info[1]); i.resize(info[2]);
        if (pool.take(f, i, info) != -id) return -11;
        id = -id;
    }
    if (info[0] != 1 || i[0] != 1 || f[0] != 5.0 + id * 10) return -12;
    return id;
}
export void More() { pool.post32("Small", a); pool.post("Absent", a); }
export bool Ordered() {
    list<array<float>> inputs = [];
    if (pool.run("BatchItem", inputs).len() != 0) return false;
    for (int j in 1, 8) {
        array<float> item = array<float>(1); item[0] = float(j);
        inputs.append(item);
    }
    list<JobResult> output = pool.run("BatchItem", inputs);
    if (output.len() != inputs.len()) return false;
    for (int k in 0, output.len()) {
        JobResult r = output[k];
        if (k == 3) {
            if (r.ok || r.error == "" || r.floats.len() != 0 || r.ints.len() != 0) return false;
        } else {
            if (!r.ok || r.error != "" || r.floats[0] != inputs[k][0] + 5.0 || r.ints[0] != k + 1) return false;
        }
    }
    return true;
}
export int SmallResult() {
    int id = pool.take32(f, small, info);
    if (id != 0 && (info[0] != 1 || small[0] != -123)) return -1;
    return id;
}
export int FailedResult() {
    int id = pool.take(f, i, info);
    if (id != 0 && (info[0] != 0 || pool.error() == "")) return -1;
    return id;
}
export void InvalidBatch() { list<array<float>> inputs = [a, null]; pool.run("BatchItem", inputs); }
export bool QueuedTimeout() {
    list<array<float>> inputs = [a, a];
    list<JobResult> r = pool.run("BatchItem", inputs, 0.02);
    return r.len() == 2 && r[0].error == "job timed out" && r[1].error == "job timed out";
}
export int Pending() { return pool.pending(); }
export void Discard() { pool.clear(); }
export void Close() { pool.close(); pool.close(); }
)";

int Poll(Program& main, const char* function)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do
    {
        auto result = main.Call(function);
        Check(result.ok(), result.error.message);
        const int id = static_cast<int>(result.value.asInt());
        if (id != 0) return id;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < until);
    throw std::runtime_error(function);
}

// 대기 중인 실행 요청을 보관해 clear/reset 이후 실행기로 돌아오는 경우를 결정적으로 재현한다.
class DeferredExecutor final : public IJobExecutor
{
public:
    unsigned Concurrency() const override { return 2; }
    void Post(std::function<void()> task) override { tasks.push_back(std::move(task)); }
    void Flush()
    {
        for (auto& task : tasks) std::thread(std::move(task)).join();
        tasks.clear();
    }
    std::vector<std::function<void()>> tasks;
};

// 단일 코어 CI에서도 두 OS 스레드가 차례로 장벽에 들어갈 수 있다.
class TwoThreadExecutor final : public IJobExecutor
{
public:
    unsigned Concurrency() const override { return 2; }
    void Post(std::function<void()> task) override { threads.emplace_back(std::move(task)); }
    ~TwoThreadExecutor() { for (auto& thread : threads) thread.join(); }
private:
    std::vector<std::thread> threads;
};

std::set<std::thread::id> spinning;
int unexpected = 0;

void CloseLog(const char* message)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (std::strstr(message, "worker-spinning")) spinning.insert(std::this_thread::get_id());
    else ++unexpected;
    wake.notify_all();
}

void CloseError(const char*)
{
    std::lock_guard<std::mutex> lock(mutex);
    ++unexpected;
    wake.notify_all();
}

bool nativeEntered = false, releaseNative = false, queuedRan = false;
void TimeoutLog(const char* message)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (std::strstr(message, "timeout-native"))
    {
        nativeEntered = true;
        wake.wait_for(lock, std::chrono::seconds(8), [] { return releaseNative; });
    }
    else if (std::strstr(message, "unexpected queued item")) queuedRan = true;
}

void TestTimeout()
{
    const char* owner = R"(
import jobs;
JobPool p = jobs.open("worker", 1);
array<float> a = array<float>(1);
export bool Partial(float seconds) {
    list<array<float>> items = [];
    for (int n in 1, 5) {
        array<float> item = array<float>(1); item[0] = float(n); items.append(item);
    }
    p.post("Quick", a);
    list<JobResult> r = p.run("Timed", items, seconds);
    if (r.len() != 4 || !r[0].ok || r[0].ints[0] != 7) return false;
    if (r[1].ok || r[1].error == "" || r[1].error == "job timed out") return false;
    for (int j in 2, 4)
        if (r[j].ok || r[j].error != "job timed out" || r[j].floats.len() != 0 || r[j].ints.len() != 0) return false;
    return true;
}
export bool Quick(float seconds) {
    list<array<float>> items = [a];
    JobResult r = p.run("Quick", items, seconds)[0];
    if (seconds == 0) return !r.ok && r.error == "job timed out";
    return r.ok && r.ints[0] == 7;
}
export bool TakePosted() {
    array<int> info = array<int>(3); array<int> i = array<int>(1); array<float> f = array<float>(0);
    return p.take(f, i, info) == 1 && info[0] == 1 && i[0] == 7 && p.pending() == 0;
}
)";
    CompileOptions options;
    options.moduleLoader = [](const std::string& name, const std::string&, ModuleSource& out, Error&) {
        if (name != "worker") return false;
        out.sourceName = "worker.nc";
        out.source = R"(
int state = 7;
export void Quick(array<float> a, array<float> f, array<int> i) { i.resize(1); i[0] = state; }
export void Timed(array<float> a, array<float> f, array<int> i) {
    int n = int(a[0]);
    if (n == 1) { Quick(a, f, i); return; }
    if (n == 2) { i.resize(1); i[0] = int(a[9]); return; }
    if (n == 3) { state = 99; print("timeout-native"); while (true) { state += 1; } }
    print("unexpected queued item");
})";
        return true;
    };
    Error e;
    auto main = Program::Compile(owner, e, "timeout.nc", false, options);
    Check(bool(main), e.message);
    SetLogHandler(TimeoutLog, nullptr);
    const auto start = std::chrono::steady_clock::now();
    auto partial = main->Call("Partial", { Scalar::Float(0.5) });
    const auto waited = std::chrono::steady_clock::now() - start;
    {
        std::lock_guard<std::mutex> lock(mutex);
        releaseNative = true;
        wake.notify_all();
        Check(nativeEntered, "batch did not enter the blocking native call");
    }
    Check(partial.ok() && partial.value.asBool(), "wrong partial batch results: " + partial.error.message);
    Check(waited >= std::chrono::milliseconds(490) && waited < std::chrono::seconds(7),
        "timeout did not bound the wait independently of the native call");
    // 중단된 VM의 전역을 버리고 새 VM으로 진행하며 앞선 post 결과도 남아 있어야 한다.
    for (double seconds : {0.0, 10.0, -1.0, (std::numeric_limits<double>::max)()})
    {
        auto quick = main->Call("Quick", { Scalar::Float(seconds) });
        Check(quick.ok() && quick.value.asBool(), "pool unavailable after timeout: " + quick.error.message);
    }
    auto posted = main->Call("TakePosted");
    Check(posted.ok() && posted.value.asBool(), "timeout affected independent post");
    for (double seconds : {-2.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        Check(!main->Call("Quick", { Scalar::Float(seconds) }).ok(), "invalid timeout accepted");
    main.reset();
    Check(!queuedRan, "expired queued item ran");
}

enum class CloseVia { Script, Reset, Destroy };

void TestClose(const std::string& workerEntry, const char* setup, CloseVia via)
{
    // 실제 워커 둘이 진입한 뒤 종료한다. 무한 루프를 놓친 회귀는 CTest 제한 시간에서 실패한다.
    const std::string workerSource = R"(
import coroutine;
void Spin() {
    list<string> values = ["owned"];
    array<int> buffer = array<int>(4);
    print("worker-spinning");
    while (true) { buffer[0] += values.len(); }
}
export void Queued(array<float> a, array<float> f, array<int> i) { print("unexpected job"); }
)" + workerEntry;
    const std::string owner = std::string(R"(
import jobs;
JobPool p = jobs.open("worker", 2);
array<float> a = array<float>(1);
export void Start() {
)") + setup + R"(
    p.post("Work", a); p.post("Work", a);
    for (int j in 0, 9) p.post("Queued", a);
}
export int Close() { p.close(); p.close(); return p.pending(); }
)";
    Error error;
    CompileOptions options;
    if (std::thread::hardware_concurrency() < 2) options.jobExecutor = std::make_shared<TwoThreadExecutor>();
    options.moduleLoader = [&](const std::string& name, const std::string&, ModuleSource& out, Error&) {
        if (name != "worker") return false;
        out.source = workerSource; out.sourceName = "worker.nc"; return true;
    };
    NeoScript::AllocStats before, after;
    NeoScript::GetAllocStats(before);
    auto main = Program::Compile(owner, error, "close.nc", false, options);
    Check(bool(main), error.message);
    {
        std::lock_guard<std::mutex> lock(mutex);
        spinning.clear();
        unexpected = 0;
    }
    auto started = main->Call("Start");
    Check(started.ok(), started.error.message);
    {
        std::unique_lock<std::mutex> lock(mutex);
        Check(wake.wait_for(lock, std::chrono::seconds(5), [] { return spinning.size() == 2 || unexpected; })
            && spinning.size() == 2, "two workers did not enter the expected script");
    }
    if (via == CloseVia::Script)
    {
        auto result = main->Call("Close");
        Check(result.ok() && result.value.asInt() == 0, "close left jobs or failed");
    }
    else if (via == CloseVia::Reset)
        Check(main->Reset(error), error.message);
    main.reset();
    {
        std::lock_guard<std::mutex> lock(mutex);
        Check(!unexpected, "a queued job ran, a cancelled callback resumed, or cancellation logged a script error");
    }
    NeoScript::GetAllocStats(after);
    Check(after.lists == before.lists && after.arrays == before.arrays && after.strings == before.strings
        && after.closures == before.closures && after.coroutines == before.coroutines && after.bufferBytes == before.bufferBytes,
        "worker allocations remained after close");
}

void Test()
{
    Error error;
    CompileOptions options;
    if (std::thread::hardware_concurrency() < 2) options.jobExecutor = std::make_shared<TwoThreadExecutor>();
    const auto caller = std::this_thread::get_id();
    int entries = 0, dependencies = 0;
    options.moduleLoader = [&](const std::string& name, const std::string& requester, ModuleSource& out, Error&) {
        Check(std::this_thread::get_id() == caller, "worker invoked the source loader");
        out.sourceName = name + ".nc";
        if (name == "worker" && requester == "main.nc") { ++entries; out.source = worker; return true; }
        if (name == "arithmetic" && requester == "worker.nc")
        { ++dependencies; out.source = "public float Add(float x,float y){return x+y;}"; return true; }
        return false;
    };
    std::vector<std::uint8_t> image;
    Check(Program::CompileImage(mainSource, image, error, "main.nc", false, options), error.message);
    auto main = Program::LoadImage(image, error, options);
    Check(bool(main), error.message);
    SetLogHandler(Log, nullptr);
    auto started = main->Call("Start");
    Check(started.ok(), started.error.message);
    std::set<int> ids;
    ids.insert(Poll(*main, "Take"));
    ids.insert(Poll(*main, "Take"));
    Check(ids == std::set<int>({1, 2}), "wrong job id, setup, isolation or resized result");
    Check(together && !inside.count(caller), "worker scripts did not execute concurrently");
    auto more = main->Call("More");
    Check(more.ok(), more.error.message);
    auto ordered = main->Call("Ordered");
    Check(ordered.ok(), ordered.error.message);
    Check(ordered.value.asBool(), "ordered batch values or error isolation");
    Check(batchOverlap && caller != batchHead && caller != batchTail, "batch did not execute concurrently");
    Check(main->Call("Pending").value.asInt() == 2, "batch consumed an asynchronous result");
    Check(Poll(*main, "SmallResult") == 3, "compact output");
    Check(Poll(*main, "FailedResult") == 4, "missing export");
    Check(main->Call("Pending").value.asInt() == 0, "unconsumed results");
    Check(!main->Call("InvalidBatch").ok(), "accepted null batch item");
    Check(main->Call("Pending").value.asInt() == 0, "partially submitted invalid batch");
    Check(main->Call("Close").ok(), "idempotent close");
    Check(!main->Call("Ordered").ok(), "closed pool ran an empty batch");
    Check(!main->Call("More").ok(), "closed pool accepted work");
    Check(entries == 1 && dependencies == 1, "module loaded more than once");
    auto deferred = std::make_shared<DeferredExecutor>();
    options.jobExecutor = deferred;
    main = Program::LoadImage(image, error, options);
    Check(main && main->Call("Start").ok() && deferred->tasks.size() == 2, "injected executor");
    auto queued = main->Call("QueuedTimeout");
    Check(queued.ok() && queued.value.asBool() && main->Call("Pending").value.asInt() == 2,
        "timeout did not remove just its own queued items");
    Check(main->Call("Discard").ok(), "clear");
    deferred->Flush();
    Check(main->Call("Pending").value.asInt() == 0, "cleared jobs produced results");
    Check(main->Call("More").ok() && main->Reset(error), "reset with pending work");
    deferred->Flush();
    Check(entries == 2 && dependencies == 2, "unexpected load on clear or reset");
    SetLogHandler(CloseLog, CloseError);
    const char* spin = "export void Work(array<float> a, array<float> f, array<int> i) { Spin(); }";
    TestClose(spin, "", CloseVia::Script);
    const std::string unreachable = R"(export void Work(array<float> a, array<float> f, array<int> i) { print("unexpected export"); })";
    TestClose(unreachable + " Spin();", "", CloseVia::Destroy);
    TestClose(unreachable + " export void Setup(array<float> a) { Spin(); }",
        "p.broadcast(\"Setup\", a);", CloseVia::Reset);
    TestClose(R"(
export void Work(array<float> a, array<float> f, array<int> i) {
    list<int> values = [2, 1]; int bias = 1;
    values.sort(fn(int x, int y)->bool { Spin(); return x + bias < y; });
    print("unexpected return from sort");
})", "", CloseVia::Script);
    TestClose(R"(
export void Work(array<float> a, array<float> f, array<int> i) {
    coroutine<fn()->void> task = coroutine.create(Spin);
    coroutine.resume(task);
    print("unexpected coroutine return");
})", "", CloseVia::Script);
    TestTimeout();
    SetLogHandler(nullptr, nullptr);
}
}
int main()
{
    try { Test(); }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
    std::puts("parallel jobs passed");
    return 0;
}
