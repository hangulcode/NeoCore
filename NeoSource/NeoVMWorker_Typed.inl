#pragma once

// 정적 프런트엔드가 입력 저장형을 확정한다. 실행 중에는 타입을 다시 고르지 않고
// 알려진 페이로드를 읽으며, 결과 슬롯의 기존 참조와 배열 범위 검사는 유지한다.
// 피연산자는 payload 포인터(GetVarRaw*)로 받고, 컬렉션 원소처럼 태그가 있는 값이나
// 선언 타입이 필요한 저장만 경계 뷰(GetVarPtr*)를 만든다.
NEOS_NOINLINE void StoreMapReference(ValueRef result, ValueRef found)
{
    // 목적지를 해제하면서 원본 맵까지 사라질 수 있으므로 값과 소유권을 먼저 보존한다.
    BoundaryValue value = *found;
    Var_AddRef(&value);
    Var_Release(result);
    *result = value;
}

template<bool StringKey, bool Contains>
NEOS_FORCEINLINE void TypedMapRead(const SVMOperation& op)
{
    VarInfo* receiver = GetVarRaw2(op);
    VarInfo* key = GetVarRaw3(op);
    if (receiver->_tbl == nullptr)
    {
        SetError("cannot access a null map");
        return;
    }
    // 키 종류는 컴파일 때 확정된다. 문자열 키의 null과 키 존재 여부만 실행 중 검사한다.
    ValueRef found = StringKey
        ? (key->_str == nullptr ? ValueRef(nullptr) : receiver->_tbl->FindString(key->_str))
        : receiver->_tbl->FindInt(key->_int);
    if (Contains)
        StoreNumberBool(GetVarRawF1(op), found != nullptr);
    else if (!found)
        SetErrorFormat("map key not found: %s", ToString(ValueRef(key, StringKey ? VAR_STRING : VAR_INT)).c_str());
    else
    {
        ValueRef result = GetVarPtr1(op);
        // 두 값 모두 비참조형이면 소유권 분기·해제 없이 슬롯만 복사한다.
        if (!found.IsAllocType() && !result.IsAllocType()) *result = *found;
        else StoreMapReference(result, found);
    }
}

template<bool StringKey, int Operation>
NEOS_FORCEINLINE void TypedSetOperation(const SVMOperation& op)
{
    VarInfo* receiver = GetVarRaw2(op);
    ValueRef key = GetVarPtr3(op);
    if (receiver->_set == nullptr)
    {
        SetError("cannot access a null set");
        return;
    }
    if (StringKey && key.GetType() == VAR_NONE)
    {
        if (Operation == 1) SetError("set element cannot be null");
        else StoreNumberBool(GetVarRawF1(op), false);
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
    // 삽입·제거와 결과 계산을 끝낸 뒤 목적지에 쓴다.
    StoreNumberBool(GetVarRawF1(op), changed);
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
        assert(value->_vec != nullptr && value->_vec->_refCount > 0);
        std::memcpy(components, value->_vec->v, sizeof(float) * 4);
    }
}

template<int Dimension>
NEOS_NOINLINE void TypedVectorMake(const SVMOperation& op)
{
    float components[4] = {};
    if (Dimension == 2)
    {
        components[0] = NeoNarrowFloat32(GetVarRaw2(op)->_float);
        components[1] = NeoNarrowFloat32(GetVarRaw3(op)->_float);
    }
    else
    {
        const VarInfo* arguments = GetVarRaw_L(op.n2);
        for (int i = 0; i < Dimension; ++i)
            components[i] = NeoNarrowFloat32(arguments[i]._float);
    }
    StoreVector(GetVarPtr1(op), Dimension, components[0], components[1], components[2], components[3]);
}

template<int Dimension, int Operation>
NEOS_NOINLINE void TypedVectorArithmetic(const SVMOperation& op)
{
    float left[4], right[4];
    ReadTypedVector<Dimension>(GetVarRaw2(op), left);
    if (Operation < 2)
        ReadTypedVector<Dimension>(GetVarRaw3(op), right);
    else
    {
        const float scalar = NeoNarrowFloat32(GetVarRaw3(op)->_float);
        for (int i = 0; i < Dimension; ++i) right[i] = scalar;
    }
    // 목적지가 어느 입력과 같아도 해제나 COW 전에 모든 입력 성분을 확보한다.
    for (int i = 0; i < Dimension; ++i)
        left[i] = Operation == 0 ? left[i] + right[i] : Operation == 1 ? left[i] - right[i]
            : Operation == 2 ? left[i] * right[i] : left[i] / right[i];
    StoreVector(GetVarPtr1(op), Dimension, left[0], left[1], left[2], left[3]);
}

template<int Dimension>
NEOS_NOINLINE void TypedVectorGet(const SVMOperation& op)
{
    float components[4];
    ReadTypedVector<Dimension>(GetVarRaw2(op), components);
    StoreNumberFloat(GetVarRawF1(op), static_cast<NS_FLOAT>(components[op.n3]));
}

template<int Dimension>
NEOS_NOINLINE void TypedVectorSet(const SVMOperation& op)
{
    const VarInfo* arguments = GetVarRaw_L(op.n2);
    float components[4];
    ReadTypedVector<Dimension>(&arguments[0], components);
    components[op.n3] = NeoNarrowFloat32(arguments[1]._float);
    // 성분 교체는 새 값을 만든다. 원래 변수·배열·프로퍼티에 되쓰는 것은 별도 명령이다.
    StoreVector(GetVarPtr1(op), Dimension, components[0], components[1], components[2], components[3]);
}

NEOS_NOINLINE void TypedCellNew(const SVMOperation& op)
{
    ListInfo* cell = GetVM()->ListAlloc(1);
    ValueRef value = GetVarPtr2(op);
    // 셀 안 값의 참조·순환 추적은 유지한다. 이 검사는 연산자 타입 선택이 아니다.
    if (value.IsContainerType()) cell->MarkContainerChild();
    Move(&cell->_Bucket[0], value);
    Var_SetList(GetVarPtr1(op), cell);
}

NEOS_NOINLINE void TypedCellGet(const SVMOperation& op)
{
    // 결과 임시가 셀 자체를 보유할 수 있어 셀 해제 전에 원소의 소유권을 확보한다.
    BoundaryValue value = GetVarRaw2(op)->_lst->_Bucket[0];
    Var_AddRef(&value);
    ValueRef result = GetVarPtr1(op);
    Var_Release(result);
    *result = value;
}

NEOS_NOINLINE void TypedCellSet(const SVMOperation& op)
{
    BoundaryValue value = *GetVarPtr2(op);
    Var_AddRef(&value);
    ListInfo* cell = GetVarRaw3(op)->_lst;
    if (value.IsContainerType()) cell->MarkContainerChild();
    Move(&cell->_Bucket[0], &value);
    ValueRef result = GetVarPtr1(op);
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
        BoundaryValue value(array->VectorAt(index));
        if (Element != NeoArrayElementType::Vector2)
        {
            ++value._vec->_refCount;
            ReleaseByPolicy(result, StoragePolicy::Vec);
        }
        *result = static_cast<const VarInfo&>(value);
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
        BoundaryValue copy(Element == NeoArrayElementType::Vector3 ? VAR_VEC3 : VAR_VEC4);
        static_cast<VarInfo&>(copy) = *value;
        ++copy._vec->_refCount;
        ValueRef target = array->VectorAt(index);
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
    // 참조 타입은 null일 수 있다. 배열 슬롯의 payload가 0이면 null이다.
    if (value->_array != nullptr) return true;
    SetError("cannot access a null array");
    return false;
}

template<NeoArrayElementType Element>
NEOS_NOINLINE void TypedArrayNew(const SVMOperation& op)
{
    const NS_INT size = GetVarRaw2(op)->_int;
    if (size < 0 || size > INT32_MAX)
    {
        SetError(RTE_CALL_INVALID);
        return;
    }
    ValueRef initial = GetVarPtr3(op);
    if (!CheckTypedArrayValue<Element>(initial.Data())) return;
    ArrayInfo* array = GetVM()->ArrayAlloc(Element, static_cast<int>(size), BoundaryValue(initial));
    if (!array)
    {
        SetError(RTE_CALL_INVALID);
        return;
    }
    Var_SetArray(GetVarPtr1(op), array);
}

template<NeoArrayElementType Element>
NEOS_NOINLINE void TypedArrayGet(const SVMOperation& op)
{
    const VarInfo* owner = GetVarRaw2(op);
    if (!CheckTypedArrayReceiver(owner)) return;
    ArrayInfo* array = owner->_array;
    const NS_INT index = GetVarRaw3(op)->_int;
    if (!array->IsValidIndex(index))
    {
        SetError(RTE_ARRAY_INDEX_READ);
        return;
    }
    ReadTypedArrayValue<Element>(array, static_cast<int>(index), GetVarRawF1(op));
}

template<NeoArrayElementType Element>
NEOS_NOINLINE void TypedArraySet(const SVMOperation& op)
{
    const VarInfo* arguments = GetVarRaw_L(op.n2);
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
    ReadTypedArrayValue<Element>(array, static_cast<int>(index), GetVarRawF1(op));
}

template<NeoArrayElementType Element>
NEOS_NOINLINE void TypedArrayAppend(const SVMOperation& op)
{
    const VarInfo* owner = GetVarRaw2(op);
    if (!CheckTypedArrayReceiver(owner)) return;
    ArrayInfo* array = owner->_array;
    const int index = array->_count;
    if (index == INT32_MAX)
    {
        SetError(RTE_CALL_INVALID);
        return;
    }
    if (!CheckTypedArrayValue<Element>(GetVarRaw3(op))) return;
    if (!GetVM()->ArrayResize(array, index + 1))
    {
        SetError(RTE_CALL_INVALID);
        return;
    }
    WriteTypedArrayValue<Element>(array, index, GetVarRaw3(op));
    Var_SetNone(GetVarPtr1(op));
}

NEOS_NOINLINE void TypedArrayResize(const SVMOperation& op)
{
    const VarInfo* owner = GetVarRaw2(op);
    if (!CheckTypedArrayReceiver(owner)) return;
    const NS_INT size = GetVarRaw3(op)->_int;
    if (size > INT32_MAX)
    {
        SetError("collection capacity exceeds limit");
        return;
    }
    GetVM()->ArrayResize(owner->_array, size < 0 ? 0 : static_cast<int>(size));
    Var_SetNone(GetVarPtr1(op));
}

NEOS_NOINLINE void TypedArrayReserve(const SVMOperation& op)
{
    const VarInfo* owner = GetVarRaw2(op);
    if (!CheckTypedArrayReceiver(owner)) return;
    const NS_INT size = GetVarRaw3(op)->_int;
    if (size < 0 || size > INT32_MAX)
    {
        SetError(size < 0 ? "reserve capacity must not be negative" : "collection capacity exceeds limit");
        return;
    }
    GetVM()->ArrayReserve(owner->_array, static_cast<int>(size));
    Var_SetNone(GetVarPtr1(op));
}

NEOS_NOINLINE void TypedIntegerRemainder(const SVMOperation& op)
{
    const NS_INT left = GetVarRaw2(op)->_int, right = GetVarRaw3(op)->_int;
    if (CheckIntegerDivision(left, right)) StoreNumberInt(GetVarRawF1(op), left % right);
}

template<bool Left>
NEOS_NOINLINE void TypedIntegerShift(const SVMOperation& op)
{
    const NS_INT value = GetVarRaw2(op)->_int, count = GetVarRaw3(op)->_int;
    if (CheckShiftCount(count))
        StoreNumberInt(GetVarRawF1(op), Left ? NeoShiftLeft(value, count) : NeoShiftRight(value, count));
}

// Owner: 0은 목록, 1은 정수 키 맵, 2는 문자열 키 맵이다. 값의 타입은 템플릿으로 고정한다.
template<int Owner>
NEOS_FORCEINLINE ValueRef ScalarContainerElement(VarInfo* receiver, VarInfo* key, bool write)
{
    if (receiver->_int == 0)
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
    if (Owner == 2 && key->_str == nullptr)
    {
        if (write) SetError("map key cannot be null");
        else SetErrorFormat("map key not found: %s", ToString(ValueRef(key, VAR_STRING)).c_str());
        return nullptr;
    }
    ValueRef found = Owner == 1 ? receiver->_tbl->FindInt(key->_int) : receiver->_tbl->FindString(key->_str);
    if (!found && !write)
        SetErrorFormat("map key not found: %s", ToString(ValueRef(key, Owner == 1 ? VAR_INT : VAR_STRING)).c_str());
    return found;
}

template<int Owner, VAR_TYPE Value>
NEOS_FORCEINLINE void ScalarContainerLoad(VarInfo* result, VarInfo* receiver, VarInfo* key)
{
    ValueRef found = ScalarContainerElement<Owner>(receiver, key, false);
    if (!found) return;
    assert(found.GetType() == Value);
    if (Value == VAR_INT) StoreNumberInt(result, found->_int);
    else if (Value == VAR_FLOAT) StoreNumberFloat(result, found->_float);
    else StoreNumberBool(result, found->_bl);
}

template<int Owner, VAR_TYPE Value>
NEOS_FORCEINLINE void ScalarContainerStore(VarInfo* receiver, VarInfo* key, VarInfo* value)
{
    ValueRef target = ScalarContainerElement<Owner>(receiver, key, true);
    if (IsErrorTrap()) return;
    if (target)
    {
        if (Value == VAR_INT) StoreNumberInt(target, value->_int);
        else if (Value == VAR_FLOAT) StoreNumberFloat(target, value->_float);
        else StoreNumberBool(target, value->_bl);
    }
    else receiver->_tbl->Insert(ValueRef(key, Owner == 1 ? VAR_INT : VAR_STRING), ValueRef(value, Value));
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

// 범용 비교 점프는 문자열 비교 등 타입이 필요하므로 경계 뷰로 넘긴다.
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

// 입력과 목적지가 같은 숫자 슬롯이므로 payload만 갱신한다.
inline void UpdateIntegerArithmetic(const SVMOperation& OP)
{
    VarInfo* value = GetVarRawF1(OP);
    const VarInfo* right = GetVarRaw2(OP);
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
    VarInfo* value = GetVarRawF1(OP);
    const VarInfo* right = GetVarRaw2(OP);
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

// 숫자 목적지는 컴파일러가 확정했다. 고정 여부와 관계없이 payload만 쓴다.
template<bool Fixed>
NEOS_FORCEINLINE void StoreArithmeticInt(VarInfo* target, NS_INT value) { target->_int = value; }
template<bool Fixed>
NEOS_FORCEINLINE void StoreArithmeticFloat(VarInfo* target, NS_FLOAT value) { target->_float = value; }

// 숫자 산술의 한 단계. 세부 연산이 템플릿 인자라 분기가 접힌다. 검사에 실패하면 오류를 올리고 false.
// 디스패치 루프의 런타임 opcode 핸들러와 퓨전 실행기가 같은 본문을 쓴다.
template<ArithmeticOp Sub>
NEOS_FORCEINLINE bool IntegerStep(VarInfo* value, NS_INT a, NS_INT b)
{
    switch (Sub)
    {
    case ArithmeticOp::Add: value->_int = NeoIntAdd(a, b); return true;
    case ArithmeticOp::Sub: value->_int = NeoIntSub(a, b); return true;
    case ArithmeticOp::Mul: value->_int = NeoIntMul(a, b); return true;
    case ArithmeticOp::Div: if (!CheckIntegerDivision(a, b)) return false; value->_int = a / b; return true;
    case ArithmeticOp::Mod: if (!CheckIntegerDivision(a, b)) return false; value->_int = a % b; return true;
    case ArithmeticOp::BitAnd: value->_int = a & b; return true;
    case ArithmeticOp::BitOr: value->_int = a | b; return true;
    case ArithmeticOp::BitXor: value->_int = a ^ b; return true;
    case ArithmeticOp::ShiftLeft: if (!CheckShiftCount(b)) return false; value->_int = NeoShiftLeft(a, b); return true;
    case ArithmeticOp::ShiftRight: if (!CheckShiftCount(b)) return false; value->_int = NeoShiftRight(a, b); return true;
    case ArithmeticOp::Neg: value->_int = NeoIntNeg(a); return true;
    }
    SetError(RTE_UNKNOWN_OP);
    return false;
}
template<ArithmeticOp Sub>
NEOS_FORCEINLINE bool FloatStep(VarInfo* value, NS_FLOAT a, NS_FLOAT b)
{
    switch (Sub)
    {
    case ArithmeticOp::Add: value->_float = a + b; return true;
    case ArithmeticOp::Sub: value->_float = a - b; return true;
    case ArithmeticOp::Mul: value->_float = a * b; return true;
    case ArithmeticOp::Div: value->_float = a / b; return true;
    case ArithmeticOp::Neg: value->_float = -a; return true;
    default: break;
    }
    SetError(RTE_UNKNOWN_OP);
    return false;
}
template<ComparisonOp Sub, typename Number>
NEOS_FORCEINLINE static bool CompareStep(Number a, Number b)
{
    switch (Sub)
    {
    case ComparisonOp::Equal: return a == b;
    case ComparisonOp::Less: return a < b;
    case ComparisonOp::LessEqual: return a <= b;
    case ComparisonOp::NotEqual: return a != b;
    case ComparisonOp::Greater: return a > b;
    case ComparisonOp::GreaterEqual: return a >= b;
    }
    return false;
}
NEOS_FORCEINLINE static bool IntegerStepCanFail(ArithmeticOp sub)
{
    return sub == ArithmeticOp::Div || sub == ArithmeticOp::Mod ||
        sub == ArithmeticOp::ShiftLeft || sub == ArithmeticOp::ShiftRight;
}

// 런타임 opcode 핸들러. 피연산자를 읽어 한 단계를 실행한다. 제자리 형식은 n1이 왼쪽 입력이고 n2가 오른쪽이다.
template<ArithmeticOp Sub, bool Update>
NEOS_FORCEINLINE void I64ArithmeticRT(const SVMOperation& OP)
{
    VarInfo* value = GetVarRawF1(OP);
    const NS_INT a = Update ? value->_int : GetVarRaw2(OP)->_int;
    const NS_INT b = Sub == ArithmeticOp::Neg ? 0 : (Update ? GetVarRaw2(OP)->_int : GetVarRaw3(OP)->_int);
    IntegerStep<Sub>(value, a, b);
}
template<ArithmeticOp Sub, bool Update>
NEOS_FORCEINLINE void F64ArithmeticRT(const SVMOperation& OP)
{
    VarInfo* value = GetVarRawF1(OP);
    const NS_FLOAT a = Update ? value->_float : GetVarRaw2(OP)->_float;
    const NS_FLOAT b = Sub == ArithmeticOp::Neg ? 0 : (Update ? GetVarRaw2(OP)->_float : GetVarRaw3(OP)->_float);
    FloatStep<Sub>(value, a, b);
}
template<ComparisonOp Sub>
NEOS_FORCEINLINE void I64CompareRT(const SVMOperation& OP)
{
    StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == CompareStep<Sub>(GetVarRaw2(OP)->_int, GetVarRaw3(OP)->_int));
}
template<ComparisonOp Sub>
NEOS_FORCEINLINE void F64CompareRT(const SVMOperation& OP)
{
    StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == CompareStep<Sub>(GetVarRaw2(OP)->_float, GetVarRaw3(OP)->_float));
}
template<ComparisonOp Sub>
NEOS_FORCEINLINE void I64JumpCompareRT(const SVMOperation& OP)
{
    if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == CompareStep<Sub>(GetVarRaw2(OP)->_int, GetVarRaw3(OP)->_int)) SetComparisonJump(OP.n1);
}
template<ComparisonOp Sub>
NEOS_FORCEINLINE void F64JumpCompareRT(const SVMOperation& OP)
{
    if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == CompareStep<Sub>(GetVarRaw2(OP)->_float, GetVarRaw3(OP)->_float)) SetComparisonJump(OP.n1);
}

template<bool Fixed>
inline void handle_I64_ARITH(const SVMOperation& OP)
{
    if (OP.argFlag & NEOS_OP_ARITH_UPDATE) { UpdateIntegerArithmetic(OP); return; }
    switch (static_cast<ArithmeticOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ArithmeticOp::Add: StoreArithmeticInt<Fixed>(GetVarRawF1(OP), NeoIntAdd(GetVarRaw2(OP)->_int, GetVarRaw3(OP)->_int)); break;
    case ArithmeticOp::Sub: StoreArithmeticInt<Fixed>(GetVarRawF1(OP), NeoIntSub(GetVarRaw2(OP)->_int, GetVarRaw3(OP)->_int)); break;
    case ArithmeticOp::Mul: StoreArithmeticInt<Fixed>(GetVarRawF1(OP), NeoIntMul(GetVarRaw2(OP)->_int, GetVarRaw3(OP)->_int)); break;
    case ArithmeticOp::Div:
        if (CheckIntegerDivision(GetVarRaw2(OP)->_int, GetVarRaw3(OP)->_int))
            StoreArithmeticInt<Fixed>(GetVarRawF1(OP), GetVarRaw2(OP)->_int / GetVarRaw3(OP)->_int);
        break;
    case ArithmeticOp::Mod:
        if (CheckIntegerDivision(GetVarRaw2(OP)->_int, GetVarRaw3(OP)->_int))
            StoreArithmeticInt<Fixed>(GetVarRawF1(OP), GetVarRaw2(OP)->_int % GetVarRaw3(OP)->_int);
        break;
    case ArithmeticOp::BitAnd: StoreArithmeticInt<Fixed>(GetVarRawF1(OP), GetVarRaw2(OP)->_int & GetVarRaw3(OP)->_int); break;
    case ArithmeticOp::BitOr: StoreArithmeticInt<Fixed>(GetVarRawF1(OP), GetVarRaw2(OP)->_int | GetVarRaw3(OP)->_int); break;
    case ArithmeticOp::BitXor: StoreArithmeticInt<Fixed>(GetVarRawF1(OP), GetVarRaw2(OP)->_int ^ GetVarRaw3(OP)->_int); break;
    case ArithmeticOp::ShiftLeft:
        if (CheckShiftCount(GetVarRaw3(OP)->_int))
            StoreArithmeticInt<Fixed>(GetVarRawF1(OP), NeoShiftLeft(GetVarRaw2(OP)->_int, GetVarRaw3(OP)->_int));
        break;
    case ArithmeticOp::ShiftRight:
        if (CheckShiftCount(GetVarRaw3(OP)->_int))
            StoreArithmeticInt<Fixed>(GetVarRawF1(OP), NeoShiftRight(GetVarRaw2(OP)->_int, GetVarRaw3(OP)->_int));
        break;
    case ArithmeticOp::Neg: StoreArithmeticInt<Fixed>(GetVarRawF1(OP), NeoIntNeg(GetVarRaw2(OP)->_int)); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

template<bool Fixed>
inline void handle_F64_ARITH(const SVMOperation& OP)
{
    if (OP.argFlag & NEOS_OP_ARITH_UPDATE) { UpdateFloatArithmetic(OP); return; }
    switch (static_cast<ArithmeticOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ArithmeticOp::Add: StoreArithmeticFloat<Fixed>(GetVarRawF1(OP), GetVarRaw2(OP)->_float + GetVarRaw3(OP)->_float); break;
    case ArithmeticOp::Sub: StoreArithmeticFloat<Fixed>(GetVarRawF1(OP), GetVarRaw2(OP)->_float - GetVarRaw3(OP)->_float); break;
    case ArithmeticOp::Mul: StoreArithmeticFloat<Fixed>(GetVarRawF1(OP), GetVarRaw2(OP)->_float * GetVarRaw3(OP)->_float); break;
    case ArithmeticOp::Div: StoreArithmeticFloat<Fixed>(GetVarRawF1(OP), GetVarRaw2(OP)->_float / GetVarRaw3(OP)->_float); break;
    case ArithmeticOp::Neg: StoreArithmeticFloat<Fixed>(GetVarRawF1(OP), -GetVarRaw2(OP)->_float); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_NUM_CONVERT(const SVMOperation& OP)
{
    switch (static_cast<ConversionOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ConversionOp::IntToFloat: StoreNumberFloat(GetVarRawF1(OP), static_cast<NS_FLOAT>(GetVarRaw2(OP)->_int)); break;
    case ConversionOp::FloatToInt: StoreNumberInt(GetVarRawF1(OP), NeoFloatToInt(GetVarRaw2(OP)->_float)); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_I64_COMPARE(const SVMOperation& OP)
{
    switch (static_cast<ComparisonOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ComparisonOp::Equal: StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_int == GetVarRaw3(OP)->_int)); break;
    case ComparisonOp::Less: StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_int < GetVarRaw3(OP)->_int)); break;
    case ComparisonOp::LessEqual: StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_int <= GetVarRaw3(OP)->_int)); break;
    case ComparisonOp::NotEqual: StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_int != GetVarRaw3(OP)->_int)); break;
    case ComparisonOp::Greater: StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_int > GetVarRaw3(OP)->_int)); break;
    case ComparisonOp::GreaterEqual: StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_int >= GetVarRaw3(OP)->_int)); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_F64_COMPARE(const SVMOperation& OP)
{
    switch (static_cast<ComparisonOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ComparisonOp::Equal: StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_float == GetVarRaw3(OP)->_float)); break;
    case ComparisonOp::Less: StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_float < GetVarRaw3(OP)->_float)); break;
    case ComparisonOp::LessEqual: StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_float <= GetVarRaw3(OP)->_float)); break;
    case ComparisonOp::NotEqual: StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_float != GetVarRaw3(OP)->_float)); break;
    case ComparisonOp::Greater: StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_float > GetVarRaw3(OP)->_float)); break;
    case ComparisonOp::GreaterEqual: StoreNumberBool(GetVarRawF1(OP), ((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_float >= GetVarRaw3(OP)->_float)); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_JMP_I64_COMPARE(const SVMOperation& OP)
{
    switch (static_cast<ComparisonOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ComparisonOp::Equal: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_int == GetVarRaw3(OP)->_int)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::Less: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_int < GetVarRaw3(OP)->_int)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::LessEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_int <= GetVarRaw3(OP)->_int)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::NotEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_int != GetVarRaw3(OP)->_int)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::Greater: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_int > GetVarRaw3(OP)->_int)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::GreaterEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_int >= GetVarRaw3(OP)->_int)) SetComparisonJump(OP.n1); break;
    default: SetError(RTE_UNKNOWN_OP); break;
    }
}

inline void handle_JMP_F64_COMPARE(const SVMOperation& OP)
{
    switch (static_cast<ComparisonOp>(OP.argFlag & NEOS_OP_SUB_MASK))
    {
    case ComparisonOp::Equal: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_float == GetVarRaw3(OP)->_float)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::Less: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_float < GetVarRaw3(OP)->_float)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::LessEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_float <= GetVarRaw3(OP)->_float)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::NotEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_float != GetVarRaw3(OP)->_float)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::Greater: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_float > GetVarRaw3(OP)->_float)) SetComparisonJump(OP.n1); break;
    case ComparisonOp::GreaterEqual: if (((OP.argFlag & NEOS_OP_COMPARE_INVERT) == 0) == (GetVarRaw2(OP)->_float >= GetVarRaw3(OP)->_float)) SetComparisonJump(OP.n1); break;
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
