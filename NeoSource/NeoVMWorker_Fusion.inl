#pragma once

// 연산 목록은 그대로 두고, 자주 반복되는 두 연산의 분기를 C++ 인스턴스화로 없앤다.
// 슬롯·상수·컨테이너 조합을 위한 별도 바이트코드는 추가하지 않는다.
#define NEOCORE_FUSION_ARRAY_GET(TYPE, ELEMENT) \
        case NOP_ARRAY_##TYPE##_GET: NEOCORE_FUSION_FAULT_LOCATION(); \
            if (!CheckTypedArrayReceiver(left)) return false; \
            if (!left->_array->IsValidIndex(right->_int)) { SetError(RTE_ARRAY_INDEX_READ); return false; } \
            ReadTypedArrayValue<NeoArrayElementType::ELEMENT>(left->_array, static_cast<int>(right->_int), value); break;
#define NEOCORE_FUSION_CONTAINER(OWNER, INDEX, TYPE, TAG) \
        case NOP_##OWNER##_##TYPE##_LOAD: NEOCORE_FUSION_FAULT_LOCATION(); ScalarContainerLoad<INDEX, TAG>(value, left, right); if (IsErrorTrap()) return false; break; \
        case NOP_##OWNER##_##TYPE##_STORE: NEOCORE_FUSION_FAULT_LOCATION(); ScalarContainerStore<INDEX, TAG>(receiver, left, right); if (IsErrorTrap()) return false; break;
#define NEOCORE_FUSION_OWNER(OWNER, INDEX) \
        NEOCORE_FUSION_CONTAINER(OWNER, INDEX, I64, VAR_INT) \
        NEOCORE_FUSION_CONTAINER(OWNER, INDEX, F64, VAR_FLOAT) \
        NEOCORE_FUSION_CONTAINER(OWNER, INDEX, BOOL, VAR_BOOL)
#define NEOCORE_FUSION_ARRAY_STORE(TYPE, ELEMENT) \
        case NOP_ARRAY_##TYPE##_STORE: NEOCORE_FUSION_FAULT_LOCATION(); ScalarArrayStore<NeoArrayElementType::ELEMENT>(receiver, left, right); if (IsErrorTrap()) return false; break;
#define NEOCORE_FUSION_SECOND(OP) case OP: return SelectFusionPair<First, OP>(plan);
#define NEOCORE_FUSION_FIRST(OP) case OP: plan.execute = FusionPairExecutor<OP>(second, plan); break;

// 컴파일 때 숫자 태그 유지가 증명된 목적지에만 값 부분을 갱신한다.
NEOS_FORCEINLINE static void FusionUpdate(VarInfo* value, NS_INT number)
{ assert(value->GetType() == VAR_INT); value->_int = number; }
NEOS_FORCEINLINE static void FusionUpdate(VarInfo* value, NS_FLOAT number)
{ assert(value->GetType() == VAR_FLOAT); value->_float = number; }
NEOS_FORCEINLINE void FusionArithmeticStore(VarInfo* value, NS_INT number, bool update)
{ if (update) FusionUpdate(value, number); else StoreNumberInt(value, number); }
NEOS_FORCEINLINE void FusionArithmeticStore(VarInfo* value, NS_FLOAT number, bool update)
{ if (update) FusionUpdate(value, number); else StoreNumberFloat(value, number); }

template<OperationCode Operation>
NEOS_FORCEINLINE bool ExecuteFusionStep(const ProgramFusion& plan, unsigned i, VarInfo** bases,
    const SVMOperation* code, const SVMOperation*& next)
{
        const SVMOperation& op = plan.operations[i];
        const ProgramFusion::Address* address = plan.addresses[i];
        VarInfo* receiver = bases[address[0].base] + address[0].slot;
        VarInfo* left = bases[address[1].base] + address[1].slot;
        VarInfo* right = bases[address[2].base] + address[2].slot;
        VarInfo* value = bases[address[3].base] + address[3].slot;
        bool jump = false;
        // 실패할 수 있는 연산만 원래 IP를 기록한다. 성공한 그룹은 끝에서 한 번 이동한다.
#define NEOCORE_FUSION_FAULT_LOCATION() (_pCodeCurrent = code + i + 1)
        const eNOperation primary = PrimaryOperation(ArithmeticMeaning(Operation == NOP_NONE ? op.Code() : Operation));
        const u8 sub = Operation == NOP_NONE ? (op.argFlag & NEOS_OP_SUB_MASK) : OperationSubCode(Operation);
        switch (primary)
        {
        case NOP_LOG_NOT: StoreNumberBool(value, !left->_bl); break;
        case NOP_I64_ARITH:
            switch (static_cast<ArithmeticOp>(sub))
            {
            case ArithmeticOp::Add: FusionArithmeticStore(value, NeoIntAdd(left->_int, right->_int), ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::Sub: FusionArithmeticStore(value, NeoIntSub(left->_int, right->_int), ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::Mul: FusionArithmeticStore(value, NeoIntMul(left->_int, right->_int), ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::Div: NEOCORE_FUSION_FAULT_LOCATION();
                if (!CheckIntegerDivision(left->_int, right->_int)) return false;
                FusionArithmeticStore(value, left->_int / right->_int, ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::Mod: NEOCORE_FUSION_FAULT_LOCATION();
                if (!CheckIntegerDivision(left->_int, right->_int)) return false;
                FusionArithmeticStore(value, left->_int % right->_int, ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::BitAnd: FusionArithmeticStore(value, left->_int & right->_int, ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::BitOr: FusionArithmeticStore(value, left->_int | right->_int, ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::BitXor: FusionArithmeticStore(value, left->_int ^ right->_int, ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::ShiftLeft: NEOCORE_FUSION_FAULT_LOCATION();
                if (!CheckShiftCount(right->_int)) return false;
                FusionArithmeticStore(value, NeoShiftLeft(left->_int, right->_int), ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::ShiftRight: NEOCORE_FUSION_FAULT_LOCATION();
                if (!CheckShiftCount(right->_int)) return false;
                FusionArithmeticStore(value, NeoShiftRight(left->_int, right->_int), ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::Neg: FusionArithmeticStore(value, NeoIntNeg(left->_int), ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            default: NEOCORE_FUSION_FAULT_LOCATION(); SetError(RTE_UNKNOWN_OP); return false;
            }
            break;
        case NOP_F64_ARITH:
            switch (static_cast<ArithmeticOp>(sub))
            {
            case ArithmeticOp::Add: FusionArithmeticStore(value, left->_float + right->_float, ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::Sub: FusionArithmeticStore(value, left->_float - right->_float, ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::Mul: FusionArithmeticStore(value, left->_float * right->_float, ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::Div: FusionArithmeticStore(value, left->_float / right->_float, ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            case ArithmeticOp::Neg: FusionArithmeticStore(value, -left->_float, ((plan.update & ~plan.elide) & (1u << i)) != 0); break;
            default: NEOCORE_FUSION_FAULT_LOCATION(); SetError(RTE_UNKNOWN_OP); return false;
            }
            break;
        case NOP_NUM_CONVERT:
            switch (static_cast<ConversionOp>(sub))
            {
            case ConversionOp::IntToFloat: StoreNumberFloat(value, static_cast<NS_FLOAT>(left->_int)); break;
            case ConversionOp::FloatToInt: StoreNumberInt(value, NeoFloatToInt(left->_float)); break;
            default: NEOCORE_FUSION_FAULT_LOCATION(); SetError(RTE_UNKNOWN_OP); return false;
            }
            break;
        case NOP_I64_COMPARE:
            switch (static_cast<ComparisonOp>(sub))
            {
            case ComparisonOp::Equal: StoreNumberBool(value, ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_int == right->_int)); break;
            case ComparisonOp::Less: StoreNumberBool(value, ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_int < right->_int)); break;
            case ComparisonOp::LessEqual: StoreNumberBool(value, ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_int <= right->_int)); break;
            case ComparisonOp::NotEqual: StoreNumberBool(value, ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_int != right->_int)); break;
            case ComparisonOp::Greater: StoreNumberBool(value, ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_int > right->_int)); break;
            case ComparisonOp::GreaterEqual: StoreNumberBool(value, ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_int >= right->_int)); break;
            default: NEOCORE_FUSION_FAULT_LOCATION(); SetError(RTE_UNKNOWN_OP); return false;
            }
            break;
        case NOP_F64_COMPARE:
            switch (static_cast<ComparisonOp>(sub))
            {
            case ComparisonOp::Equal: StoreNumberBool(value, ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_float == right->_float)); break;
            case ComparisonOp::Less: StoreNumberBool(value, ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_float < right->_float)); break;
            case ComparisonOp::LessEqual: StoreNumberBool(value, ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_float <= right->_float)); break;
            case ComparisonOp::NotEqual: StoreNumberBool(value, ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_float != right->_float)); break;
            case ComparisonOp::Greater: StoreNumberBool(value, ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_float > right->_float)); break;
            case ComparisonOp::GreaterEqual: StoreNumberBool(value, ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_float >= right->_float)); break;
            default: NEOCORE_FUSION_FAULT_LOCATION(); SetError(RTE_UNKNOWN_OP); return false;
            }
            break;
        case NOP_JMP_I64_COMPARE:
            switch (static_cast<ComparisonOp>(sub))
            {
            case ComparisonOp::Equal: jump = ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_int == right->_int); break;
            case ComparisonOp::Less: jump = ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_int < right->_int); break;
            case ComparisonOp::LessEqual: jump = ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_int <= right->_int); break;
            case ComparisonOp::NotEqual: jump = ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_int != right->_int); break;
            case ComparisonOp::Greater: jump = ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_int > right->_int); break;
            case ComparisonOp::GreaterEqual: jump = ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_int >= right->_int); break;
            default: NEOCORE_FUSION_FAULT_LOCATION(); SetError(RTE_UNKNOWN_OP); return false;
            }
            break;
        case NOP_JMP_F64_COMPARE:
            switch (static_cast<ComparisonOp>(sub))
            {
            case ComparisonOp::Equal: jump = ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_float == right->_float); break;
            case ComparisonOp::Less: jump = ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_float < right->_float); break;
            case ComparisonOp::LessEqual: jump = ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_float <= right->_float); break;
            case ComparisonOp::NotEqual: jump = ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_float != right->_float); break;
            case ComparisonOp::Greater: jump = ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_float > right->_float); break;
            case ComparisonOp::GreaterEqual: jump = ((op.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (left->_float >= right->_float); break;
            default: NEOCORE_FUSION_FAULT_LOCATION(); SetError(RTE_UNKNOWN_OP); return false;
            }
            break;
        NEOCORE_FUSION_ARRAY_GET(I64, Int)
        NEOCORE_FUSION_ARRAY_GET(F64, Float)
        NEOCORE_FUSION_ARRAY_GET(BOOL, Bool)
        NEOCORE_FUSION_ARRAY_GET(I32, Int32)
        NEOCORE_FUSION_ARRAY_GET(F32, Float32)
        NEOCORE_FUSION_OWNER(LIST, 0)
        NEOCORE_FUSION_OWNER(MAP_INT, 1)
        NEOCORE_FUSION_OWNER(MAP_STR, 2)
        NEOCORE_FUSION_ARRAY_STORE(I64, Int)
        NEOCORE_FUSION_ARRAY_STORE(F64, Float)
        NEOCORE_FUSION_ARRAY_STORE(BOOL, Bool)
        NEOCORE_FUSION_ARRAY_STORE(I32, Int32)
        NEOCORE_FUSION_ARRAY_STORE(F32, Float32)
        default: NEOCORE_FUSION_FAULT_LOCATION(); SetError(RTE_UNKNOWN_OP); return false;
        }
        // 임시 저장을 생략해도 원래 슬롯의 참조 해제 시점은 지킨다.
        if ((plan.elide & (1u << i)) && !(plan.update & (1u << i))) Var_Release(GetVarPtr_L(op.n1));
        if (jump) next += op.n1;
#undef NEOCORE_FUSION_FAULT_LOCATION
    return true;
}

static NEOS_NOINLINE void ExecuteTypedFusion(CNeoVMWorker* worker, const ProgramFusion& plan)
{
    VarInfo values[ProgramFusion::MaxSteps];
    // 상수 영역이 비어 있어도 기준 주소만 구한다. 0번 원소를 조회하지 않는다.
    VarInfo* bases[] = { worker->m_pVarGlobal_Pointer, worker->m_pVarStack_Pointer, values };
    const SVMOperation* code = reinterpret_cast<const SVMOperation*>(worker->_pCodeBegin) + plan.start;
    const SVMOperation* next = code + plan.count;
    for (unsigned i = 0; i < plan.count; ++i)
        if (!worker->ExecuteFusionStep<NOP_NONE>(plan, i, bases, code, next)) return;
    worker->_pCodeCurrent = next;
}

template<OperationCode First, OperationCode Second>
static NEOS_NOINLINE void ExecuteTypedFusionPair(CNeoVMWorker* worker, const ProgramFusion& plan)
{
    VarInfo values[ProgramFusion::MaxSteps];
    // 상수 영역이 비어 있어도 기준 주소만 구한다. 0번 원소를 조회하지 않는다.
    VarInfo* bases[] = { worker->m_pVarGlobal_Pointer, worker->m_pVarStack_Pointer, values };
    const SVMOperation* code = reinterpret_cast<const SVMOperation*>(worker->_pCodeBegin) + plan.start;
    const SVMOperation* next = code + plan.count;
    for (unsigned i = 0; i < plan.count; i += 2)
    {
        if (!worker->ExecuteFusionStep<First>(plan, i, bases, code, next)) return;
        if (!worker->ExecuteFusionStep<Second>(plan, i + 1, bases, code, next)) return;
    }
    worker->_pCodeCurrent = next;
}

// 두 숫자 연산만 있는 지역 슬롯 그룹은 범용 VarInfo 작업 배열을 만들지 않는다.
// 중간값은 C++ 숫자로 전달하되, 원래 목적지의 참조 해제와 사용자 변수 쓰기는 유지한다.
NEOS_FORCEINLINE static NS_INT FusionNumber(const VarInfo* value, NS_INT*) { return value->_int; }
NEOS_FORCEINLINE static NS_FLOAT FusionNumber(const VarInfo* value, NS_FLOAT*) { return value->_float; }
// 참조 해제가 필요한 목적지는 그룹 전체를 기존 실행기로 보낸 뒤 이 경로에 들어온다.
NEOS_FORCEINLINE static void FusionStore(VarInfo* value, NS_INT number)
{
    assert(!value->IsAllocType());
    value->SetType(VAR_INT); value->_int = number;
}
NEOS_FORCEINLINE static void FusionStore(VarInfo* value, NS_FLOAT number)
{
    assert(!value->IsAllocType());
    value->SetType(VAR_FLOAT); value->_float = number;
}

template<OperationCode Operation>
NEOS_FORCEINLINE bool FusionArithmetic(NS_INT left, NS_INT right, NS_INT& result)
{
    switch (static_cast<ArithmeticOp>(OperationSubCode(Operation)))
    {
    case ArithmeticOp::Add: result = NeoIntAdd(left, right); break;
    case ArithmeticOp::Sub: result = NeoIntSub(left, right); break;
    case ArithmeticOp::Mul: result = NeoIntMul(left, right); break;
    case ArithmeticOp::Div:
        if (!CheckIntegerDivision(left, right)) return false;
        result = left / right; break;
    case ArithmeticOp::Mod:
        if (!CheckIntegerDivision(left, right)) return false;
        result = left % right; break;
    case ArithmeticOp::BitAnd: result = left & right; break;
    case ArithmeticOp::BitOr: result = left | right; break;
    case ArithmeticOp::BitXor: result = left ^ right; break;
    case ArithmeticOp::ShiftLeft:
        if (!CheckShiftCount(right)) return false;
        result = NeoShiftLeft(left, right); break;
    case ArithmeticOp::ShiftRight:
        if (!CheckShiftCount(right)) return false;
        result = NeoShiftRight(left, right); break;
    case ArithmeticOp::Neg: result = NeoIntNeg(left); break;
    default: SetError(RTE_UNKNOWN_OP); return false;
    }
    return true;
}

template<OperationCode Operation>
NEOS_FORCEINLINE bool FusionArithmetic(NS_FLOAT left, NS_FLOAT right, NS_FLOAT& result)
{
    switch (static_cast<ArithmeticOp>(OperationSubCode(Operation)))
    {
    case ArithmeticOp::Add: result = left + right; break;
    case ArithmeticOp::Sub: result = left - right; break;
    case ArithmeticOp::Mul: result = left * right; break;
    case ArithmeticOp::Div: result = left / right; break;
    case ArithmeticOp::Neg: result = -left; break;
    default: SetError(RTE_UNKNOWN_OP); return false;
    }
    return true;
}

template<OperationCode Operation>
NEOS_FORCEINLINE static bool FusionArithmeticCanFail()
{
    return PrimaryOperation(Operation) == NOP_I64_ARITH &&
        (OperationSubCode(Operation) == static_cast<u8>(ArithmeticOp::Div) ||
         OperationSubCode(Operation) == static_cast<u8>(ArithmeticOp::Mod) ||
         OperationSubCode(Operation) == static_cast<u8>(ArithmeticOp::ShiftLeft) ||
         OperationSubCode(Operation) == static_cast<u8>(ArithmeticOp::ShiftRight));
}

template<OperationCode First, OperationCode Second, typename Number>
static NEOS_NOINLINE void ExecuteScalarFusionPair(CNeoVMWorker* worker, const ProgramFusion& plan)
{
    VarInfo* stack = worker->m_pVarStack_Pointer;
    const SVMOperation& first = plan.operations[0];
    const SVMOperation& second = plan.operations[1];
    // 원래 해제 순서를 보존하며, 흔한 숫자 경로에는 해제 호출용 레지스터 보관을 남기지 않는다.
    if ((!(plan.update & 1) && !IsFixedArithmetic(first) && stack[first.n1].IsAllocType()) ||
        (!(plan.update & 2) && !IsFixedArithmetic(second) && stack[second.n1].IsAllocType()))
    {
        ExecuteTypedFusionPair<First, Second>(worker, plan);
        return;
    }
    const SVMOperation* code = reinterpret_cast<const SVMOperation*>(worker->_pCodeBegin) + plan.start;
    Number middle, result;
    const Number left = FusionNumber(stack + plan.addresses[0][1].slot, static_cast<Number*>(nullptr));
    const Number right = OperationSubCode(First) == static_cast<u8>(ArithmeticOp::Neg) ? Number() :
        FusionNumber(stack + plan.addresses[0][2].slot, static_cast<Number*>(nullptr));
    if (FusionArithmeticCanFail<First>()) worker->_pCodeCurrent = code + 1;
    if (!worker->FusionArithmetic<First>(left, right, middle)) return;
    if (plan.elide & 1) { if (!(plan.update & 1)) stack[first.n1].ClearType(); }
    else if (plan.update & 1) FusionUpdate(stack + first.n1, middle);
    else worker->FusionStore(stack + first.n1, middle);
    // 두 연산 사이에는 호출·컨테이너 저장이 없으므로 같은 슬롯의 결과를 직접 전달할 수 있다.
    const Number nextLeft = plan.addresses[1][1].base == 2 ? middle :
        FusionNumber(stack + plan.addresses[1][1].slot, static_cast<Number*>(nullptr));
    const Number nextRight = OperationSubCode(Second) == static_cast<u8>(ArithmeticOp::Neg) ? Number() :
        plan.addresses[1][2].base == 2 ? middle : FusionNumber(stack + plan.addresses[1][2].slot, static_cast<Number*>(nullptr));
    if (FusionArithmeticCanFail<Second>()) worker->_pCodeCurrent = code + 2;
    if (!worker->FusionArithmetic<Second>(nextLeft, nextRight, result)) return;
    if (plan.elide & 2) { if (!(plan.update & 2)) stack[second.n1].ClearType(); }
    else if (plan.update & 2) FusionUpdate(stack + second.n1, result);
    else worker->FusionStore(stack + second.n1, result);
    worker->_pCodeCurrent = code + 2;
}

static bool FusionHasOnlyLocalOperands(const ProgramFusion& plan)
{
    for (unsigned i = 0; i < plan.count; ++i)
        for (unsigned n = 0; n < 3; ++n)
            if (((plan.reads[i] | plan.writes[i]) & (1u << n)) &&
                !(plan.operations[i].argFlag & (NEOS_ARG_N1_LOCAL >> n))) return false;
    return true;
}

template<OperationCode First, OperationCode Second>
static ProgramFusion::Executor SelectFusionPair(const ProgramFusion& plan)
{
    if (plan.count == 2 && FusionHasOnlyLocalOperands(plan))
    {
        if (PrimaryOperation(First) == NOP_I64_ARITH && PrimaryOperation(Second) == NOP_I64_ARITH)
            return &ExecuteScalarFusionPair<First, Second, NS_INT>;
        if (PrimaryOperation(First) == NOP_F64_ARITH && PrimaryOperation(Second) == NOP_F64_ARITH)
            return &ExecuteScalarFusionPair<First, Second, NS_FLOAT>;
    }
    return &ExecuteTypedFusionPair<First, Second>;
}

template<OperationCode First>
static ProgramFusion::Executor FusionPairExecutor(OperationCode second, const ProgramFusion& plan)
{
    switch (second)
    {
        NEOCORE_FUSION_SECOND(OP_I64_ADD)
        NEOCORE_FUSION_SECOND(OP_F64_ADD)
        NEOCORE_FUSION_SECOND(OP_I64_SUB)
        NEOCORE_FUSION_SECOND(OP_F64_SUB)
        NEOCORE_FUSION_SECOND(OP_I64_MUL)
        NEOCORE_FUSION_SECOND(OP_F64_MUL)
        NEOCORE_FUSION_SECOND(OP_I64_DIV)
        NEOCORE_FUSION_SECOND(OP_F64_DIV)
        NEOCORE_FUSION_SECOND(OP_I64_MOD)
        NEOCORE_FUSION_SECOND(OP_I64_AND)
        NEOCORE_FUSION_SECOND(OP_I64_OR)
        NEOCORE_FUSION_SECOND(OP_I64_XOR)
        NEOCORE_FUSION_SECOND(OP_I64_SHL)
        NEOCORE_FUSION_SECOND(OP_I64_SHR)
        default: return &ExecuteTypedFusion;
    }
}

static void ConfigureFusion(ProgramFusion& plan)
{
    plan.execute = &ExecuteTypedFusion;
    if (plan.count % 2 != 0) return;
    const OperationCode first = ArithmeticMeaning(plan.operations[0].Code()), second = ArithmeticMeaning(plan.operations[1].Code());
    for (unsigned i = 2; i < plan.count; ++i)
        if (ArithmeticMeaning(plan.operations[i].Code()) != (i % 2 ? second : first)) return;
    switch (first)
    {
        NEOCORE_FUSION_FIRST(OP_I64_ADD)
        NEOCORE_FUSION_FIRST(OP_F64_ADD)
        NEOCORE_FUSION_FIRST(NOP_ARRAY_I64_GET)
        NEOCORE_FUSION_FIRST(NOP_ARRAY_F64_GET)
        NEOCORE_FUSION_FIRST(NOP_ARRAY_I32_GET)
        NEOCORE_FUSION_FIRST(NOP_ARRAY_F32_GET)
        NEOCORE_FUSION_FIRST(NOP_LIST_I64_LOAD)
        NEOCORE_FUSION_FIRST(NOP_LIST_F64_LOAD)
        NEOCORE_FUSION_FIRST(NOP_MAP_INT_I64_LOAD)
        NEOCORE_FUSION_FIRST(NOP_MAP_INT_F64_LOAD)
        NEOCORE_FUSION_FIRST(NOP_MAP_STR_I64_LOAD)
        NEOCORE_FUSION_FIRST(NOP_MAP_STR_F64_LOAD)
        NEOCORE_FUSION_FIRST(OP_I64_SUB)
        NEOCORE_FUSION_FIRST(OP_F64_SUB)
        NEOCORE_FUSION_FIRST(OP_I64_MUL)
        NEOCORE_FUSION_FIRST(OP_F64_MUL)
        NEOCORE_FUSION_FIRST(OP_F64_DIV)
        NEOCORE_FUSION_FIRST(OP_I64_AND)
        NEOCORE_FUSION_FIRST(OP_I64_OR)
        NEOCORE_FUSION_FIRST(OP_I64_XOR)
        default: break;
    }
}

#undef NEOCORE_FUSION_ARRAY_GET
#undef NEOCORE_FUSION_ARRAY_STORE
#undef NEOCORE_FUSION_CONTAINER
#undef NEOCORE_FUSION_FIRST
#undef NEOCORE_FUSION_OWNER
#undef NEOCORE_FUSION_SECOND
