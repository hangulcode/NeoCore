#pragma once

NEOS_FORCEINLINE void CNeoVMWorker::StoreNumberInt(ValueRef r, NS_INT v)
{
	if (r.IsAllocType()) ReleaseBeforeNumber(r);
	r.SetType(VAR_INT);
	r->_int = v;
}
NEOS_FORCEINLINE void CNeoVMWorker::StoreNumberFloat(ValueRef r, NS_FLOAT v)
{
	if (r.IsAllocType()) ReleaseBeforeNumber(r);
	r.SetType(VAR_FLOAT);
	r->_float = v;
}
NEOS_FORCEINLINE void CNeoVMWorker::StoreNumberBool(ValueRef r, bool v)
{
	if (r.IsAllocType()) ReleaseBeforeNumber(r);
	r.SetType(VAR_BOOL);
	r->_bl = v;
}
// 산술 결과가 참조형 값을 덮어쓰는 드문 경우만 여기로 온다. 핫 경로에서 해제 코드를 떼어 두는 것이 목적이다.
NEOS_NOINLINE void CNeoVMWorker::ReleaseBeforeNumber(ValueRef r)
{
	Var_ReleaseAlloc(r);
}

NEOS_FORCEINLINE bool CNeoVMWorker::EnsureStackRange(int base, int lastOffset)
{
	// SetVarStackBase 가 유지하는 캐시. 옛 코드는 여기서 m_pVarStack_Base->size() 를
	// 읽어 의존 로드가 두 단이었다 — 호출 빈도가 높아 그 자체로 비용이었다.
	const int stackSize = m_iVarStackSize;
	if (base > stackSize - 1 - lastOffset)
	{
		SetError(RTE_CALL_STACK_OVERFLOW);
		return false;
	}
	return true;
}


// 즉값은 숫자 슬롯에만 쓴다(로더가 검증). payload만 쓴다.
NEOS_FORCEINLINE void CNeoVMWorker::MoveI(VarInfo* v1, int v)
{
	v1->_int = v;
}

NEOS_FORCEINLINE void CNeoVMWorker::MoveF(VarInfo* v1, int bits)
{
	float immediate;
	std::memcpy(&immediate, &bits, sizeof(immediate));
	v1->_float = static_cast<NS_FLOAT>(immediate);
}

NEOS_NOINLINE void ArrayReadValue(CNeoVMWorker* worker, ArrayInfo* array, int index, ValueRef out)
{
	switch (array->_elementType)
	{
	case NeoArrayElementType::Bool:  worker->Var_SetBool(out, array->GetBool(index)); break;
	case NeoArrayElementType::Int:   worker->Var_SetInt(out, array->IntData()[index]); break;
	case NeoArrayElementType::Float: worker->Var_SetFloat(out, array->FloatData()[index]); break;
	case NeoArrayElementType::Vector2:
	case NeoArrayElementType::Vector3:
	case NeoArrayElementType::Vector4:
		array->ReadVector(index, out);
		break;
	case NeoArrayElementType::Int32: worker->Var_SetInt(out, array->Int32Data()[index]); break;
	case NeoArrayElementType::Float32: worker->Var_SetFloat(out, array->Float32Data()[index]); break;
	}
}

NEOS_FORCEINLINE bool ArrayWriteValue(ArrayInfo* array, int index, ValueRef value)
{
	return array->AssignValue(index, value);
}


// 디스패치 루프에 인라인되는 것은 list[int] / array[int] / map[string] 세 경로뿐이다.
// 나머지(프로퍼티 맵, 벡터 성분 쓰기, 문자열 인덱서, 에러 포맷)는 CltInsertRare 로 밀어낸다 —
// RunInternal이 커지지 않도록 드문 경로를 루프 밖에 둔다.
NEOS_FORCEINLINE void CNeoVMWorker::CltInsert(ValueRef pClt, ValueRef pKey, ValueRef pValue, const SVMOperation* write)
{
	const VAR_TYPE t = pClt.GetType();
	if (t == VAR_LIST)
	{
		if (pKey.GetType() == VAR_INT)
		{
			ListInfo* lst = pClt->_lst;
			NS_UINT idx = (NS_UINT)pKey->_int;
			if (idx < (unsigned)lst->_itemCount)
			{
				if (pValue.IsContainerType())
					lst->MarkContainerChild();
				Move(&lst->_Bucket[idx], pValue);
				return;
			}
		}
	}
	else if (t == VAR_MAP)
	{
		if (pKey.GetType() == VAR_NONE) { SetError("map key cannot be null"); return; }
		MapInfo* tbl = pClt->_tbl;
		if (pValue.GetType() != VAR_NONE)
		{
			// 기존 키 갱신은 노드를 찾아 값만 옮기면 된다 — 구조가 안 바뀌므로
			// MapInfo::Insert 와 결과가 같다(_mutationVersion 은 신규 노드에서만 증가).
			// 새 키는 할당/ReMap 이 필요해 그대로 아웃오브라인 Insert 로 보낸다.
			const VAR_TYPE kt = pKey.GetType();
			if (kt == VAR_STRING || kt == VAR_INT)
			{
				ValueRef pFind = (kt == VAR_STRING) ? tbl->FindString(pKey->_str)
													: tbl->FindInt(pKey->_int);
				if (pFind)
				{
					if (pValue.IsContainerType())
						tbl->MarkContainerChild();
					Move(pFind, pValue);
					return;
				}
			}
			tbl->Insert(pKey, pValue);
			return;
		}
	}
	else if (pClt.IsVector())
	{
		// v[i] = x. 인라인 성분은 직접 쓰고 공유 중인 4성분 저장소만 복제한다.
		assert(pKey.GetType() == VAR_INT && pValue.IsNumber());
		{
			NS_UINT idx = (NS_UINT)pKey->_int;
			if (idx < (unsigned)pClt.VectorComponentCount())
			{
				WritableVectorComponent(pClt, static_cast<int>(idx)) = NeoNarrowFloat32(pValue.GetFloatNumber());
				if (write && (write->argFlag & NEOS_OP_REF_WRITE)) CommitRefVector(pClt);
				return;
			}
		}
	}
	else if (t == VAR_ARRAY)
	{
		assert(pKey.GetType() == VAR_INT);
		ArrayInfo* array = pClt->_array;
		const NS_INT index = pKey->_int;
		if (array->IsValidIndex(index) == false)
		{
			SetError(RTE_ARRAY_INDEX_WRITE);
			return;
		}
		if (ArrayWriteValue(array, static_cast<int>(index), pValue))
			return;
		SetArrayAssignError(array, pValue);
		return;
	}
	CltInsertRare(pClt, pKey, pValue);
}

// The loader validates the adjacent WREF. Recover its owner in the resumed
// frame, without retaining a pointer or pending state across native callbacks.
NEOS_NOINLINE void CNeoVMWorker::CommitRefVector(ValueRef value)
{
	const SVMOperation& owner = _pCodeCurrent[-2];
	CltInsert(GetVarPtr1(owner), GetVarPtr2(owner), value);
}

NEOS_NOINLINE void CNeoVMWorker::CltInsertRare(ValueRef pClt, ValueRef pKey, ValueRef pValue)
{
	switch (pClt.GetType())
	{
	case VAR_VEC:
	case VAR_VEC3:
	case VAR_VEC4:
		// 가변 성분 쓰기: 인라인 값은 직접 수정하고 4성분 벡터만 COW로 분리한다.
		assert(pKey.GetType() == VAR_INT && pValue.IsNumber());
		{
			NS_UINT idx = (NS_UINT)pKey->_int;
			if (idx < (unsigned)pClt.VectorComponentCount())
			{
				WritableVectorComponent(pClt, static_cast<int>(idx)) = NeoNarrowFloat32(pValue.GetFloatNumber());
				return;
			}
		}
		SetError(RTE_VECTOR_INDEX_WRITE);
		return;
	case VAR_FP_NATIVE:
	{
		assert(pKey.GetType() == VAR_STRING);
		FunctionPropertyInfo* fp = pClt->_fpNative;
		PropertyNative(fp->_fun, fp->_pUserData, pKey->_str, pValue, false);
		return;
	}
	case VAR_ARRAY:
	{
		assert(pKey.GetType() == VAR_INT);
		ArrayInfo* array = pClt->_array;
		const NS_INT index = pKey->_int;
		if (array->IsValidIndex(index) == false)
		{
			SetError(RTE_ARRAY_INDEX_WRITE);
			return;
		}
		if (ArrayWriteValue(array, static_cast<int>(index), pValue))
			return;
		SetArrayAssignError(array, pValue);
		return;
	}
	case VAR_MAP:
	{
		if (pKey.GetType() == VAR_NONE) { SetError("map key cannot be null"); return; }
		if (pValue.GetType() == VAR_NONE)
		{
			TableRemove(pClt, pKey);
			return;
		}
		pClt->_tbl->Insert(pKey, pValue);
		return;
	}
	case VAR_LIST:
	{
		ListInfo* lst = pClt->_lst;
		if (pKey.GetType() == VAR_INT)
		{
			// fast path: CltRead 와 동일한 이유로 버킷에 직접 쓴다.
			NS_UINT idx = (NS_UINT)pKey->_int;
			if (idx < (unsigned)lst->_itemCount)
			{
				if (pValue.IsContainerType())
					lst->MarkContainerChild();
				Move(&lst->_Bucket[idx], pValue);
				return;
			}
			break;   // 범위 밖
		}
		if (lst->_pIndexer != nullptr && pKey.GetType() == VAR_STRING)
		{
			int idx;
			if(lst->_pIndexer->TryGetValue(pKey->_str, &idx))
			{
				if(lst->SetValue(idx, pValue))
					return;
			}
		}

		SetErrorFormat(RTE_INDEX_WRITE, GetDataType(pClt.GetType()).c_str());
		return;
	}
	default:
		break;
	}
	SetErrorFormat(RTE_INDEX_WRITE, GetDataType(pClt.GetType()).c_str());
}
NEOS_FORCEINLINE void CNeoVMWorker::CltInsert(ValueRef pClt, int key, ValueRef v)
{
	switch (pClt.GetType())
	{
	case VAR_MAP:
		pClt->_tbl->Insert(key, v);
		return;
	case VAR_LIST:
		pClt->_lst->SetValue(NeoNarrowInt(key), v);
		return;
	default:
		break;
	}
	SetErrorFormat(RTE_INDEX_WRITE, GetDataType(pClt.GetType()).c_str());
}

NEOS_FORCEINLINE void CNeoVMWorker::CltInsert(ValueRef pClt, ValueRef pKey, NS_INT v)
{
	switch (pClt.GetType())
	{
	case VAR_MAP:
		if (pKey.GetType() == VAR_NONE) { SetError("map key cannot be null"); return; }
		pClt->_tbl->Insert(pKey, v);
		return;
	case VAR_LIST:
		pClt->_lst->SetValue(NeoNarrowInt(pKey->_int), v);
		return;
	default:
		break;
	}
	SetErrorFormat(RTE_INDEX_WRITE, GetDataType(pClt.GetType()).c_str());
}
NEOS_FORCEINLINE void CNeoVMWorker::CltInsert(ValueRef pClt, NS_INT key, NS_INT v)
{
	switch (pClt.GetType())
	{
	case VAR_MAP:
		pClt->_tbl->Insert(key, v);
		return;
	case VAR_LIST:
		pClt->_lst->SetValue(NeoNarrowInt(key), v);
		return;
	default:
		break;
	}
	SetErrorFormat(RTE_INDEX_WRITE, GetDataType(pClt.GetType()).c_str());
}
NEOS_FORCEINLINE ValueRef CNeoVMWorker::GetTableItem(ValueRef pClt, ValueRef pKey)
{
	if (pClt.GetType() == VAR_MAP)
	{
		return pClt->_tbl->Find(pKey);
	}
	else if (pClt.GetType() == VAR_LIST)
	{
		if (pKey.GetType() == VAR_INT)
		{
			return pClt->_lst->GetValue(NeoNarrowInt(pKey->_int));
		}
		else
		{
			if (pClt->_lst->_pIndexer != nullptr && pKey.GetType() == VAR_STRING)
			{
				int idx;
				if (pClt->_lst->_pIndexer->TryGetValue(pKey->_str, &idx))
					return pClt->_lst->GetValue(idx);
			}
		}
	}

	SetErrorFormat(RTE_INDEX_READ, GetDataType(pClt.GetType()).c_str());
	return nullptr;
}
// CltInsert 와 같은 이유로 list[int] / array[int] / map[string]만 인라인하고 나머지는 분리한다.
NEOS_FORCEINLINE void CNeoVMWorker::CltRead(ValueRef pClt, ValueRef pKey, ValueRef pValue)
{
	const VAR_TYPE t = pClt.GetType();
	if (t == VAR_LIST)
	{
		if (pKey.GetType() == VAR_INT)
		{
			ListInfo* lst = pClt->_lst;
			NS_UINT idx = (NS_UINT)pKey->_int;
			if (idx < (unsigned)lst->_itemCount)
			{
				Move(pValue, &lst->_Bucket[idx]);
				return;
			}
		}
	}
	else if (t == VAR_MAP)
	{
		MapInfo* tbl = pClt->_tbl;
		const VAR_TYPE kt = pKey.GetType();
		// 문자열/정수 두 키 타입만 인라인한다. 꼬리(Move/Release)는 공유해서 본문을 작게 유지.
		if (kt == VAR_STRING || kt == VAR_INT)
		{
			ValueRef pFind = (kt == VAR_STRING) ? tbl->FindString(pKey->_str)
												: tbl->FindInt(pKey->_int);
			if (pFind)
				Move(pValue, pFind);
			else
				Var_Release(pValue);
			return;
		}
	}
	else if (pClt.IsVector())
	{
		// 벡터 첨자 타입은 정적 계약이고, 실제 범위는 실행 중 검사한다.
		assert(pKey.GetType() == VAR_INT);
		{
			NS_UINT idx = (NS_UINT)pKey->_int;
			if (idx < (unsigned)pClt.VectorComponentCount())
			{
				Var_SetFloat(pValue, pClt.VectorComponent(static_cast<int>(idx)));
				return;
			}
		}
	}
	else if (t == VAR_ARRAY)
	{
		assert(pKey.GetType() == VAR_INT);
		ArrayInfo* array = pClt->_array;
		const NS_INT index = pKey->_int;
		if (array->IsValidIndex(index) == false)
		{
			SetError(RTE_ARRAY_INDEX_READ);
			return;
		}
		// 숫자는 목적지의 이전 타입과 무관하게 직접 저장하고, 참조 해제만 드문 경로로 보낸다.
		// 원소 값을 먼저 읽으므로 목적지가 배열 자체를 덮어써도 해제된 저장소를 읽지 않는다.
		if (array->_elementType == NeoArrayElementType::Int)
			StoreNumberInt(pValue, array->IntData()[index]);
		else if (array->_elementType == NeoArrayElementType::Float)
			StoreNumberFloat(pValue, array->FloatData()[index]);
		else
			ArrayReadValue(this, array, static_cast<int>(index), pValue);
		return;
	}
	CltReadRare(pClt, pKey, pValue);
}

NEOS_NOINLINE void CNeoVMWorker::CltReadRare(ValueRef pClt, ValueRef pKey, ValueRef pValue)
{
	ValueRef pFind;
	switch (pClt.GetType())
	{
	case VAR_VEC:
	case VAR_VEC3:
	case VAR_VEC4:
		assert(pKey.GetType() == VAR_INT);
		{
			NS_UINT idx = (NS_UINT)pKey->_int;
			if (idx < (unsigned)pClt.VectorComponentCount())
			{
				Var_SetFloat(pValue, pClt.VectorComponent(static_cast<int>(idx)));
				return;
			}
		}
		SetError(RTE_VECTOR_INDEX_READ);
		return;
	case VAR_FP_NATIVE:
		{
			assert(pKey.GetType() == VAR_STRING);
			FunctionPropertyInfo* fp = pClt->_fpNative;
			PropertyNative(fp->_fun, fp->_pUserData, pKey->_str, pValue, true);
			return;
		}
	case VAR_ARRAY:
	{
		assert(pKey.GetType() == VAR_INT);
		ArrayInfo* array = pClt->_array;
		const NS_INT index = pKey->_int;
		if (array->IsValidIndex(index) == false)
		{
			SetError(RTE_ARRAY_INDEX_READ);
			return;
		}
		ArrayReadValue(this, array, static_cast<int>(index), pValue);
		return;
	}
	case VAR_MAP:
		{
			MapInfo* tbl = pClt->_tbl;
			// 문자열 키는 타입 디스패치를 생략하는 전용 경로로 직행
			if (pKey.GetType() == VAR_STRING)
				pFind = tbl->FindString(pKey->_str);
			else
				pFind = GetTableItem(pClt, pKey);
			if (pFind)
				Move(pValue, pFind);
			else
				Var_Release(pValue);
			return;
		}
	case VAR_LIST:
	{
		ListInfo* lst = pClt->_lst;
		if (pKey.GetType() == VAR_INT)
		{
			// fast path: 함수 호출(ListInfo::GetValue)과 VM 간접참조 없이 버킷을 직접 읽는다.
			// 음수는 unsigned 로 접히므로 비교 한 번으로 범위 검사가 끝난다.
			NS_UINT idx = (NS_UINT)pKey->_int;
			if (idx < (unsigned)lst->_itemCount)
			{
				Move(pValue, &lst->_Bucket[idx]);
				return;
			}
			break;   // 범위 밖
		}
		if (lst->_pIndexer != nullptr && pKey.GetType() == VAR_STRING)
		{
			int idx;
			if (lst->_pIndexer->TryGetValue(pKey->_str, &idx))
			{
				lst->GetValue(idx, pValue);
				return;
			}
		}
		SetErrorFormat(RTE_INDEX_READ, GetDataType(pClt.GetType()).c_str());
		return;
	}
	default:
		break;
	}
	SetErrorFormat(RTE_INDEX_READ, GetDataType(pClt.GetType()).c_str());
}

NEOS_FORCEINLINE void CNeoVMWorker::TableRemove(ValueRef pTable, ValueRef pKey)
{
	if (pTable.GetType() != VAR_MAP)
	{
		SetErrorFormat(RTE_INDEX_WRITE, GetDataType(pTable.GetType()).c_str());
		return;
	}

	pTable->_tbl->Remove(pKey);
}

bool CNeoVMWorker::ResetVarType(ValueRef p, VAR_TYPE type, int capa)
{
	if(type == VAR_MAP)
	{
		Var_SetTable(p, GetVM()->TableAlloc(capa));
		return true;
	}
	else if (type == VAR_FP_NATIVE)
	{
		if (p.IsAllocType())
			Var_Release(p);
		FunctionPropertyInfo* fp = GetVM()->FunctionPropertyAlloc();
		p.SetType(VAR_FP_NATIVE);
		p->_fpNative = fp;
		++fp->_refCount;
		return true;
	}
	else if (type == VAR_LIST)
	{
		Var_SetList(p, GetVM()->ListAlloc(capa));
		return true;
	}
	else if (type == VAR_NONE)
	{
		Var_Release(p);
		return true;
	}
	return false;
}


NEOS_FORCEINLINE void CNeoVMWorker::Swap(ValueRef v1, ValueRef v2)
{
	ValueRef p = v1;
	v1 = v2;
	v2 = p;
}



NEOS_FORCEINLINE bool CNeoVMWorker::CheckIntegerDivision(NS_INT dividend, NS_INT divisor)
{
	if (divisor == 0)
	{
		SetError(RTE_INTEGER_DIV_ZERO);
		return false;
	}
	return divisor != -1 || CheckIntegerDivisionOverflow(dividend);
}

// 드문 오버플로 검사를 분리하여 최소 정수 상수가 전체 디스패치의 레지스터를 점유하지 않게 한다.
NEOS_NOINLINE bool CNeoVMWorker::CheckIntegerDivisionOverflow(NS_INT dividend)
{
	if (dividend == NS_INT_MIN)
	{
		SetError(RTE_INTEGER_DIV_OVERFLOW);
		return false;
	}
	return true;
}

NEOS_FORCEINLINE bool CNeoVMWorker::CheckShiftCount(NS_INT count)
{
	if (static_cast<NS_UINT>(count) < 64) return true;
	SetError(RTE_INTEGER_SHIFT_RANGE);
	return false;
}

NEOS_FORCEINLINE bool CNeoVMWorker::CompareEQ(ValueRef v1, ValueRef v2)
{
	const VAR_TYPE t1 = v1.GetType();
	const VAR_TYPE t2 = v2.GetType();
	if (t1 == VAR_INT)
	{
		if (t2 == VAR_INT)
			return v1->_int == v2->_int;
		if (t2 == VAR_FLOAT)
			return v1->_int == v2->_float;
	}
	else if (t1 == VAR_FLOAT)
	{
		if (t2 == VAR_INT)
			return v1->_float == v2->_int;
		if (t2 == VAR_FLOAT)
			return v1->_float == v2->_float;
	}
	return CompareEQRare(v1, v2);
}

NEOS_NOINLINE bool CNeoVMWorker::CompareEQRare(ValueRef v1, ValueRef v2)
{
	switch (v1.GetType())
	{
	case VAR_BOOL:
		if (v2.GetType() == VAR_BOOL)
			return v1->_bl == v2->_bl;
		break;
	case VAR_NONE:
		if (v2.GetType() == VAR_NONE)
			return true;
		break;
	case VAR_VEC:
		return VectorValuesEqual(v1, v2);
	case VAR_STRING:
		if (v2.GetType() == VAR_STRING)
			return v1->_str->_str == v2->_str->_str;
		break;
	case VAR_VEC3:
	case VAR_VEC4:
		return VectorValuesEqual(v1, v2);
	default:
		return ReferenceEquals(v1, v2);
	}
	return false;
}

NEOS_FORCEINLINE bool CNeoVMWorker::CompareGR(ValueRef v1, ValueRef v2, bool reverseDiagnostic)
{
	const VAR_TYPE t1 = v1.GetType();
	const VAR_TYPE t2 = v2.GetType();
	if (t1 == VAR_INT)
	{
		if (t2 == VAR_INT)
			return v1->_int > v2->_int;
		if (t2 == VAR_FLOAT)
			return v1->_int > v2->_float;
	}
	else if (t1 == VAR_FLOAT)
	{
		if (t2 == VAR_INT)
			return v1->_float > v2->_int;
		if (t2 == VAR_FLOAT)
			return v1->_float > v2->_float;
	}
	return CompareGRRare(v1, v2, reverseDiagnostic);
}

NEOS_NOINLINE bool CNeoVMWorker::CompareGRRare(ValueRef v1, ValueRef v2, bool reverseDiagnostic)
{
	if (v1.GetType() == VAR_STRING && v2.GetType() == VAR_STRING)
		return v1->_str->_str > v2->_str->_str;
	SetErrorOperator(reverseDiagnostic ? "<" : ">", reverseDiagnostic ? v2 : v1, reverseDiagnostic ? v1 : v2);
	return false;
}

NEOS_FORCEINLINE bool CNeoVMWorker::CompareGE(ValueRef v1, ValueRef v2, bool reverseDiagnostic)
{
	const VAR_TYPE t1 = v1.GetType();
	const VAR_TYPE t2 = v2.GetType();
	if (t1 == VAR_INT)
	{
		if (t2 == VAR_INT)
			return v1->_int >= v2->_int;
		if (t2 == VAR_FLOAT)
			return v1->_int >= v2->_float;
	}
	else if (t1 == VAR_FLOAT)
	{
		if (t2 == VAR_INT)
			return v1->_float >= v2->_int;
		if (t2 == VAR_FLOAT)
			return v1->_float >= v2->_float;
	}
	return CompareGERare(v1, v2, reverseDiagnostic);
}

NEOS_NOINLINE bool CNeoVMWorker::CompareGERare(ValueRef v1, ValueRef v2, bool reverseDiagnostic)
{
	if (v1.GetType() == VAR_STRING && v2.GetType() == VAR_STRING)
		return v1->_str->_str >= v2->_str->_str;
	SetErrorOperator(reverseDiagnostic ? "<=" : ">=", reverseDiagnostic ? v2 : v1, reverseDiagnostic ? v1 : v2);
	return false;
}

NEOS_FORCEINLINE bool CNeoVMWorker::For(VarInfo* slots)
{
    // 범위 반복의 네 슬롯은 컴파일러가 int로 확정한다. 핫패스에서는 페이로드만 접근한다.
    slots[0]._int = slots[1]._int;
    const NS_INT step = slots[3]._int;
    if (step == 1)
    {
        if (slots[0]._int >= slots[2]._int) return false;
        // 현재 값이 끝값보다 작으므로 증가가 정수 범위를 넘지 않는다.
        ++slots[1]._int;
        return true;
    }
    return ForRare(slots, step);
}
// 드문 경로: step<0(역방향) 과 step==0(에러). 인라인하지 않아 For 의 hot path 를 작게 유지한다.
NEOS_NOINLINE bool CNeoVMWorker::ForRare(VarInfo* pCur, NS_INT step)
{
	if (step == 0)
	{
		SetErrorFormat(RTE_FOR_STEP_ZERO);   // step==0 은 영원히 끝나지 않는다
		return false;
	}
	if (step > 0)
	{
		if (pCur->_int < pCur[2]._int)
		{
			const NS_INT next = pCur[1]._int > NS_INT_MAX - step ? NS_INT_MAX : pCur[1]._int + step;
			pCur[1]._int = next >= pCur[2]._int ? pCur[2]._int : next;
			return true;
		}
		return false;
	}
	if (pCur->_int > pCur[2]._int)           // 역방향은 비교 방향이 반대
	{
		const NS_INT next = pCur[1]._int < NS_INT_MIN - step ? NS_INT_MIN : pCur[1]._int + step;
		pCur[1]._int = next <= pCur[2]._int ? pCur[2]._int : next;
		return true;
	}
	return false;
}
// foreach 의 드문 컨테이너. string/set 순회는 스크립트에서 거의 안 쓰이는데 본문이 커서
// 디스패치 루프만 키운다 — CltInsert/PTRCALL 과 같은 fast/rare 분리다.
// 반복자·버전 슬롯은 소유권이 없는 payload다. 컴파일러가 key 뒤에 value/iterator/version을 연속 배치한다.
NEOS_NOINLINE bool CNeoVMWorker::ForEachRare(ValueRef pClt, ValueRef pKey, bool bTwoVar)
{
	VarInfo* pIterator = pKey.Data() + 2;
	VarInfo* pMutationVersion = pKey.Data() + 3;

	switch (pClt.GetType())
	{
	case VAR_STRING:
	{
		if (bTwoVar)
		{
			pIterator->_int = 0;
			SetError(RTE_FOREACH_STRING_TWOVAR);
			return false;
		}
		std::string* str = &pClt->_str->_str;
		if (pMutationVersion->_int == -1)
		{
			if (0 < (int)str->length())
			{
				pIterator->_int = 0;
				pIterator->_it._iStringOffset = 0;
				pMutationVersion->_int = 0;
			}
			else
				return false;
		}

		if (pIterator->_it._iStringOffset < (int)str->length())
		{
			Var_SetStringA(pKey, utf_string::UTF8_ONE(*str, pIterator->_it._iStringOffset));
			return true;
		}
		else
		{
			pIterator->_int = 0;
			return false;
		}
		break;
	}
	case VAR_SET:
	{
		// set 도 값만 순회한다. foreach(var k, v in set) 는 미지원.
		if (bTwoVar)
		{
			pIterator->_int = 0;
			SetError(RTE_FOREACH_SET_TWOVAR);
			return false;
		}
		SetInfo* set = pClt->_set;
		if (pMutationVersion->_int == -1)
		{
			if (0 < set->GetCount())
			{
				pIterator->_it = set->FirstNode();
				pMutationVersion->_int = static_cast<NS_INT>(set->_mutationVersion);
			}
			else
				return false;
		}
		else
		{
			if ((u32)pMutationVersion->_int != set->_mutationVersion)
			{
				pIterator->_int = 0;
				SetError(RTE_FOREACH_MODIFIED);
				return false;
			}
			set->NextNode(pIterator->_it);
		}

		SetNode* n = pIterator->_it._pSetNode;
		if (n)
		{
			Move(pKey, &n->data->key);
			return true;
		}
		else
		{
			pIterator->_int = 0;
			return false;
		}
		break;
	}
	default:
		break;
	}
	SetErrorFormat(RTE_FOREACH_UNSUPPORTED, GetDataType(pClt.GetType()).c_str());
	return false;
}

NEOS_FORCEINLINE bool CNeoVMWorker::ForEach(ValueRef pClt, ValueRef pKey, ValueRef pValue, bool bTwoVar)
{
	VarInfo* pIterator = pKey.Data() + 2;
	// 컴파일러가 foreach마다 key/value/iterator/version 슬롯을 연속 배치한다.
	VarInfo* pMutationVersion = pKey.Data() + 3;

	switch (pClt.GetType())
	{
	case VAR_ARRAY:
	{
		// array도 list처럼 값 하나만 순회한다. 원소 대입은 iterator를 무효화하지 않지만,
		// append/resize처럼 길이나 저장 위치를 바꾸는 연산은 다음 단계에서 거부한다.
		if (bTwoVar)
		{
			pIterator->_int = 0;
			SetError(RTE_FOREACH_ARRAY_TWOVAR);
			return false;
		}
		ArrayInfo* array = pClt->_array;
		if (pMutationVersion->_int == -1)
		{
			if (array->_count > 0)
			{
				pIterator->_int = 0;
				pIterator->_it._iListOffset = 0;
				pMutationVersion->_int = static_cast<NS_INT>(array->_mutationVersion);
			}
			else
				return false;
		}
		else
		{
			if ((u32)pMutationVersion->_int != array->_mutationVersion)
			{
				pIterator->_int = 0;
				SetError(RTE_FOREACH_MODIFIED);
				return false;
			}
			++pIterator->_it._iListOffset;
		}

		if (pIterator->_it._iListOffset < array->_count)
		{
			ArrayReadValue(this, array, pIterator->_it._iListOffset, pKey);
			return true;
		}
		pIterator->_int = 0;
		return false;
	}
	case VAR_MAP:
	{
		MapInfo* tbl = pClt->_tbl;
		if (pMutationVersion->_int == -1)
		{
			if (0 < tbl->GetCount())
			{
				pIterator->_it = tbl->FirstNode();
				pMutationVersion->_int = static_cast<NS_INT>(tbl->_mutationVersion);
			}
			else
				return false;
		}
		else
		{
			if ((u32)pMutationVersion->_int != tbl->_mutationVersion)
			{
				pIterator->_int = 0;
				SetError(RTE_FOREACH_MODIFIED);
				return false;
			}
			tbl->NextNode(pIterator->_it);
		}

		MapNode* n = pIterator->_it._pTableNode;
		if (n)
		{
			Move(pKey, &n->data->key);
			Move(pValue, &n->data->value);
			return true;
		}
		else
		{
			pIterator->_int = 0;
			return false;
		}
		break;
	}
	case VAR_LIST:
	{
		// list 는 값만 순회한다. foreach(var k, v in list) 는 미지원.
		if (bTwoVar)
		{
			pIterator->_int = 0;
			SetError(RTE_FOREACH_LIST_TWOVAR);
			return false;
		}
		ListInfo* lst = pClt->_lst;
		if (pMutationVersion->_int == -1)
		{
			if (0 < lst->GetCount())
			{
				pIterator->_int = 0;
				pIterator->_it._iListOffset = 0;
				pMutationVersion->_int = static_cast<NS_INT>(lst->_mutationVersion);
			}
			else
				return false;
		}
		else
		{
			if ((u32)pMutationVersion->_int != lst->_mutationVersion)
			{
				pIterator->_int = 0;
				SetError(RTE_FOREACH_MODIFIED);
				return false;
			}
			++pIterator->_it._iListOffset;
		}

		if (pIterator->_it._iListOffset < lst->GetCount())
		{
			lst->GetValue(pIterator->_it._iListOffset, pKey);
			//Move(pValue, &n->value);
			return true;
		}
		else
		{
			pIterator->_int = 0;
			return false;
		}
		break;
	}
	default:
		break;
	}
	// string/set 과 미지원 타입은 콜드 경로로.
	return ForEachRare(pClt, pKey, bTwoVar);
}

