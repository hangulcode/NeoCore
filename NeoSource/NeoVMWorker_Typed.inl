#pragma once

// 정적 프런트엔드가 입력 저장형을 확정한다. 실행 중에는 타입을 다시 고르지 않고
// 알려진 페이로드를 읽으며, 결과 슬롯의 기존 참조와 배열 범위 검사는 유지한다.
NEOS_NOINLINE void StoreMapReference(VarInfo* result, const VarInfo* found)
{
    // 목적지를 해제하면서 원본 맵까지 사라질 수 있으므로 값과 소유권을 먼저 보존한다.
    VarInfo value = *found;
    Var_AddRef(&value);
    Var_Release(result);
    *result = value;
}

template<bool StringKey, bool Contains>
NEOS_FORCEINLINE void TypedMapRead(const SVMOperation& op)
{
    VarInfo* receiver = GetVarPtr2(op);
    VarInfo* key = GetVarPtr3(op);
    assert(receiver->GetType() == VAR_MAP || receiver->GetType() == VAR_NONE);
    if (receiver->GetType() == VAR_NONE)
    {
        SetError("cannot access a null map");
        return;
    }
    // 키 종류는 컴파일 때 확정된다. 문자열 키의 null과 키 존재 여부만 실행 중 검사한다.
    VarInfo* found = StringKey
        ? (key->GetType() == VAR_NONE ? nullptr : receiver->_tbl->FindString(key->_str))
        : receiver->_tbl->FindInt(key->_int);
    if (Contains)
        StoreNumberBool(GetVarPtrF1(op), found != nullptr);
    else if (!found)
        SetErrorFormat("map key not found: %s", ToString(key).c_str());
    else
    {
        VarInfo* result = GetVarPtrF1(op);
        // 두 값 모두 비참조형이면 소유권 분기·해제·태그 초기화 없이 슬롯만 복사한다.
        if (!found->IsAllocType() && !result->IsAllocType()) *result = *found;
        else StoreMapReference(result, found);
    }
}

template<bool StringKey, int Operation>
NEOS_FORCEINLINE void TypedSetOperation(const SVMOperation& op)
{
    VarInfo* receiver = GetVarPtr2(op);
    VarInfo* key = GetVarPtr3(op);
    assert(receiver->GetType() == VAR_SET || receiver->GetType() == VAR_NONE);
    if (receiver->GetType() == VAR_NONE)
    {
        SetError("cannot access a null set");
        return;
    }
    if (StringKey && key->GetType() == VAR_NONE)
    {
        if (Operation == 1) SetError("set element cannot be null");
        else StoreNumberBool(GetVarPtrF1(op), false);
        return;
    }
    SetInfo* set = receiver->_set;
    bool changed;
    if (Operation == 0) changed = set->Find(key);
    else
    {
        const int before = set->GetCount();
        if (Operation == 1)
        {
            if (!set->Insert(key)) { SetError(RTE_CALL_INVALID); return; }
        }
        else set->Remove(key);
        changed = set->GetCount() != before;
    }
    // 삽입·제거와 결과 계산을 끝낸 뒤 목적지의 이전 참조를 해제한다.
    StoreNumberBool(GetVarPtrF1(op), changed);
}

template<int Dimension>
static NEOS_FORCEINLINE void ReadTypedVector(const VarInfo* value, float* components)
{
    if (Dimension == 2)
    {
        components[0] = value->_vecXY[0];
        components[1] = value->_vecXY[1];
        components[2] = 0.0f;
        components[3] = 0.0f;
    }
    else
    {
        assert((value->GetType() == VAR_VEC3 || value->GetType() == VAR_VEC4) && value->_vec->_refCount > 0);
        std::memcpy(components, value->_vec->v, sizeof(float) * 4);
    }
}

template<int Dimension>
NEOS_NOINLINE void TypedVectorMake(const SVMOperation& op)
{
    float components[4] = {};
    if (Dimension == 2)
    {
        components[0] = NeoNarrowFloat32(GetVarPtr2(op)->_float);
        components[1] = NeoNarrowFloat32(GetVarPtr3(op)->_float);
    }
    else
    {
        const VarInfo* arguments = GetVarPtr_L(op.n2);
        for (int i = 0; i < Dimension; ++i)
            components[i] = NeoNarrowFloat32(arguments[i]._float);
    }
    StoreVector(GetVarPtrF1(op), Dimension, components[0], components[1], components[2], components[3]);
}

template<int Dimension, int Operation>
NEOS_NOINLINE void TypedVectorArithmetic(const SVMOperation& op)
{
    float left[4], right[4];
    ReadTypedVector<Dimension>(GetVarPtr2(op), left);
    if (Operation < 2)
        ReadTypedVector<Dimension>(GetVarPtr3(op), right);
    else
    {
        const float scalar = NeoNarrowFloat32(GetVarPtr3(op)->_float);
        for (int i = 0; i < Dimension; ++i) right[i] = scalar;
    }
    // 목적지가 어느 입력과 같아도 해제나 COW 전에 모든 입력 성분을 확보한다.
    for (int i = 0; i < Dimension; ++i)
        left[i] = Operation == 0 ? left[i] + right[i] : Operation == 1 ? left[i] - right[i]
            : Operation == 2 ? left[i] * right[i] : left[i] / right[i];
    StoreVector(GetVarPtrF1(op), Dimension, left[0], left[1], left[2], left[3]);
}

template<int Dimension>
NEOS_NOINLINE void TypedVectorGet(const SVMOperation& op)
{
    float components[4];
    ReadTypedVector<Dimension>(GetVarPtr2(op), components);
    StoreNumberFloat(GetVarPtrF1(op), static_cast<NS_FLOAT>(components[op.n3]));
}

template<int Dimension>
NEOS_NOINLINE void TypedVectorSet(const SVMOperation& op)
{
    const VarInfo* arguments = GetVarPtr_L(op.n2);
    float components[4];
    ReadTypedVector<Dimension>(&arguments[0], components);
    components[op.n3] = NeoNarrowFloat32(arguments[1]._float);
    // 성분 교체는 새 값을 만든다. 원래 변수·배열·프로퍼티에 되쓰는 것은 별도 명령이다.
    StoreVector(GetVarPtrF1(op), Dimension, components[0], components[1], components[2], components[3]);
}

NEOS_NOINLINE void TypedCellNew(const SVMOperation& op)
{
    ListInfo* cell = GetVM()->ListAlloc(1);
    VarInfo* value = GetVarPtr2(op);
    // 셀 안 값의 참조·순환 추적은 유지한다. 이 검사는 연산자 타입 선택이 아니다.
    if (value->IsContainerType()) cell->MarkContainerChild();
    Move(&cell->_Bucket[0], value);
    Var_SetList(GetVarPtrF1(op), cell);
}

NEOS_NOINLINE void TypedCellGet(const SVMOperation& op)
{
    // 결과 임시가 셀 자체를 보유할 수 있어 셀 해제 전에 원소의 소유권을 확보한다.
    VarInfo value = GetVarPtr2(op)->_lst->_Bucket[0];
    Var_AddRef(&value);
    VarInfo* result = GetVarPtrF1(op);
    Var_Release(result);
    *result = value;
}

NEOS_NOINLINE void TypedCellSet(const SVMOperation& op)
{
    VarInfo value = *GetVarPtr2(op);
    Var_AddRef(&value);
    ListInfo* cell = GetVarPtr3(op)->_lst;
    if (value.IsContainerType()) cell->MarkContainerChild();
    Move(&cell->_Bucket[0], &value);
    VarInfo* result = GetVarPtrF1(op);
    Var_Release(result);
    *result = value;
}

template<NeoArrayElementType Element>
NEOS_FORCEINLINE void ReadTypedArrayValue(ArrayInfo* array, int index, VarInfo* result)
{
    if (Element == NeoArrayElementType::Bool)
        StoreNumberBool(result, array->GetBool(index));
    else if (Element == NeoArrayElementType::Int)
        StoreNumberInt(result, array->IntData()[index]);
    else if (Element == NeoArrayElementType::Float)
        StoreNumberFloat(result, array->FloatData()[index]);
    else if (Element == NeoArrayElementType::Int32)
        StoreNumberInt(result, array->Int32Data()[index]);
    else if (Element == NeoArrayElementType::Float32)
        StoreNumberFloat(result, array->Float32Data()[index]);
    else
    {
        // 결과 해제로 배열이 사라지더라도 공유 벡터 저장소는 결과가 계속 보유한다.
        VarInfo value = array->VectorValues()[index];
        if (Element != NeoArrayElementType::Vector2) ++value._vec->_refCount;
        Var_Release(result);
        *result = value;
    }
}

template<NeoArrayElementType Element>
NEOS_FORCEINLINE void WriteTypedArrayValue(ArrayInfo* array, int index, const VarInfo* value)
{
    if (Element == NeoArrayElementType::Bool) array->SetBool(index, value->_bl);
    else if (Element == NeoArrayElementType::Int) array->IntData()[index] = value->_int;
    else if (Element == NeoArrayElementType::Float) array->FloatData()[index] = value->_float;
    else if (Element == NeoArrayElementType::Int32) array->Int32Data()[index] = static_cast<std::int32_t>(value->_int);
    else if (Element == NeoArrayElementType::Float32) array->Float32Data()[index] = NeoNarrowFloat32(value->_float);
    else if (Element == NeoArrayElementType::Vector3 || Element == NeoArrayElementType::Vector4)
    {
        VarInfo copy = *value;
        ++copy._vec->_refCount;
        VarInfo* target = &array->VectorValues()[index];
        Var_Release(target);
        *target = copy;
    }
    else array->VectorValues()[index] = *value;
}

template<NeoArrayElementType Element>
NEOS_FORCEINLINE bool CheckTypedArrayValue(const VarInfo* value)
{
    // 정적 타입이 int64여도 32비트 저장 범위는 실제 값으로 검사한다. 변경 전에 호출한다.
    if (Element == NeoArrayElementType::Int32 && (value->_int < INT32_MIN || value->_int > INT32_MAX))
    {
        SetError("int32 array value out of range");
        return false;
    }
    return true;
}

NEOS_FORCEINLINE bool CheckTypedArrayReceiver(const VarInfo* value)
{
    // 참조 타입은 null일 수 있다. ClearType 뒤 남은 이전 포인터 페이로드를 읽기 전에 태그로 검사한다.
    assert(value->GetType() == VAR_ARRAY || value->GetType() == VAR_NONE);
    if (value->GetType() != VAR_NONE) return true;
    SetError("cannot access a null array");
    return false;
}

template<NeoArrayElementType Element>
NEOS_NOINLINE void TypedArrayNew(const SVMOperation& op)
{
    const NS_INT size = GetVarPtr2(op)->_int;
    if (size < 0 || size > INT32_MAX)
    {
        SetError(RTE_CALL_INVALID);
        return;
    }
    const VarInfo* initial = GetVarPtr3(op);
    if (!CheckTypedArrayValue<Element>(initial)) return;
    ArrayInfo* array = GetVM()->ArrayAlloc(Element, static_cast<int>(size), *initial);
    if (!array)
    {
        SetError(RTE_CALL_INVALID);
        return;
    }
    Var_SetArray(GetVarPtrF1(op), array);
}

template<NeoArrayElementType Element>
NEOS_NOINLINE void TypedArrayGet(const SVMOperation& op)
{
    const VarInfo* owner = GetVarPtr2(op);
    if (!CheckTypedArrayReceiver(owner)) return;
    ArrayInfo* array = owner->_array;
    const NS_INT index = GetVarPtr3(op)->_int;
    if (!array->IsValidIndex(index))
    {
        SetError(RTE_ARRAY_INDEX_READ);
        return;
    }
    ReadTypedArrayValue<Element>(array, static_cast<int>(index), GetVarPtrF1(op));
}

template<NeoArrayElementType Element>
NEOS_NOINLINE void TypedArraySet(const SVMOperation& op)
{
    const VarInfo* arguments = GetVarPtr_L(op.n2);
    if (!CheckTypedArrayReceiver(&arguments[1])) return;
    ArrayInfo* array = arguments[1]._array;
    const NS_INT index = arguments[2]._int;
    if (!array->IsValidIndex(index))
    {
        SetError(RTE_ARRAY_INDEX_WRITE);
        return;
    }
    if (!CheckTypedArrayValue<Element>(&arguments[0])) return;
    WriteTypedArrayValue<Element>(array, static_cast<int>(index), &arguments[0]);
    ReadTypedArrayValue<Element>(array, static_cast<int>(index), GetVarPtrF1(op));
}

template<NeoArrayElementType Element>
NEOS_NOINLINE void TypedArrayAppend(const SVMOperation& op)
{
    const VarInfo* owner = GetVarPtr2(op);
    if (!CheckTypedArrayReceiver(owner)) return;
    ArrayInfo* array = owner->_array;
    const int index = array->_count;
    if (index == INT32_MAX)
    {
        SetError(RTE_CALL_INVALID);
        return;
    }
    if (!CheckTypedArrayValue<Element>(GetVarPtr3(op))) return;
    if (!GetVM()->ArrayResize(array, index + 1))
    {
        SetError(RTE_CALL_INVALID);
        return;
    }
    WriteTypedArrayValue<Element>(array, index, GetVarPtr3(op));
    Var_SetNone(GetVarPtrF1(op));
}

NEOS_NOINLINE void TypedArrayResize(const SVMOperation& op)
{
    const VarInfo* owner = GetVarPtr2(op);
    if (!CheckTypedArrayReceiver(owner)) return;
    const NS_INT size = GetVarPtr3(op)->_int;
    if (size > INT32_MAX)
    {
        SetError("collection capacity exceeds limit");
        return;
    }
    GetVM()->ArrayResize(owner->_array, size < 0 ? 0 : static_cast<int>(size));
    Var_SetNone(GetVarPtrF1(op));
}

NEOS_NOINLINE void TypedArrayReserve(const SVMOperation& op)
{
    const VarInfo* owner = GetVarPtr2(op);
    if (!CheckTypedArrayReceiver(owner)) return;
    const NS_INT size = GetVarPtr3(op)->_int;
    if (size < 0 || size > INT32_MAX)
    {
        SetError(size < 0 ? "reserve capacity must not be negative" : "collection capacity exceeds limit");
        return;
    }
    GetVM()->ArrayReserve(owner->_array, static_cast<int>(size));
    Var_SetNone(GetVarPtrF1(op));
}

NEOS_NOINLINE void TypedIntegerRemainder(const SVMOperation& op)
{
    const NS_INT left = GetVarPtr2(op)->_int, right = GetVarPtr3(op)->_int;
    if (CheckIntegerDivision(left, right)) StoreNumberInt(GetVarPtrF1(op), left % right);
}

template<bool Left>
NEOS_NOINLINE void TypedIntegerShift(const SVMOperation& op)
{
    const NS_INT value = GetVarPtr2(op)->_int, count = GetVarPtr3(op)->_int;
    if (CheckShiftCount(count))
        StoreNumberInt(GetVarPtrF1(op), Left ? NeoShiftLeft(value, count) : NeoShiftRight(value, count));
}

// Owner: 0은 목록, 1은 정수 키 맵, 2는 문자열 키 맵이다. 값의 타입은 템플릿으로 고정한다.
template<int Owner>
NEOS_FORCEINLINE VarInfo* ScalarContainerElement(VarInfo* receiver, VarInfo* key, bool write)
{
    assert(receiver->GetType() == (Owner == 0 ? VAR_LIST : VAR_MAP) || receiver->GetType() == VAR_NONE);
    if (receiver->GetType() == VAR_NONE)
    {
        SetError(Owner == 0 ? "cannot access a null list" : "cannot access a null map");
        return nullptr;
    }
    if (Owner == 0)
    {
        const NS_UINT index = static_cast<NS_UINT>(key->_int);
        if (index >= static_cast<NS_UINT>(receiver->_lst->_itemCount))
        {
            SetError(write ? RTE_ARRAY_INDEX_WRITE : RTE_ARRAY_INDEX_READ);
            return nullptr;
        }
        return &receiver->_lst->_Bucket[index];
    }
    if (Owner == 2 && key->GetType() == VAR_NONE)
    {
        if (write) SetError("map key cannot be null");
        else SetErrorFormat("map key not found: %s", ToString(key).c_str());
        return nullptr;
    }
    VarInfo* found = Owner == 1 ? receiver->_tbl->FindInt(key->_int) : receiver->_tbl->FindString(key->_str);
    if (!found && !write) SetErrorFormat("map key not found: %s", ToString(key).c_str());
    return found;
}

template<int Owner, VAR_TYPE Value>
NEOS_FORCEINLINE void ScalarContainerLoad(VarInfo* result, VarInfo* receiver, VarInfo* key)
{
    VarInfo* found = ScalarContainerElement<Owner>(receiver, key, false);
    if (!found) return;
    assert(found->GetType() == Value);
    if (Value == VAR_INT) StoreNumberInt(result, found->_int);
    else if (Value == VAR_FLOAT) StoreNumberFloat(result, found->_float);
    else StoreNumberBool(result, found->_bl);
}

template<int Owner, VAR_TYPE Value>
NEOS_FORCEINLINE void ScalarContainerStore(VarInfo* receiver, VarInfo* key, VarInfo* value)
{
    VarInfo* target = ScalarContainerElement<Owner>(receiver, key, true);
    if (IsErrorTrap()) return;
    assert(value->GetType() == Value);
    if (target)
    {
        if (Value == VAR_INT) StoreNumberInt(target, value->_int);
        else if (Value == VAR_FLOAT) StoreNumberFloat(target, value->_float);
        else StoreNumberBool(target, value->_bl);
    }
    else receiver->_tbl->Insert(key, value);
}

template<NeoArrayElementType Element>
NEOS_FORCEINLINE void ScalarArrayStore(VarInfo* receiver, VarInfo* key, VarInfo* value)
{
    if (!CheckTypedArrayReceiver(receiver)) return;
    if (!receiver->_array->IsValidIndex(key->_int)) { SetError(RTE_ARRAY_INDEX_WRITE); return; }
    if (!CheckTypedArrayValue<Element>(value)) return;
    WriteTypedArrayValue<Element>(receiver->_array, static_cast<int>(key->_int), value);
}

// VM은 기본 opcode로 분기하고, 계열별 함수가 하위 비트의 세부 연산을 처리한다.
inline void handle_COMPARE(const SVMOperation& OP)
{
    switch (static_cast<ComparisonOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ComparisonOp::Equal: handle_EQUAL2_cold(OP); break;
    case ComparisonOp::Less: handle_LESS_cold(OP); break;
    case ComparisonOp::LessEqual: handle_LESS_EQ_cold(OP); break;
    case ComparisonOp::NotEqual: handle_NEQUAL_cold(OP); break;
    case ComparisonOp::Greater: handle_GREAT_cold(OP); break;
    case ComparisonOp::GreaterEqual: handle_GREAT_EQ_cold(OP); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_JMP_COMPARE(const SVMOperation& OP)
{
    switch (static_cast<ComparisonOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ComparisonOp::Equal: if (true == CompareEQ(GetVarPtr2(OP), GetVarPtr3(OP))) SetCodeIncPtr(OP.n1); break;
    case ComparisonOp::Less: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == CompareGR(GetVarPtr3(OP), GetVarPtr2(OP), true)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::LessEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == CompareGE(GetVarPtr3(OP), GetVarPtr2(OP), true)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::NotEqual: if (false == CompareEQ(GetVarPtr2(OP), GetVarPtr3(OP))) SetCodeIncPtr(OP.n1); break;
    case ComparisonOp::Greater: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == CompareGR(GetVarPtr2(OP), GetVarPtr3(OP))) SetComparisonJump(OP.n1); break;
    case ComparisonOp::GreaterEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == CompareGE(GetVarPtr2(OP), GetVarPtr3(OP))) SetComparisonJump(OP.n1); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_JMP_COMPARE_L(const SVMOperation& OP)
{
    switch (static_cast<ComparisonOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ComparisonOp::Equal: if (true == CompareEQ(GetVarPtr_L(OP.n2), GetVarPtr_L(OP.n3))) SetCodeIncPtr(OP.n1); break;
    case ComparisonOp::Less: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == CompareGR(GetVarPtr_L(OP.n3), GetVarPtr_L(OP.n2), true)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::LessEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == CompareGE(GetVarPtr_L(OP.n3), GetVarPtr_L(OP.n2), true)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::NotEqual: if (false == CompareEQ(GetVarPtr_L(OP.n2), GetVarPtr_L(OP.n3))) SetCodeIncPtr(OP.n1); break;
    case ComparisonOp::Greater: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == CompareGR(GetVarPtr_L(OP.n2), GetVarPtr_L(OP.n3))) SetComparisonJump(OP.n1); break;
    case ComparisonOp::GreaterEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == CompareGE(GetVarPtr_L(OP.n2), GetVarPtr_L(OP.n3))) SetComparisonJump(OP.n1); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

// 입력과 목적지가 같은 숫자 슬롯이므로 참조 해제와 타입 태그 쓰기를 반복하지 않는다.
inline void UpdateIntegerArithmetic(const SVMOperation& OP)
{
    VarInfo* value = GetVarPtrF1(OP);
    const VarInfo* right = GetVarPtr2(OP);
    assert(value->GetType() == VAR_INT && right->GetType() == VAR_INT);
    const NS_INT a = value->_int, b = right->_int;
    switch (static_cast<ArithmeticOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ArithmeticOp::Add: value->_int = NeoIntAdd(a, b); break;
    case ArithmeticOp::Sub: value->_int = NeoIntSub(a, b); break;
    case ArithmeticOp::Mul: value->_int = NeoIntMul(a, b); break;
    case ArithmeticOp::Div: if (CheckIntegerDivision(a, b)) value->_int = a / b; break;
    case ArithmeticOp::Mod: if (CheckIntegerDivision(a, b)) value->_int = a % b; break;
    case ArithmeticOp::BitAnd: value->_int = a & b; break;
    case ArithmeticOp::BitOr: value->_int = a | b; break;
    case ArithmeticOp::BitXor: value->_int = a ^ b; break;
    case ArithmeticOp::ShiftLeft: if (CheckShiftCount(b)) value->_int = NeoShiftLeft(a, b); break;
    case ArithmeticOp::ShiftRight: if (CheckShiftCount(b)) value->_int = NeoShiftRight(a, b); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}
inline void UpdateFloatArithmetic(const SVMOperation& OP)
{
    VarInfo* value = GetVarPtrF1(OP);
    const VarInfo* right = GetVarPtr2(OP);
    assert(value->GetType() == VAR_FLOAT && right->GetType() == VAR_FLOAT);
    const NS_FLOAT a = value->_float, b = right->_float;
    switch (static_cast<ArithmeticOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ArithmeticOp::Add: value->_float = a + b; break;
    case ArithmeticOp::Sub: value->_float = a - b; break;
    case ArithmeticOp::Mul: value->_float = a * b; break;
    case ArithmeticOp::Div: value->_float = a / b; break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

template<bool Fixed>
NEOS_FORCEINLINE void StoreArithmeticInt(VarInfo* target, NS_INT value)
{
    if (Fixed) { assert(target->GetType() == VAR_INT); target->_int = value; }
    else StoreNumberInt(target, value);
}
template<bool Fixed>
NEOS_FORCEINLINE void StoreArithmeticFloat(VarInfo* target, NS_FLOAT value)
{
    if (Fixed) { assert(target->GetType() == VAR_FLOAT); target->_float = value; }
    else StoreNumberFloat(target, value);
}

template<bool Fixed>
inline void handle_I64_ARITH(const SVMOperation& OP)
{
    if (OP.argFlag & NEOS_OP_ARITH_UPDATE) { UpdateIntegerArithmetic(OP); return; }
    switch (static_cast<ArithmeticOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ArithmeticOp::Add: StoreArithmeticInt<Fixed>(GetVarPtrF1(OP), NeoIntAdd(GetVarPtr2(OP)->_int, GetVarPtr3(OP)->_int)); break;
    case ArithmeticOp::Sub: StoreArithmeticInt<Fixed>(GetVarPtrF1(OP), NeoIntSub(GetVarPtr2(OP)->_int, GetVarPtr3(OP)->_int)); break;
    case ArithmeticOp::Mul: StoreArithmeticInt<Fixed>(GetVarPtrF1(OP), NeoIntMul(GetVarPtr2(OP)->_int, GetVarPtr3(OP)->_int)); break;
    case ArithmeticOp::Div:
        if (CheckIntegerDivision(GetVarPtr2(OP)->_int, GetVarPtr3(OP)->_int))
            StoreArithmeticInt<Fixed>(GetVarPtrF1(OP), GetVarPtr2(OP)->_int / GetVarPtr3(OP)->_int);
        break;
    case ArithmeticOp::Mod:
        if (CheckIntegerDivision(GetVarPtr2(OP)->_int, GetVarPtr3(OP)->_int))
            StoreArithmeticInt<Fixed>(GetVarPtrF1(OP), GetVarPtr2(OP)->_int % GetVarPtr3(OP)->_int);
        break;
    case ArithmeticOp::BitAnd: StoreArithmeticInt<Fixed>(GetVarPtrF1(OP), GetVarPtr2(OP)->_int & GetVarPtr3(OP)->_int); break;
    case ArithmeticOp::BitOr: StoreArithmeticInt<Fixed>(GetVarPtrF1(OP), GetVarPtr2(OP)->_int | GetVarPtr3(OP)->_int); break;
    case ArithmeticOp::BitXor: StoreArithmeticInt<Fixed>(GetVarPtrF1(OP), GetVarPtr2(OP)->_int ^ GetVarPtr3(OP)->_int); break;
    case ArithmeticOp::ShiftLeft:
        if (CheckShiftCount(GetVarPtr3(OP)->_int))
            StoreArithmeticInt<Fixed>(GetVarPtrF1(OP), NeoShiftLeft(GetVarPtr2(OP)->_int, GetVarPtr3(OP)->_int));
        break;
    case ArithmeticOp::ShiftRight:
        if (CheckShiftCount(GetVarPtr3(OP)->_int))
            StoreArithmeticInt<Fixed>(GetVarPtrF1(OP), NeoShiftRight(GetVarPtr2(OP)->_int, GetVarPtr3(OP)->_int));
        break;
    case ArithmeticOp::Neg: StoreArithmeticInt<Fixed>(GetVarPtrF1(OP), NeoIntNeg(GetVarPtr2(OP)->_int)); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

template<bool Fixed>
inline void handle_F64_ARITH(const SVMOperation& OP)
{
    if (OP.argFlag & NEOS_OP_ARITH_UPDATE) { UpdateFloatArithmetic(OP); return; }
    switch (static_cast<ArithmeticOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ArithmeticOp::Add: StoreArithmeticFloat<Fixed>(GetVarPtrF1(OP), GetVarPtr2(OP)->_float + GetVarPtr3(OP)->_float); break;
    case ArithmeticOp::Sub: StoreArithmeticFloat<Fixed>(GetVarPtrF1(OP), GetVarPtr2(OP)->_float - GetVarPtr3(OP)->_float); break;
    case ArithmeticOp::Mul: StoreArithmeticFloat<Fixed>(GetVarPtrF1(OP), GetVarPtr2(OP)->_float * GetVarPtr3(OP)->_float); break;
    case ArithmeticOp::Div: StoreArithmeticFloat<Fixed>(GetVarPtrF1(OP), GetVarPtr2(OP)->_float / GetVarPtr3(OP)->_float); break;
    case ArithmeticOp::Neg: StoreArithmeticFloat<Fixed>(GetVarPtrF1(OP), -GetVarPtr2(OP)->_float); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_NUM_CONVERT(const SVMOperation& OP)
{
    switch (static_cast<ConversionOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ConversionOp::IntToFloat: StoreNumberFloat(GetVarPtrF1(OP), static_cast<NS_FLOAT>(GetVarPtr2(OP)->_int)); break;
    case ConversionOp::FloatToInt: StoreNumberInt(GetVarPtrF1(OP), NeoFloatToInt(GetVarPtr2(OP)->_float)); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_I64_COMPARE(const SVMOperation& OP)
{
    switch (static_cast<ComparisonOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ComparisonOp::Equal: StoreNumberBool(GetVarPtrF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_int == GetVarPtr3(OP)->_int)); break;
    case ComparisonOp::Less: StoreNumberBool(GetVarPtrF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_int < GetVarPtr3(OP)->_int)); break;
    case ComparisonOp::LessEqual: StoreNumberBool(GetVarPtrF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_int <= GetVarPtr3(OP)->_int)); break;
    case ComparisonOp::NotEqual: StoreNumberBool(GetVarPtrF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_int != GetVarPtr3(OP)->_int)); break;
    case ComparisonOp::Greater: StoreNumberBool(GetVarPtrF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_int > GetVarPtr3(OP)->_int)); break;
    case ComparisonOp::GreaterEqual: StoreNumberBool(GetVarPtrF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_int >= GetVarPtr3(OP)->_int)); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_F64_COMPARE(const SVMOperation& OP)
{
    switch (static_cast<ComparisonOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ComparisonOp::Equal: StoreNumberBool(GetVarPtrF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_float == GetVarPtr3(OP)->_float)); break;
    case ComparisonOp::Less: StoreNumberBool(GetVarPtrF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_float < GetVarPtr3(OP)->_float)); break;
    case ComparisonOp::LessEqual: StoreNumberBool(GetVarPtrF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_float <= GetVarPtr3(OP)->_float)); break;
    case ComparisonOp::NotEqual: StoreNumberBool(GetVarPtrF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_float != GetVarPtr3(OP)->_float)); break;
    case ComparisonOp::Greater: StoreNumberBool(GetVarPtrF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_float > GetVarPtr3(OP)->_float)); break;
    case ComparisonOp::GreaterEqual: StoreNumberBool(GetVarPtrF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_float >= GetVarPtr3(OP)->_float)); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_JMP_I64_COMPARE(const SVMOperation& OP)
{
    switch (static_cast<ComparisonOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ComparisonOp::Equal: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_int == GetVarPtr3(OP)->_int)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::Less: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_int < GetVarPtr3(OP)->_int)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::LessEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_int <= GetVarPtr3(OP)->_int)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::NotEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_int != GetVarPtr3(OP)->_int)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::Greater: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_int > GetVarPtr3(OP)->_int)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::GreaterEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_int >= GetVarPtr3(OP)->_int)) SetComparisonJump(OP.n1); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_JMP_F64_COMPARE(const SVMOperation& OP)
{
    switch (static_cast<ComparisonOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ComparisonOp::Equal: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_float == GetVarPtr3(OP)->_float)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::Less: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_float < GetVarPtr3(OP)->_float)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::LessEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_float <= GetVarPtr3(OP)->_float)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::NotEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_float != GetVarPtr3(OP)->_float)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::Greater: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_float > GetVarPtr3(OP)->_float)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::GreaterEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarPtr2(OP)->_float >= GetVarPtr3(OP)->_float)) SetComparisonJump(OP.n1); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_V2_ARITH(const SVMOperation& OP)
{
    switch (static_cast<ArithmeticOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ArithmeticOp::Add: TypedVectorArithmetic<2, 0>(OP); break;
    case ArithmeticOp::Sub: TypedVectorArithmetic<2, 1>(OP); break;
    case ArithmeticOp::Mul: TypedVectorArithmetic<2, 2>(OP); break;
    case ArithmeticOp::Div: TypedVectorArithmetic<2, 3>(OP); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_V3_ARITH(const SVMOperation& OP)
{
    switch (static_cast<ArithmeticOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ArithmeticOp::Add: TypedVectorArithmetic<3, 0>(OP); break;
    case ArithmeticOp::Sub: TypedVectorArithmetic<3, 1>(OP); break;
    case ArithmeticOp::Mul: TypedVectorArithmetic<3, 2>(OP); break;
    case ArithmeticOp::Div: TypedVectorArithmetic<3, 3>(OP); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_V4_ARITH(const SVMOperation& OP)
{
    switch (static_cast<ArithmeticOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ArithmeticOp::Add: TypedVectorArithmetic<4, 0>(OP); break;
    case ArithmeticOp::Sub: TypedVectorArithmetic<4, 1>(OP); break;
    case ArithmeticOp::Mul: TypedVectorArithmetic<4, 2>(OP); break;
    case ArithmeticOp::Div: TypedVectorArithmetic<4, 3>(OP); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}
