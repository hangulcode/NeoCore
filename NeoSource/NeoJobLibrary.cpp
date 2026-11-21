#include "NeoJobLibrary.h"
#include "NeoVMWorker.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace NeoScript
{
namespace JobLibrary
{
namespace
{
using NeoCoreBackend::JobPool;
const char poolType = 0;

JobPool& Pool(ValueRef receiver)
{
    if (receiver.GetType() == VAR_NONE)
        throw std::runtime_error("cannot access a null JobPool");
    assert(receiver.GetType() == VAR_FP_NATIVE && receiver->_fpNative->_identityType == &poolType);
    return *static_cast<JobPool*>(receiver->_fpNative->_pUserData);
}

ArrayInfo& Array(ValueRef arg)
{
    if (arg.GetType() == VAR_NONE) throw std::runtime_error("job array must not be null");
    return *arg->_array;
}

std::vector<double> CopyInput(ValueRef arg)
{
    ArrayInfo& array = Array(arg);
    if (!array._count) return {};
    return std::vector<double>(array.FloatData(), array.FloatData() + array._count);
}

const std::string& Name(ValueRef arg)
{
    if (arg.GetType() == VAR_NONE || arg->_str->_str.empty())
        throw std::runtime_error("job name must not be empty");
    return arg->_str->_str;
}
bool NoMethod(INeoVMWorker*, void*, const VMString*, short) { return false; }

// 예외는 스레드 경계나 VM 디스패치를 넘기지 않고 호출한 스크립트 줄의 오류로 변환한다.
template<class Action> bool Checked(CNeoVMWorker* w, Action action)
{
    try { action(); return true; }
    catch (const std::exception& e) { w->SetError(e.what()); return false; }
}
enum class Submit { Init, Normal, Compact };

bool SubmitJob(CNeoVMWorker* w, ValueRef receiver, Submit mode)
{
    return Checked(w, [&] {
        auto& pool = Pool(receiver);
        const auto& name = Name(w->GetStack(1));
        auto args = CopyInput(w->GetStack(2));
        if (mode == Submit::Init)
        {
            pool.Broadcast(name, std::move(args));
            w->ReturnValue();
        }
        else w->ReturnValue(pool.Post(name, std::move(args), mode == Submit::Compact));
    });
}

ArrayInfo* OutputArray(CNeoVMWorker* w, ValueRef slot, NeoArrayElementType element, size_t count)
{
    BoundaryValue zero(element == NeoArrayElementType::Float ? VAR_FLOAT : VAR_INT);
    auto* array = w->GetVM()->ArrayAlloc(element, static_cast<int>(count), zero);
    if (!array) throw std::runtime_error("could not allocate job output array");
    w->Var_SetArray(slot, array);
    return array;
}

void ReturnBatch(CNeoVMWorker* w, const std::vector<NeoCoreBackend::JobResult>& results)
{
    auto output = w->GetReturnVar();
    w->ResetVarType(output, VAR_LIST, static_cast<int>(results.size()));
    ListInfo& list = *output->_lst;
    if (!results.empty()) list.MarkContainerChild();
    for (size_t i = 0; i < results.size(); ++i)
    {
        // 구조체는 선언 순서대로 필드를 담는 리스트다. 부모가 즉시 소유하므로 부분 생성도 정리된다.
        auto record = list.GetValue(static_cast<int>(i));
        w->ResetVarType(record, VAR_LIST, 4);
        auto& fields = *record->_lst;
        const auto& result = results[i];
        w->Var_SetBool(fields.GetValue(0), result.error.empty());
        w->Var_SetStringA(fields.GetValue(1), result.error);
        auto* floats = OutputArray(w, fields.GetValue(2), NeoArrayElementType::Float, result.floats.size());
        auto* ints = OutputArray(w, fields.GetValue(3), NeoArrayElementType::Int, result.ints.size());
        if (!result.floats.empty()) std::copy(result.floats.begin(), result.floats.end(), floats->FloatData());
        if (!result.ints.empty()) std::copy(result.ints.begin(), result.ints.end(), ints->IntData());
    }
}

bool TakeJob(CNeoVMWorker* w, ValueRef receiver, bool compact)
{
    return Checked(w, [&] {
        auto& pool = Pool(receiver);
        auto& floats = Array(w->GetStack(1));
        auto& ints = Array(w->GetStack(2));
        auto& info = Array(w->GetStack(3));
        if (info._count < 3 || &ints == &info) throw std::runtime_error("job info needs a separate array of at least three integers");
        const auto* result = pool.Peek(compact);
        if (!result) { w->ReturnValue(NS_INT(0)); return; }
        const auto count = compact ? result->compactInts.size() : result->ints.size();
        info.IntData()[0] = result->error.empty() ? 1 : 0;
        info.IntData()[1] = static_cast<NS_INT>(result->floats.size());
        info.IntData()[2] = static_cast<NS_INT>(count);
        if (static_cast<size_t>(floats._count) < result->floats.size() || static_cast<size_t>(ints._count) < count)
        { w->ReturnValue(-result->id); return; }
        if (!result->floats.empty()) std::copy(result->floats.begin(), result->floats.end(), floats.FloatData());
        if (count && compact) std::copy(result->compactInts.begin(), result->compactInts.end(), ints.Int32Data());
        if (count && !compact) std::copy(result->ints.begin(), result->ints.end(), ints.IntData());
        const NS_INT id = result->id;
        pool.Consume(compact);
        w->ReturnValue(id);
    });
}
}

bool Open(CNeoVMWorker* w, ValueRef, short)
{
    return Checked(w, [&] {
        if (!w->jobFactory) throw std::runtime_error("jobs.open requires a module loader on the owning program");
        auto pool = w->jobFactory->Open(Name(w->GetStack(1)), w->GetStack(2)->_int);
        auto result = w->GetReturnVar();
        w->ResetVarType(result, VAR_FP_NATIVE, 0);
        if (!NeoVMSystem::RegisterTableCallBack(result, pool.get(), NoMethod, nullptr, &poolType, pool.get(), pool))
            throw std::runtime_error("cannot create JobPool handle");
    });
}
bool Workers(CNeoVMWorker* w, ValueRef r, short) { return Checked(w, [&] { w->ReturnValue(NS_INT(Pool(r).Workers())); }); }
bool Broadcast(CNeoVMWorker* w, ValueRef r, short) { return SubmitJob(w, r, Submit::Init); }
// 호출 전체가 한 마감 시각을 사용한다. 오래된 이미지의 두 인자 호출도 무한 대기로 처리한다.
static std::chrono::steady_clock::time_point BatchDeadline(double seconds)
{
    using Clock = std::chrono::steady_clock;
    if (seconds == -1.0) return Clock::time_point::max();
    if (!std::isfinite(seconds) || seconds < 0.0)
        throw std::runtime_error("JobPool.run timeoutSeconds must be -1 or a finite nonnegative number");
    const auto now = Clock::now();
    const auto span = std::chrono::duration<double>(seconds);
    if (span >= std::chrono::duration<double>(Clock::time_point::max() - now))
        return Clock::time_point::max();
    return now + std::chrono::duration_cast<Clock::duration>(span);
}

bool Run(CNeoVMWorker* w, ValueRef r, short argc)
{
    return Checked(w, [&] {
        auto& pool = Pool(r);
        const auto& name = Name(w->GetStack(1));
        const auto deadline = BatchDeadline(argc == 2 ? -1.0 : w->GetStack(3)->_float);
        auto input = w->GetStack(2);
        if (input.GetType() == VAR_NONE) throw std::runtime_error("job input list must not be null");
        std::vector<std::vector<double>> batch;
        batch.reserve(input->_lst->GetCount());
        for (int i = 0; i < input->_lst->GetCount(); ++i)
            batch.push_back(CopyInput(input->_lst->GetValue(i)));
        ReturnBatch(w, pool.Run(name, std::move(batch), deadline));
    });
}
bool Post(CNeoVMWorker* w, ValueRef r, short) { return SubmitJob(w, r, Submit::Normal); }
bool Post32(CNeoVMWorker* w, ValueRef r, short) { return SubmitJob(w, r, Submit::Compact); }
bool Take(CNeoVMWorker* w, ValueRef r, short) { return TakeJob(w, r, false); }
bool Take32(CNeoVMWorker* w, ValueRef r, short) { return TakeJob(w, r, true); }
bool ErrorText(CNeoVMWorker* w, ValueRef r, short) { return Checked(w, [&] { w->ReturnValue(Pool(r).ErrorText().c_str()); }); }
bool Pending(CNeoVMWorker* w, ValueRef r, short) { return Checked(w, [&] { w->ReturnValue(Pool(r).Pending()); }); }
bool Clear(CNeoVMWorker* w, ValueRef r, short) { return Checked(w, [&] { Pool(r).Clear(); w->ReturnValue(); }); }
bool Close(CNeoVMWorker* w, ValueRef r, short) { return Checked(w, [&] { Pool(r).Close(); w->ReturnValue(); }); }
}
}
