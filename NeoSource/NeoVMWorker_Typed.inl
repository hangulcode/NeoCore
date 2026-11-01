#pragma once

// 정적 프런트엔드가 입력 저장형을 확정한다. 실행 중에는 타입을 다시 고르지 않고
// 알려진 페이로드를 읽으며, 결과 슬롯의 기존 참조와 배열 범위 검사는 유지한다.
template<int Dimension>
static NEOS_FORCEINLINE void ReadTypedVector(const VarInfo* value, float* components)
{
    if (Dimension < 4)
    {
        components[0] = value->_vecX;
        components[1] = value->_vecYZ[0];
        components[2] = Dimension == 3 ? value->_vecYZ[1] : 0.0f;
        components[3] = 0.0f;
    }
    else
        std::memcpy(components, value->_vec->v, sizeof(float) * 4);
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
        // 결과 해제로 배열이 사라지더라도 Vec4 저장소는 결과가 계속 보유한다.
        VarInfo value = array->VectorValues()[index];
        if (Element == NeoArrayElementType::Vector4) ++value._vec->_refCount;
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
    else if (Element == NeoArrayElementType::Vector4)
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
