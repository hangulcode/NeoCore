#pragma once

#include "NeoVMProgram.h"
#include <algorithm>
#include <set>

namespace NeoScript
{

struct FusionIO
{
    unsigned reads = 0, writes = 0;
    VAR_TYPE result = VAR_NONE;
    bool supported = false, jump = false;
};

// 타입별 연산의 결과와 슬롯 사용을 한 번만 분석한다. 값의 태그를 보고 타입을 추론하지 않는다.
inline FusionIO FusionOperation(const SVMOperation& instruction)
{
    const OperationCode op = ArithmeticMeaning(instruction.Code());
    FusionIO info;
    const TypedOpInfo* typed = GetTypedOpInfo(op);
    if (typed)
    {
        info.supported = true; info.jump = typed->jump;
        info.reads = IsArithmeticUpdate(instruction) ? 3 : typed->slots & ~typed->writes; info.writes = typed->writes;
        info.result = typed->jump ? VAR_NONE : typed->comparison ? VAR_BOOL :
            (PrimaryOperation(op) == NOP_F64_ARITH || op == OP_I64_TO_F64) ? VAR_FLOAT : VAR_INT;
        return info;
    }
    if (op == NOP_LOG_NOT)
    { info.supported = true; info.reads = 2; info.writes = 1; info.result = VAR_BOOL; return info; }
    switch (op)
    {
#define NEOCORE_FUSION_ARRAY(TYPE, TAG) \
    case NOP_ARRAY_##TYPE##_GET: info.result = TAG; info.reads = 6; info.writes = 1; info.supported = true; break;
        NEOCORE_FUSION_ARRAY(I64, VAR_INT)
        NEOCORE_FUSION_ARRAY(F64, VAR_FLOAT)
        NEOCORE_FUSION_ARRAY(BOOL, VAR_BOOL)
        NEOCORE_FUSION_ARRAY(I32, VAR_INT)
        NEOCORE_FUSION_ARRAY(F32, VAR_FLOAT)
#undef NEOCORE_FUSION_ARRAY
#define NEOCORE_FUSION_ACCESS(OWNER, TYPE, TAG) \
    case NOP_##OWNER##_##TYPE##_LOAD: info.result = TAG; info.reads = 6; info.writes = 1; info.supported = true; break; \
    case NOP_##OWNER##_##TYPE##_STORE: info.reads = 7; info.supported = true; break;
#define NEOCORE_FUSION_OWNER(OWNER) \
    NEOCORE_FUSION_ACCESS(OWNER, I64, VAR_INT) \
    NEOCORE_FUSION_ACCESS(OWNER, F64, VAR_FLOAT) \
    NEOCORE_FUSION_ACCESS(OWNER, BOOL, VAR_BOOL)
        NEOCORE_FUSION_OWNER(LIST)
        NEOCORE_FUSION_OWNER(MAP_INT)
        NEOCORE_FUSION_OWNER(MAP_STR)
#undef NEOCORE_FUSION_OWNER
#undef NEOCORE_FUSION_ACCESS
    case NOP_ARRAY_I64_STORE:
    case NOP_ARRAY_F64_STORE:
    case NOP_ARRAY_BOOL_STORE:
    case NOP_ARRAY_I32_STORE:
    case NOP_ARRAY_F32_STORE: info.reads = 7; info.supported = true; break;
    default: break;
    }
    return info;
}

inline short FusionOperand(const SVMOperation& op, unsigned n)
{ return n == 0 ? op.n1 : n == 1 ? op.n2 : op.n3; }
inline bool FusionLocal(const SVMOperation& op, unsigned n)
{ return (op.argFlag & (NEOS_ARG_N1_LOCAL >> n)) != 0; }

inline bool FusionRelativeJump(OperationCode op)
{
    const TypedOpInfo* typed = GetTypedOpInfo(op);
    return (typed && typed->jump) || (PrimaryOperation(op) >= NOP_JMP && PrimaryOperation(op) <= NOP_JMP_FOREACH) ||
        (op >= NOP_JMP_RANGE_INSIDE && op <= NOP_JMP_RANGE_OUTSIDE_L);
}

// 확정된 읽기·쓰기 정보가 없는 경계는 임시값이 사용된다고 간주한다.
// 호출·클로저·호스트 접근 너머의 수명을 추측해서 제거하지 않는다.
inline bool FusionMayReadAfter(const std::vector<SVMOperation>& code, int begin, int end,
    int entry, short slot, const std::vector<std::vector<int>>& switchTargets)
{
    std::vector<int> pending(1, entry);
    std::set<int> visited;
    while (!pending.empty())
    {
        const int at = pending.back(); pending.pop_back();
        if (at < begin || at >= end) continue;
        if (!visited.insert(at).second) continue;
        if (visited.size() > 4096) return true;
        const SVMOperation& op = code[at];
        FusionIO io = FusionOperation(op);
        unsigned reads = io.reads, writes = io.writes;
        if (!io.supported)
        {
            switch (op.op == NOP_SLOT_COPY ? static_cast<OperationCode>(NOP_MOV) : op.Code())
            {
            case NOP_MOV: reads = 2; writes = 1; break;
            case NOP_MOVI: case NOP_MOVF: case NOP_LOADK: case NOP_VAR_CLEAR: writes = 1; break;
            case NOP_JMP: break;
            case NOP_JMP_FALSE: case NOP_JMP_TRUE: reads = 2; break;
            case OP_JMP_EQUAL2: case OP_JMP_LESS: case OP_JMP_LESS_EQ: case OP_JMP_NEQUAL:
            case OP_JMP_GREAT: case OP_JMP_GREAT_EQ: reads = 6; break;
            case NOP_JMP_FOR:
                if (slot >= op.n2 && slot < op.n2 + 4) return true;
                break;
            case NOP_SWITCH: reads = 2; break;
            case NOP_RETURN: reads = op.argFlag & NEOS_OP_CALL_NORESULT ? 0 : 1; break;
            case NOP_CHANGE_INT: reads = writes = 1; break;
            case NOP_NONE: case NOP_ERROR: break;
            default: return true;
            }
        }
        bool killed = false;
        for (unsigned n = 0; n < 3; ++n)
            if (FusionLocal(op, n) && FusionOperand(op, n) == slot)
            {
                if (reads & (1u << n)) return true;
                if (writes & (1u << n)) killed = true;
            }
        if (killed || op.Code() == NOP_RETURN || op.Code() == NOP_ERROR) continue;
        if (op.Code() == NOP_SWITCH)
        {
            if (static_cast<unsigned short>(op.n1) >= switchTargets.size()) return true;
            for (int offset : switchTargets[static_cast<unsigned short>(op.n1)]) pending.push_back(at + 1 + offset);
            continue;
        }
        if (FusionRelativeJump(op.Code())) pending.push_back(at + 1 + op.n1);
        if (op.Code() != NOP_JMP) pending.push_back(at + 1);
    }
    return false;
}

inline void PrepareFusion(ProgramFusion& plan)
{
    for (unsigned i = 0; i < plan.count; ++i)
    {
        const FusionIO io = FusionOperation(plan.operations[i]);
        plan.results[i] = static_cast<u8>(io.result);
        plan.reads[i] = static_cast<u8>(io.reads); plan.writes[i] = static_cast<u8>(io.writes);
        for (unsigned n = 0; n < 3; ++n)
        {
            plan.inputs[i][n] = -1;
            if (!(io.reads & (1u << n))) continue;
            for (unsigned previous = i; previous-- > 0;)
            {
                const FusionIO producer = FusionOperation(plan.operations[previous]);
                if (producer.writes == 1 &&
                    FusionLocal(plan.operations[previous], 0) == FusionLocal(plan.operations[i], n) &&
                    plan.operations[previous].n1 == FusionOperand(plan.operations[i], n))
                {
                    // 항상 스냅샷을 쓰면 컨테이너 저장으로 바뀐 원본 필드를 잘못 재사용할 수 있다.
                    // 프레임의 임시 결과만 전달하고 사용자 변수는 원래 순서대로 다시 읽는다.
                    if (plan.elide & (1u << previous)) plan.inputs[i][n] = static_cast<signed char>(previous);
                    break;
                }
            }
        }
        // 기준 주소와 슬롯 오프셋까지 확정하여 실행 중 플래그·입력 마스크 해석을 없앤다.
        for (unsigned n = 0; n < 3; ++n)
        {
            ProgramFusion::Address& address = plan.addresses[i][n];
            if (!(io.reads & (1u << n))) { address.base = 2; address.slot = 0; }
            else if (plan.inputs[i][n] >= 0) { address.base = 2; address.slot = plan.inputs[i][n]; }
            else { address.base = FusionLocal(plan.operations[i], n) ? 1 : 0; address.slot = FusionOperand(plan.operations[i], n); }
        }
        ProgramFusion::Address& output = plan.addresses[i][3];
        if (!io.writes || (plan.elide & (1u << i))) { output.base = 2; output.slot = static_cast<short>(i); }
        else { output.base = FusionLocal(plan.operations[i], 0) ? 1 : 0; output.slot = plan.operations[i].n1; }
        if (IsArithmeticUpdate(plan.operations[i]))
        {
            // 실행기는 정규화한 좌우 입력을 쓴다. 생략한 임시값의 태그를 되살려야 할 때는 일반 저장을 유지한다.
            const ProgramFusion::Address left = plan.addresses[i][0];
            plan.addresses[i][2] = plan.addresses[i][1];
            plan.addresses[i][1] = left;
        }
    }
}

inline std::vector<ProgramFusion> BuildTypedFusions(const std::vector<SVMOperation>& code,
    const std::vector<SFunctionTable>& functions, const std::vector<std::vector<int>>& switchTargets)
{
    std::vector<ProgramFusion> plans;
    std::vector<bool> boundary(code.size(), false);
    std::vector<int> starts;
    for (const SFunctionTable& function : functions) starts.push_back(function._codePtr / sizeof(SVMOperation));
    std::sort(starts.begin(), starts.end());
    for (int start : starts) if (start >= 0 && static_cast<std::size_t>(start) < boundary.size()) boundary[start] = true;
    for (std::size_t i = 0; i < code.size(); ++i)
    {
        const SVMOperation& op = code[i];
        if (FusionRelativeJump(op.Code()))
        {
            const long long target = static_cast<long long>(i) + 1 + op.n1;
            if (target >= 0 && target < static_cast<long long>(code.size())) boundary[static_cast<std::size_t>(target)] = true;
            if (i + 1 < code.size()) boundary[i + 1] = true;
        }
        if (op.Code() == NOP_SWITCH && static_cast<unsigned short>(op.n1) < switchTargets.size())
            for (int offset : switchTargets[static_cast<unsigned short>(op.n1)])
            {
                const long long target = static_cast<long long>(i) + 1 + offset;
                if (target >= 0 && target < static_cast<long long>(code.size())) boundary[static_cast<std::size_t>(target)] = true;
            }
    }
    for (const SFunctionTable& function : functions)
    {
        const int begin = function._codePtr / sizeof(SVMOperation);
        const auto next = std::upper_bound(starts.begin(), starts.end(), begin);
        const int end = next == starts.end() ? static_cast<int>(code.size()) : *next;
        const int temporaryBegin = 1 + function._argsCount + function._localVarCount;
        const int temporaryEnd = temporaryBegin + function._localTempMax;
        for (int at = begin; at < end;)
        {
            ProgramFusion plan; plan.start = at;
            bool connected = false;
            while (at + plan.count < end && plan.count < ProgramFusion::MaxSteps)
            {
                const int index = at + plan.count;
                if (plan.count && boundary[index]) break;
                const SVMOperation& op = code[index];
                const FusionIO io = FusionOperation(op);
                if (!io.supported) break;
                for (unsigned n = 0; n < 3; ++n)
                    if (io.reads & (1u << n))
                        for (unsigned prior = 0; prior < plan.count; ++prior)
                            if (FusionOperation(plan.operations[prior]).writes == 1 &&
                                FusionLocal(op, n) == FusionLocal(plan.operations[prior], 0) &&
                                FusionOperand(op, n) == plan.operations[prior].n1) connected = true;
                plan.operations[plan.count++] = op;
                if (io.jump) break;
            }
            if (plan.count < 2 || !connected) { ++at; continue; }
            for (unsigned i = 0; i < plan.count; ++i)
            {
                const SVMOperation& op = plan.operations[i];
                if (FusionOperation(op).writes != 1 || !FusionLocal(op, 0) || op.n1 < temporaryBegin || op.n1 >= temporaryEnd) continue;
                bool overwritten = false;
                for (unsigned n = i + 1; n < plan.count; ++n)
                    if (FusionOperation(plan.operations[n]).writes == 1 && FusionLocal(plan.operations[n], 0) &&
                        plan.operations[n].n1 == op.n1) { overwritten = true; break; }
                // 마지막 명령이 분기하면 양쪽 출구를 모두 조사한다.
                const SVMOperation& tail = plan.operations[plan.count - 1];
                bool live = !overwritten && FusionMayReadAfter(code, begin, end, at + plan.count, op.n1, switchTargets);
                if (!overwritten && FusionOperation(tail).jump)
                    live = live || FusionMayReadAfter(code, begin, end, at + plan.count + tail.n1, op.n1, switchTargets);
                if (!live) plan.elide |= static_cast<u8>(1u << i);
            }
            PrepareFusion(plan);
            // 태그를 생략할지 컴파일 때 결정하여 이미지에 저장한다. 로더는 이 결정을 바꾸지 않는다.
            for (unsigned i = 0; i < plan.count; ++i)
            {
                const ProgramFusion::Address& output = plan.addresses[i][3];
                const ProgramFusion::Address& left = plan.addresses[i][1];
                const SVMOperation& instruction = plan.operations[i];
                const SlotKind kind = FusionLocal(instruction, 0) ? FindSlotKind(function._storage, instruction.n1) : SlotKind::Boxed;
                const VAR_TYPE valueType = FusionOperation(instruction).result;
                const bool fixed = IsFixedArithmetic(instruction) ||
                    (kind == SlotKind::Int && valueType == VAR_INT) ||
                    (kind == SlotKind::Float && valueType == VAR_FLOAT) ||
                    (kind == SlotKind::Bool && valueType == VAR_BOOL);
                if (fixed || (output.base != 2 && IsArithmeticUpdate(instruction) &&
                    output.base == left.base && output.slot == left.slot))
                    plan.update |= static_cast<u8>(1u << i);
            }
            plans.push_back(plan);
            at += plan.count;
        }
    }
    std::sort(plans.begin(), plans.end(), [](const ProgramFusion& a, const ProgramFusion& b) { return a.start < b.start; });
    return plans;
}

inline void InstallTypedFusions(std::vector<SVMOperation>& code, const std::vector<ProgramFusion>& plans)
{
    for (std::size_t i = 0; i < plans.size(); ++i)
    {
        const ProgramFusion& plan = plans[i];
        SVMOperation root = {};
        root.SetCode(NOP_FUSED); root.n1 = plan.count; root.n23 = static_cast<int>(i);
        code[plan.start] = root;
        for (unsigned n = 1; n < plan.count; ++n)
        {
            SVMOperation extension = {}; extension.SetCode(NOP_EXTEND);
            code[plan.start + n] = extension;
        }
    }
}

}
