#pragma once

// 동적 백엔드 회귀 전용 산술이다. NeoCore의 정적 실행 라이브러리에는 포함하지 않는다.

NEOS_FORCEINLINE void CNeoVMWorker::And(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	switch (v1->GetType())
	{
	case VAR_INT:
		if (v2->GetType() == VAR_INT)
		{
			Var_SetInt(r, v1->_int & v2->_int);
			return;
		}
		else if (v2->GetType() == VAR_BOOL)
		{
			Var_SetInt(r, v1->_int & (int)v2->_bl);
			return;
		}
		break;
	case VAR_FLOAT:
		break;
	case VAR_BOOL:
		if (v2->GetType() == VAR_INT)
		{
			Var_SetInt(r, (int)v1->_bl & v2->_int);
			return;
		}
		else if (v2->GetType() == VAR_BOOL)
		{
			Var_SetBool(r, v1->_bl & v2->_bl);
			return;
		}
		break;
	case VAR_STRING:
		break;
	case VAR_MAP:
		break;
	case VAR_LIST:
		break;
	case VAR_SET:
		if (neo_DCalllibs::Set_And(this, r, v1, v2))
			return;
		break;
	default:
		break;
	}
	SetErrorOperator("&", v1, v2);
}
NEOS_FORCEINLINE void CNeoVMWorker::Or(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	switch (v1->GetType())
	{
	case VAR_INT:
		if (v2->GetType() == VAR_INT)
		{
			Var_SetInt(r, v1->_int | v2->_int);
			return;
		}
		else if (v2->GetType() == VAR_BOOL)
		{
			Var_SetInt(r, v1->_int | (int)v2->_bl);
			return;
		}
		break;
	case VAR_FLOAT:
		break;
	case VAR_BOOL:
		if (v2->GetType() == VAR_INT)
		{
			Var_SetInt(r, (int)v1->_bl | v2->_int);
			return;
		}
		else if (v2->GetType() == VAR_BOOL)
		{
			Var_SetBool(r, v1->_bl | v2->_bl);
			return;
		}
		break;
	case VAR_STRING:
		break;
	case VAR_MAP:
		break;
	case VAR_LIST:
		break;
	case VAR_SET:
		if (neo_DCalllibs::Set_Or(this, r, v1, v2))
			return;
		break;
	default:
		break;
	}
	SetErrorOperator("|", v1, v2);
}
NEOS_FORCEINLINE float VectorArithmeticComponent(float a, float b, int op)
{
	return op == 0 ? a + b : op == 1 ? a - b : op == 2 ? a * b : a / b;
}

// 인라인(2·3성분) 벡터 산술. v1 이 VAR_VEC 일 때만 부른다. Add3/Sub3/Mul3/Div3 의 fast path 와 VecArith 가
// 공유하며, op 는 호출 지점의 상수라 인라인 뒤 성분 연산의 분기가 접힌다. 4성분은 VecArith 에 남는다.
// fast path 에 두는 근거: v = v + d 연쇄·누적에서 NOINLINE *Rare 호출을 없애는 이득이 측정상 컸다.
NEOS_FORCEINLINE bool CNeoVMWorker::VecArithInline(VarInfo* r, VarInfo* v1, VarInfo* v2, int op)
{
	const int n = v1->_vecCount;
	float bx, by, bz;
	if (v2->GetType() == VAR_VEC && v2->_vecCount == n)
	{
		bx = v2->_vecX; by = v2->_vecYZ[0]; bz = v2->_vecYZ[1];
	}
	else if ((op == 2 || op == 3) && v2->IsNumber())
		bx = by = bz = NeoNarrowFloat32(v2->GetFloatNumber());
	else
		return false;
	const float x = VectorArithmeticComponent(v1->_vecX, bx, op);
	const float y = VectorArithmeticComponent(v1->_vecYZ[0], by, op);
	const float z = n == 3 ? VectorArithmeticComponent(v1->_vecYZ[1], bz, op) : 0.0f;
	// 결과를 먼저 계산하므로 r이 어느 입력과 같아도 안전하게 덮어쓸 수 있다.
	if (r->IsAllocType()) Var_Release(r);
	r->SetInlineVec(n, x, y, z);
	return true;
}

NEOS_FORCEINLINE bool CNeoVMWorker::VecArith(VarInfo* r, VarInfo* v1, VarInfo* v2, int op)
{
	// 쿼터니언은 별도 타입이 아니다 — 4성분 벡터의 성분별 연산으로 동작하며,
	// 회전 의미를 원하면 사용자가 math.RotateVectorByQuat 를 쓴다.
	// 저장 방식은 연산 진입 시 한 번만 구분한다. 성분마다 포인터/인라인을 분기하지 않는다.
	if (v1->GetType() == VAR_VEC)
		return VecArithInline(r, v1, v2, op);
	float tmp[4];
	if (v2->GetType() == VAR_VEC4)
	{
		for (int i = 0; i < 4; ++i)
			tmp[i] = VectorArithmeticComponent(v1->_vec->v[i], v2->_vec->v[i], op);
	}
	else if ((op == 2 || op == 3) && v2->IsNumber())
	{
		const float s = NeoNarrowFloat32(v2->GetFloatNumber());
		for (int i = 0; i < 4; ++i)
			tmp[i] = VectorArithmeticComponent(v1->_vec->v[i], s, op);
	}
	else
		return false;
	VecInfo* p = Vec4StoreFor(r);
	std::memcpy(p->v, tmp, sizeof(tmp));
	return true;
}
static_assert(VAR_INT == 0 && VAR_FLOAT == 1, "arithmetic fast paths assume VAR_INT == 0 and VAR_FLOAT == 1");

// 정수·실수 조합을 fall-through 직선 코드로 둔다. VAR_INT=0, VAR_FLOAT=1 이라 (t1|t2)==0 이면 정수끼리,
// (t1|t2)<=1 이면 숫자끼리다. 문자열/벡터/메타/에러는 Add3Rare 로 분리 — 디스패치 루프에 인라인되므로 본문 크기가 성능이다.
NEOS_FORCEINLINE void CNeoVMWorker::Add3(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	const unsigned t1 = v1->GetType(), t2 = v2->GetType();
    // 실수끼리는 정수 변환 분기를 거치지 않고 계산한다.
    if (t1 == VAR_FLOAT && t2 == VAR_FLOAT)
    {
        StoreNumberFloat(r, v1->_float + v2->_float);
        return;
    }
	if ((t1 | t2) == VAR_INT)
	{
		StoreNumberInt(r, NeoIntAdd(v1->_int, v2->_int));
		return;
	}
	if ((t1 | t2) <= VAR_FLOAT)
	{
		const NS_FLOAT a = t1 == VAR_INT ? (NS_FLOAT)v1->_int : v1->_float;
		const NS_FLOAT b = t2 == VAR_INT ? (NS_FLOAT)v2->_int : v2->_float;
		StoreNumberFloat(r, a + b);
		return;
	}
	// 인라인 벡터(2·3성분)는 fast path 에서 끝낸다. 실패(성분 수 불일치 등)와 4성분은 rare 로.
	if (t1 == VAR_VEC && VecArithInline(r, v1, v2, 0)) return;
	Add3Rare(r, v1, v2);
}

// 드문 경로: 문자열 연결 / 벡터 / 메타테이블 / 리스트 / 타입 에러.
NEOS_NOINLINE void CNeoVMWorker::Add3Rare(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	switch (v1->GetType())
	{
	case VAR_VEC:
		if (VecArith(r, v1, v2, 0)) return;
		break;
	case VAR_STRING:
		if (v2->GetType() == VAR_STRING)
		{
			CheckRuntimeStringGrowth(v1->_str->_str.size(), v2->_str->_str.size());
			Var_SetStringA(r, v1->_str->_str + v2->_str->_str);
			return;
		}
		break;
	case VAR_VEC4:
		if (VecArith(r, v1, v2, 0)) return;
		break;
	case VAR_LIST:
		if (neo_DCalllibs::List_Add(this, r, v1, v2))
			return;
		break;
	case VAR_SET:
		break;
	default:
		break;
	}
	SetErrorOperator("+", v1, v2);
}
// 정수·실수 조합을 fall-through 직선 코드로 둔다. VAR_INT=0, VAR_FLOAT=1 이라 (t1|t2)==0 이면 정수끼리,
// (t1|t2)<=1 이면 숫자끼리다. 문자열/벡터/메타/에러는 Sub3Rare 로 분리 — 디스패치 루프에 인라인되므로 본문 크기가 성능이다.
NEOS_FORCEINLINE void CNeoVMWorker::Sub3(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	const unsigned t1 = v1->GetType(), t2 = v2->GetType();
    // 실수끼리는 정수 변환 분기를 거치지 않고 계산한다.
    if (t1 == VAR_FLOAT && t2 == VAR_FLOAT)
    {
        StoreNumberFloat(r, v1->_float - v2->_float);
        return;
    }
	if ((t1 | t2) == VAR_INT)
	{
		StoreNumberInt(r, NeoIntSub(v1->_int, v2->_int));
		return;
	}
	if ((t1 | t2) <= VAR_FLOAT)
	{
		const NS_FLOAT a = t1 == VAR_INT ? (NS_FLOAT)v1->_int : v1->_float;
		const NS_FLOAT b = t2 == VAR_INT ? (NS_FLOAT)v2->_int : v2->_float;
		StoreNumberFloat(r, a - b);
		return;
	}
	// 인라인 벡터(2·3성분)는 fast path 에서 끝낸다. 실패(성분 수 불일치 등)와 4성분은 rare 로.
	if (t1 == VAR_VEC && VecArithInline(r, v1, v2, 1)) return;
	Sub3Rare(r, v1, v2);
}

// 드문 경로: 문자열 연결 / 벡터 / 메타테이블 / 리스트 / 타입 에러.
NEOS_NOINLINE void CNeoVMWorker::Sub3Rare(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	switch (v1->GetType())
	{
	case VAR_VEC:
		if (VecArith(r, v1, v2, 1)) return;
		break;
	case VAR_STRING:
		break;
	case VAR_VEC4:
		if (VecArith(r, v1, v2, 1)) return;
		break;
	case VAR_LIST:
		break;
	case VAR_SET:
		if (neo_DCalllibs::Set_Sub(this, r, v1, v2))
			return;
		break;
	default:
		break;
	}
	SetErrorOperator("-", v1, v2);
}
// 정수·실수 조합을 fall-through 직선 코드로 둔다. VAR_INT=0, VAR_FLOAT=1 이라 (t1|t2)==0 이면 정수끼리,
// (t1|t2)<=1 이면 숫자끼리다. 문자열/벡터/메타/에러는 Mul3Rare 로 분리 — 디스패치 루프에 인라인되므로 본문 크기가 성능이다.
NEOS_FORCEINLINE void CNeoVMWorker::Mul3(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	const unsigned t1 = v1->GetType(), t2 = v2->GetType();
    // 실수끼리는 정수 변환 분기를 거치지 않고 계산한다.
    if (t1 == VAR_FLOAT && t2 == VAR_FLOAT)
    {
        StoreNumberFloat(r, v1->_float * v2->_float);
        return;
    }
	if ((t1 | t2) == VAR_INT)
	{
		StoreNumberInt(r, NeoIntMul(v1->_int, v2->_int));
		return;
	}
	if ((t1 | t2) <= VAR_FLOAT)
	{
		const NS_FLOAT a = t1 == VAR_INT ? (NS_FLOAT)v1->_int : v1->_float;
		const NS_FLOAT b = t2 == VAR_INT ? (NS_FLOAT)v2->_int : v2->_float;
		StoreNumberFloat(r, a * b);
		return;
	}
	// 인라인 벡터(2·3성분)는 fast path 에서 끝낸다. 실패(성분 수 불일치 등)와 4성분은 rare 로.
	if (t1 == VAR_VEC && VecArithInline(r, v1, v2, 2)) return;
	Mul3Rare(r, v1, v2);
}

// 드문 경로: 문자열 연결 / 벡터 / 메타테이블 / 리스트 / 타입 에러.
NEOS_NOINLINE void CNeoVMWorker::Mul3Rare(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	switch (v1->GetType())
	{
	case VAR_VEC:
		if (VecArith(r, v1, v2, 2)) return;
		break;
	case VAR_STRING:
		break;
	case VAR_VEC4:
		if (VecArith(r, v1, v2, 2)) return;
		break;
	case VAR_LIST:
		break;
	case VAR_SET:
		break;
	default:
		break;
	}
	SetErrorOperator("*", v1, v2);
}

// 정수·실수 조합을 fall-through 직선 코드로 둔다. VAR_INT=0, VAR_FLOAT=1 이라 (t1|t2)==0 이면 정수끼리,
// (t1|t2)<=1 이면 숫자끼리다. 문자열/벡터/메타/에러는 Div3Rare 로 분리 — 디스패치 루프에 인라인되므로 본문 크기가 성능이다.
NEOS_FORCEINLINE void CNeoVMWorker::Div3(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	const unsigned t1 = v1->GetType(), t2 = v2->GetType();
    // 실수끼리는 정수 변환 분기를 거치지 않고 계산한다.
    if (t1 == VAR_FLOAT && t2 == VAR_FLOAT)
    {
        StoreNumberFloat(r, v1->_float / v2->_float);
        return;
    }
	if ((t1 | t2) == VAR_INT)
	{
		if (CheckIntegerDivision(v1->_int, v2->_int))
			StoreNumberInt(r, v1->_int / v2->_int);
		return;
	}
	if ((t1 | t2) <= VAR_FLOAT)
	{
		const NS_FLOAT a = t1 == VAR_INT ? (NS_FLOAT)v1->_int : v1->_float;
		const NS_FLOAT b = t2 == VAR_INT ? (NS_FLOAT)v2->_int : v2->_float;
		StoreNumberFloat(r, a / b);
		return;
	}
	// 인라인 벡터(2·3성분)는 fast path 에서 끝낸다. 실패(성분 수 불일치 등)와 4성분은 rare 로.
	if (t1 == VAR_VEC && VecArithInline(r, v1, v2, 3)) return;
	Div3Rare(r, v1, v2);
}

// 드문 경로: 문자열 연결 / 벡터 / 메타테이블 / 리스트 / 타입 에러.
NEOS_NOINLINE void CNeoVMWorker::Div3Rare(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	switch (v1->GetType())
	{
	case VAR_VEC:
		if (VecArith(r, v1, v2, 3)) return;
		break;
	case VAR_STRING:
		break;
	case VAR_VEC4:
		if (VecArith(r, v1, v2, 3)) return;
		break;
	case VAR_LIST:
		break;
	default:
		break;
	}
	SetErrorOperator("/", v1, v2);
}

NEOS_FORCEINLINE void CNeoVMWorker::Per3(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	switch (v1->GetType())
	{
	case VAR_INT:
		if (v2->GetType() == VAR_INT)
		{
			if (CheckIntegerDivision(v1->_int, v2->_int))
				Var_SetInt(r, v1->_int % v2->_int);
			return;
		}
		break;
	default:
		break;
	}
	SetErrorOperator("%", v1, v2);
}

// int<<int(count 0..63) 만 인라인한다. 오류·비정수 경로는 *Rare 로 분리해 디스패치 루프를 키우지 않는다.
NEOS_FORCEINLINE void CNeoVMWorker::LSh3(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	if (v1->GetType() == VAR_INT && v2->GetType() == VAR_INT && static_cast<NS_UINT>(v2->_int) < 64)
	{
		Var_SetInt(r, NeoShiftLeft(v1->_int, v2->_int));
		return;
	}
	LSh3Rare(r, v1, v2);
}
NEOS_FORCEINLINE void CNeoVMWorker::RSh3(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	if (v1->GetType() == VAR_INT && v2->GetType() == VAR_INT && static_cast<NS_UINT>(v2->_int) < 64)
	{
		Var_SetInt(r, NeoShiftRight(v1->_int, v2->_int));
		return;
	}
	RSh3Rare(r, v1, v2);
}

NEOS_NOINLINE void CNeoVMWorker::LSh3Rare(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	switch (v1->GetType())
	{
	case VAR_INT:
		if (v2->GetType() == VAR_INT)
		{
			if (CheckShiftCount(v2->_int))
				Var_SetInt(r, NeoShiftLeft(v1->_int, v2->_int));
			return;
		}
		break;
	case VAR_FLOAT:
		break;
	case VAR_STRING:
		break;
	case VAR_MAP:
		break;
	case VAR_LIST:
		break;
	case VAR_SET:
		break;
	default:
		break;
	}
	SetErrorOperator("<<", v1, v2);
}
NEOS_NOINLINE void CNeoVMWorker::RSh3Rare(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	switch (v1->GetType())
	{
	case VAR_INT:
		if (v2->GetType() == VAR_INT)
		{
			if (CheckShiftCount(v2->_int))
				Var_SetInt(r, NeoShiftRight(v1->_int, v2->_int));
			return;
		}
		break;
	case VAR_FLOAT:
		break;
	case VAR_STRING:
		break;
	case VAR_MAP:
		break;
	case VAR_LIST:
		break;
	default:
		break;
	}
	SetErrorOperator(">>", v1, v2);
}
NEOS_FORCEINLINE void CNeoVMWorker::Xor3(VarInfo* r, VarInfo* v1, VarInfo* v2)
{
	switch (v1->GetType())
	{
	case VAR_INT:
		if (v2->GetType() == VAR_INT)
		{
			Var_SetInt(r, v1->_int ^ v2->_int);
			return;
		}
		break;
	case VAR_FLOAT:
		break;
	case VAR_STRING:
		break;
	case VAR_MAP:
		break;
	case VAR_LIST:
		break;
	case VAR_SET:
		break;
	default:
		break;
	}
	SetErrorOperator("^", v1, v2);
}

// 맵(문자열·정수 키)과 리스트(정수 인덱스)의 원소 슬롯. 없으면 nullptr. 디스패치 루프에 인라인되므로 작게 유지한다.
NEOS_FORCEINLINE VarInfo* CNeoVMWorker::FindCollectionSlot(VarInfo* owner, VarInfo* key)
{
	const VAR_TYPE type = owner->GetType();
	if (type == VAR_MAP)
	{
		if (key->GetType() == VAR_STRING) return owner->_tbl->FindString(key->_str);
		if (key->GetType() == VAR_INT) return owner->_tbl->FindInt(key->_int);
	}
	else if (type == VAR_LIST && key->GetType() == VAR_INT && (NS_UINT)key->_int < (NS_UINT)owner->_lst->_itemCount)
		return &owner->_lst->_Bucket[key->_int];
	return nullptr;
}

// 배열(숫자 원소 타입)과 벡터 성분을 지역 값으로 읽는다. 그 밖의 타입·범위 밖은 false.
NEOS_FORCEINLINE bool CNeoVMWorker::ReadArrayOrVectorElement(VarInfo* owner, VarInfo* key, VarInfo& value)
{
	if (key->GetType() != VAR_INT) return false;
	const VAR_TYPE type = owner->GetType();
	if (type == VAR_ARRAY)
	{
		ArrayInfo* array = owner->_array;
		const NS_INT index = key->_int;
		if (!array->IsValidIndex(index)) return false;
		switch (array->_elementType)
		{
		case NeoArrayElementType::Int:     value.SetType(VAR_INT);   value._int = array->IntData()[index];     return true;
		case NeoArrayElementType::Float:   value.SetType(VAR_FLOAT); value._float = array->FloatData()[index];   return true;
		case NeoArrayElementType::Int32:   value.SetType(VAR_INT);   value._int = array->Int32Data()[index];   return true;
		case NeoArrayElementType::Float32: value.SetType(VAR_FLOAT); value._float = array->Float32Data()[index]; return true;
		default: return false;
		}
	}
	if (owner->IsVector() && (NS_UINT)key->_int < (unsigned)owner->VectorComponentCount())
	{
		value.SetType(VAR_FLOAT); value._float = owner->VectorComponent(static_cast<int>(key->_int));
		return true;
	}
	return false;
}

// READ_* 의 산술. op 는 호출 지점에서 상수라 인라인 뒤 분기가 접힌다.
NEOS_FORCEINLINE void CNeoVMWorker::ReadArith2(VarInfo* r, VarInfo* v, eNOperation op)
{
	if (op == NOP_READ_ADD) Add2(r, v);
	else if (op == NOP_READ_SUB) Sub2(r, v);
	else if (op == NOP_READ_MUL) Mul2(r, v);
	else Div2(r, v);
}

// dest op= clt[key]. 맵/리스트의 숫자 원소는 제자리에서 산술에 넘기고(복합 대입은 dest 가 컨테이너를 해제할 수 없다),
// 배열 원소·벡터 성분은 지역 값으로 읽는다. 그 밖의 원소 타입·없는 키·호스트 속성·오류는 ReadArithRare 가
// 분리된 READ + 산술과 같은 결과를 낸다. 디스패치 루프에 네 벌 인라인되므로 본문을 작게 유지한다.
template<eNOperation OPK>
NEOS_FORCEINLINE void CNeoVMWorker::ReadArith(const SVMOperation& OP)
{
	VarInfo* owner = GetVarPtrF1(OP);
	VarInfo* key = GetVarPtr2(OP);
	VarInfo* found = FindCollectionSlot(owner, key);
	if (found != nullptr)
	{
		if (found->IsNumber())
		{
			ReadArith2(GetVarPtr3(OP), found, OPK);
			return;
		}
	}
	else
	{
		VarInfo value;
		if (ReadArrayOrVectorElement(owner, key, value))
		{
			ReadArith2(GetVarPtr3(OP), &value, OPK);
			return;
		}
	}
	ReadArithRare(OP, OPK);
}

NEOS_FORCEINLINE bool CNeoVMWorker::handle_READ_ADD(const SVMOperation& OP) { ReadArith<NOP_READ_ADD>(OP); return false; }
NEOS_FORCEINLINE bool CNeoVMWorker::handle_READ_SUB(const SVMOperation& OP) { ReadArith<NOP_READ_SUB>(OP); return false; }
NEOS_FORCEINLINE bool CNeoVMWorker::handle_READ_MUL(const SVMOperation& OP) { ReadArith<NOP_READ_MUL>(OP); return false; }
NEOS_FORCEINLINE bool CNeoVMWorker::handle_READ_DIV(const SVMOperation& OP) { ReadArith<NOP_READ_DIV>(OP); return false; }

// 느린 경로. 일반 읽기(참조 수·호스트 getter·오류 포함)로 원소를 가져온 뒤 산술한다. 호스트 getter 가 요청한
// 코루틴 전환은 명령을 끝낸 뒤 처리한다. 읽기 뒤에는 중첩 실행으로 스택이 재할당됐을 수 있어 dest 포인터를 다시 구한다.
NEOS_NOINLINE void CNeoVMWorker::ReadArithRare(const SVMOperation& OP, eNOperation op)
{
	// 읽은 값은 VM 스택 슬롯이 아니라 지역 값이라 오류 정리가 모른다. 호스트 getter 가 값을 넣은 뒤
	// C++ 예외를 던지거나 산술이 예외를 내도 해제되도록 소멸자에서 놓는다.
	struct ElementHolder
	{
		CNeoVMWorker* worker;
		VarInfo value;
		explicit ElementHolder(CNeoVMWorker* w) : worker(w) {}
		~ElementHolder() { worker->Var_Release(&value); }
	} holder(this);
	int deferredTransferFrame = -1;
	if (!ReadElementForArith(GetVarPtrF1(OP), GetVarPtr2(OP), &holder.value, &deferredTransferFrame))
		return;
	ReadArith2(GetVarPtr3(OP), &holder.value, op);
	if (deferredTransferFrame >= 0 && !IsErrorTrap())
		FinishPropertyTransfer(deferredTransferFrame);
}

// 느린 경로의 원소 읽기. 호스트 속성은 getter 를 직접 불러 코루틴 전환을 미루고, 나머지는 일반 읽기다.
// getter 가 실행 컨텍스트(코루틴·스택·IP)를 바꿨으면 부분 실행을 이어가지 않고 오류로 끝낸다.
inline bool CNeoVMWorker::ReadElementForArith(VarInfo* clt, VarInfo* key, VarInfo* value, int* deferredTransferFrame)
{
	if (clt->GetType() == VAR_FP_NATIVE && key->GetType() == VAR_STRING)
	{
		const CoroutineInfo* context = m_pCur;
		const std::vector<VarInfo>* stack = m_pVarStack_Base;
		const SVMOperation* next = _pCodeCurrent;
		FunctionPropertyInfo* fp = clt->_fpNative;
		PropertyNative(fp->_fun, fp->_pUserData, key->_str, value, true, deferredTransferFrame);
		if (IsErrorTrap()) return false;
		if (m_pCur != context || m_pVarStack_Base != stack || _pCodeCurrent != next)
		{
			SetError(RTE_CALL_INVALID);
			return false;
		}
		return true;
	}
	CltRead(clt, key, value);
	return !IsErrorTrap();
}

NEOS_FORCEINLINE void CNeoVMWorker::MoveMinus(VarInfo* v1, VarInfo* v2)
{
	switch (v2->GetType())
	{
	case VAR_INT:
		Var_SetInt(v1, NeoIntNeg(v2->_int));
		return;
	case VAR_FLOAT:
		Var_SetFloat(v1, -v2->_float);
		return;
	default:
		break;
	}
	SetErrorOperator("-", v2);
}

NEOS_FORCEINLINE void CNeoVMWorker::LSh2(VarInfo* r, VarInfo* v2)
{
	if (r->GetType() == VAR_INT && v2->GetType() == VAR_INT && static_cast<NS_UINT>(v2->_int) < 64)
	{
		r->_int = NeoShiftLeft(r->_int, v2->_int);
		return;
	}
	LSh2Rare(r, v2);
}
NEOS_FORCEINLINE void CNeoVMWorker::RSh2(VarInfo* r, VarInfo* v2)
{
	if (r->GetType() == VAR_INT && v2->GetType() == VAR_INT && static_cast<NS_UINT>(v2->_int) < 64)
	{
		r->_int = NeoShiftRight(r->_int, v2->_int);
		return;
	}
	RSh2Rare(r, v2);
}

// 숫자 대입은 참조 해제가 필요 없으며 정수·실수 경로를 직접 계산한다.
NEOS_FORCEINLINE void CNeoVMWorker::Add2(VarInfo* r, VarInfo* v2)
{
	const unsigned t1 = r->GetType(), t2 = v2->GetType();
	if ((t1 | t2) == VAR_INT)
	{
		r->_int = NeoIntAdd(r->_int, v2->_int);
		return;
	}
	if ((t1 | t2) <= VAR_FLOAT)
	{
		const NS_FLOAT a = t1 == VAR_INT ? (NS_FLOAT)r->_int : r->_float;
		const NS_FLOAT b = t2 == VAR_INT ? (NS_FLOAT)v2->_int : v2->_float;
		r->SetType(VAR_FLOAT);
		r->_float = a + b;
		return;
	}
	Add2Rare(r, v2);
}

// 드문 경로: 문자열 연결 / 메타테이블 / 타입 에러.
NEOS_NOINLINE void CNeoVMWorker::Add2Rare(VarInfo* r, VarInfo* v2)
{
	switch (r->GetType())
	{
	case VAR_STRING:
		if (v2->GetType() == VAR_STRING)
		{
			CheckRuntimeStringGrowth(r->_str->_str.size(), v2->_str->_str.size());
			Var_SetStringA(r, r->_str->_str + v2->_str->_str);
			return;
		}
		break;
	default:
		break;
	}
	SetErrorOperator("+=", r, v2);
}
// 숫자 대입은 참조 해제가 필요 없으며 정수·실수 경로를 직접 계산한다.
NEOS_FORCEINLINE void CNeoVMWorker::Sub2(VarInfo* r, VarInfo* v2)
{
	const unsigned t1 = r->GetType(), t2 = v2->GetType();
	if ((t1 | t2) == VAR_INT)
	{
		r->_int = NeoIntSub(r->_int, v2->_int);
		return;
	}
	if ((t1 | t2) <= VAR_FLOAT)
	{
		const NS_FLOAT a = t1 == VAR_INT ? (NS_FLOAT)r->_int : r->_float;
		const NS_FLOAT b = t2 == VAR_INT ? (NS_FLOAT)v2->_int : v2->_float;
		r->SetType(VAR_FLOAT);
		r->_float = a - b;
		return;
	}
	Sub2Rare(r, v2);
}

// 드문 경로: 문자열 연결 / 메타테이블 / 타입 에러.
NEOS_NOINLINE void CNeoVMWorker::Sub2Rare(VarInfo* r, VarInfo* v2)
{
	switch (r->GetType())
	{
	case VAR_STRING:
		break;
	default:
		break;
	}
	SetErrorOperator("-=", r, v2);
}
// 숫자 대입은 참조 해제가 필요 없으며 정수·실수 경로를 직접 계산한다.
NEOS_FORCEINLINE void CNeoVMWorker::Mul2(VarInfo* r, VarInfo* v2)
{
	const unsigned t1 = r->GetType(), t2 = v2->GetType();
	if ((t1 | t2) == VAR_INT)
	{
		r->_int = NeoIntMul(r->_int, v2->_int);
		return;
	}
	if ((t1 | t2) <= VAR_FLOAT)
	{
		const NS_FLOAT a = t1 == VAR_INT ? (NS_FLOAT)r->_int : r->_float;
		const NS_FLOAT b = t2 == VAR_INT ? (NS_FLOAT)v2->_int : v2->_float;
		r->SetType(VAR_FLOAT);
		r->_float = a * b;
		return;
	}
	Mul2Rare(r, v2);
}

// 드문 경로: 문자열 연결 / 메타테이블 / 타입 에러.
NEOS_NOINLINE void CNeoVMWorker::Mul2Rare(VarInfo* r, VarInfo* v2)
{
	switch (r->GetType())
	{
	case VAR_STRING:
		break;
	default:
		break;
	}
	SetErrorOperator("*=", r, v2);
}
// 숫자 대입은 참조 해제가 필요 없으며 정수·실수 경로를 직접 계산한다.
NEOS_FORCEINLINE void CNeoVMWorker::Div2(VarInfo* r, VarInfo* v2)
{
	const unsigned t1 = r->GetType(), t2 = v2->GetType();
	if ((t1 | t2) == VAR_INT)
	{
		if (CheckIntegerDivision(r->_int, v2->_int))
			r->_int /= v2->_int;
		return;
	}
	if ((t1 | t2) <= VAR_FLOAT)
	{
		const NS_FLOAT a = t1 == VAR_INT ? (NS_FLOAT)r->_int : r->_float;
		const NS_FLOAT b = t2 == VAR_INT ? (NS_FLOAT)v2->_int : v2->_float;
		r->SetType(VAR_FLOAT);
		r->_float = a / b;
		return;
	}
	Div2Rare(r, v2);
}

// 드문 경로: 문자열 연결 / 메타테이블 / 타입 에러.
NEOS_NOINLINE void CNeoVMWorker::Div2Rare(VarInfo* r, VarInfo* v2)
{
	switch (r->GetType())
	{
	case VAR_STRING:
		break;
	default:
		break;
	}
	SetErrorOperator("/=", r, v2);
}
NEOS_FORCEINLINE void CNeoVMWorker::Per2(VarInfo* r, VarInfo* v2)
{
	switch (r->GetType())
	{
	case VAR_INT:
		if (v2->GetType() == VAR_INT)
		{
			if (CheckIntegerDivision(r->_int, v2->_int))
				r->_int %= v2->_int;
			return;
		}
		break;
	case VAR_FLOAT:
		break;
	case VAR_STRING:
		break;
	case VAR_MAP:
		break;
	default:
		break;
	}
	SetErrorOperator("%=", r, v2);
}
NEOS_NOINLINE void CNeoVMWorker::LSh2Rare(VarInfo* r, VarInfo* v2)
{
	switch (r->GetType())
	{
	case VAR_INT:
		if (v2->GetType() == VAR_INT)
		{
			if (CheckShiftCount(v2->_int))
				r->_int = NeoShiftLeft(r->_int, v2->_int);
			return;
		}
		break;
	case VAR_FLOAT:
		break;
	case VAR_STRING:
		break;
	case VAR_MAP:
		break;
	default:
		break;
	}
	SetErrorOperator("<<=", r, v2);
}
NEOS_NOINLINE void CNeoVMWorker::RSh2Rare(VarInfo* r, VarInfo* v2)
{
	switch (r->GetType())
	{
	case VAR_INT:
		if (v2->GetType() == VAR_INT)
		{
			if (CheckShiftCount(v2->_int))
				r->_int = NeoShiftRight(r->_int, v2->_int);
			return;
		}
		break;
	case VAR_FLOAT:
		break;
	case VAR_STRING:
		break;
	case VAR_MAP:
		break;
	default:
		break;
	}
	SetErrorOperator(">>=", r, v2);
}
NEOS_FORCEINLINE void CNeoVMWorker::And2(VarInfo* r, VarInfo* v2)
{
	switch (r->GetType())
	{
	case VAR_INT:
		if (v2->GetType() == VAR_INT)
		{
			r->_int &= v2->_int;
			return;
		}
		break;
	case VAR_FLOAT:
		break;
	case VAR_STRING:
		break;
	case VAR_MAP:
		break;
	default:
		break;
	}
	SetErrorOperator("&=", r, v2);
}
NEOS_FORCEINLINE void CNeoVMWorker::Or2(VarInfo* r, VarInfo* v2)
{
	switch (r->GetType())
	{
	case VAR_INT:
		if (v2->GetType() == VAR_INT)
		{
			r->_int |= v2->_int;
			return;
		}
		break;
	case VAR_FLOAT:
		break;
	case VAR_STRING:
		break;
	case VAR_MAP:
		break;
	default:
		break;
	}
	SetErrorOperator("|=", r, v2);
}
NEOS_FORCEINLINE void CNeoVMWorker::Xor2(VarInfo* r, VarInfo* v2)
{
	switch (r->GetType())
	{
	case VAR_INT:
		if (v2->GetType() == VAR_INT)
		{
			r->_int ^= v2->_int;
			return;
		}
		break;
	case VAR_FLOAT:
		break;
	case VAR_STRING:
		break;
	case VAR_MAP:
		break;
	default:
		break;
	}
	SetErrorOperator("^=", r, v2);
}
