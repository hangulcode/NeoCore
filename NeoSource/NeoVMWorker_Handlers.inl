// NeoVMWorker_Handlers.inl

#pragma once

// --- Instruction Handlers ---
// Return 'true' to exit the RunInternal loop, 'false' to continue.

// 연결 피연산자 하나의 텍스트. 문자열은 그 버퍼를 가리키고, 정수는 임시 std::string 없이 자릿수를 만든다.
// 그 밖의 값(실수·bool·null 등)은 기존 ToString 표기를 그대로 쓴다.
struct ConcatPiece
{
    const char* data = nullptr;
    size_t size = 0;
    int chars = 0;      // UTF-8 글자 수. 문자열은 저장된 값을, 정수 자릿수는 바이트 수를 쓴다.
    char digits[24];
    std::string storage;
};
NEOS_FORCEINLINE void MakeConcatPiece(ConcatPiece& piece, ValueRef value)
{
    const VAR_TYPE type = value.GetType();
    if (type == VAR_STRING)
    {
        piece.data = value->_str->_str.data();
        piece.size = value->_str->_str.size();
        piece.chars = value->_str->_StringLen;
        return;
    }
    if (type == VAR_INT)
    {
        char* end = piece.digits + sizeof(piece.digits);
        char* begin = end;
        NS_UINT magnitude = value->_int < 0 ? NS_UINT(0) - NS_UINT(value->_int) : NS_UINT(value->_int);
        do { *--begin = char('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
        if (value->_int < 0) *--begin = '-';
        piece.data = begin;
        piece.size = static_cast<size_t>(end - begin);
        piece.chars = static_cast<int>(piece.size);
        return;
    }
    piece.storage = ToString(value);
    piece.data = piece.storage.data();
    piece.size = piece.storage.size();
    piece.chars = utf_string::UTF8_LENGTH(piece.storage);
}
// 두 조각을 이은 글자 수는 조각 글자 수의 합이다. 단, 오른쪽이 이어지는 바이트(10xxxxxx)로 시작하면 왼쪽 끝의
// 불완전한 글자와 합쳐질 수 있으므로 그때만 다시 센다(-1).
NEOS_FORCEINLINE int ConcatChars(int leftChars, const ConcatPiece& right)
{
    if (right.size != 0 && (static_cast<u8>(right.data[0]) & 0xC0) == 0x80) return -1;
    return leftChars + right.chars;
}

// 목적지가 참조 하나뿐인 일반 문자열이면 그 버퍼를 다시 쓴다. `s = s .. x` 는 뒤에 붙이기만 하므로 반복 누적이
// 길이에 비례하는 복사·할당 없이 끝난다. 오른쪽 피연산자가 목적지 자신이면(`s = x .. s`) 일반 경로로 간다.
// 참조가 둘 이상이거나 정규(인터닝) 문자열이면 다른 소유자가 보고 있으므로 새 문자열을 만든다.
NEOS_FORCEINLINE void ConcatenateStrings(ValueRef result, ValueRef first, ValueRef second) {
    StringInfo* own = result.GetType() == VAR_STRING ? result->_str : nullptr;
    if (own != nullptr && own->_refCount == 1 && !own->_interned &&
        !(second.GetType() == VAR_STRING && second->_str == own))
    {
        ConcatPiece right;
        MakeConcatPiece(right, second);
        if (first.GetType() == VAR_STRING && first->_str == own)
        {
            CheckRuntimeStringGrowth(own->_str.size(), right.size);
            own->_str.reserve(own->_str.size() + right.size);
            const int chars = ConcatChars(own->_StringLen, right);
            own->_str.append(right.data, right.size);
            own->_StringLen = chars >= 0 ? chars : utf_string::UTF8_LENGTH(own->_str);
        }
        else
        {
            ConcatPiece left;
            MakeConcatPiece(left, first);
            CheckRuntimeStringGrowth(left.size, right.size);
            own->_str.reserve(left.size + right.size);
            own->_str.assign(left.data, left.size);
            own->_str.append(right.data, right.size);
            const int chars = ConcatChars(left.chars, right);
            own->_StringLen = chars >= 0 ? chars : utf_string::UTF8_LENGTH(own->_str);
        }
        own->_hash = 0;
        return;
    }
    ConcatPiece left, right;
    MakeConcatPiece(left, first);
    MakeConcatPiece(right, second);
    _pVM->Var_SetStringConcat(result, left.data, left.size, right.data, right.size, ConcatChars(left.chars, right));
}

NEOS_NOINLINE bool handle_STR_ADD(const SVMOperation& OP) {
    ConcatenateStrings(GetVarPtr1(OP), GetVarPtr2(OP), GetVarPtr3(OP));
    return false;
}

NEOS_NOINLINE bool handle_STR_ADD_L(const SVMOperation& OP) {
    ConcatenateStrings(GetVarPtr_L(OP.n1), GetVarPtr_L(OP.n2), GetVarPtr_L(OP.n3));
    return false;
}

NEOS_NOINLINE bool handle_TOSTRING(const SVMOperation& OP) {
    Var_SetStringA(GetVarPtr1(OP), ToString(GetVarPtr2(OP)));
    return false;
}

// 숫자는 공통 변환의 타입 분기를 피한다. 목적지는 숫자 슬롯이라 payload만 쓴다.
NEOS_FORCEINLINE void ConvertToInt(VarInfo* dst, ValueRef src) {
    const VAR_TYPE type = src.GetType();
    dst->_int = type == VAR_INT ? src->_int :
        type == VAR_FLOAT ? NeoFloatToInt(src->_float) : ToInt(src);
}
NEOS_FORCEINLINE void ConvertToFloat(VarInfo* dst, ValueRef src) {
    const VAR_TYPE type = src.GetType();
    dst->_float = type == VAR_INT ? static_cast<NS_FLOAT>(src->_int) :
        type == VAR_FLOAT ? src->_float : ToFloat(src);
}

NEOS_FORCEINLINE bool handle_TOINT(const SVMOperation& OP) {
    ConvertToInt(GetVarRawF1(OP), GetVarPtr2(OP));
    return false;
}

// _L 변형: 오퍼랜드가 전부 로컬인 것이 프로그램 빌드 시(PatchLocalOps) 확정된 op.
// argFlag 비트 테스트 없이 스택 베이스로 직행한다(test+cmov 제거).
NEOS_FORCEINLINE bool handle_TOINT_L(const SVMOperation& OP) {
    ConvertToInt(GetVarRaw_L(OP.n1), GetVarPtr_L(OP.n2));
    return false;
}

NEOS_FORCEINLINE bool handle_TOFLOAT(const SVMOperation& OP) {
    ConvertToFloat(GetVarRawF1(OP), GetVarPtr2(OP));
    return false;
}

NEOS_NOINLINE bool handle_TOSIZE(const SVMOperation& OP) {
    GetVarRawF1(OP)->_int = ToSize(GetVarPtr2(OP));
    return false;
}


NEOS_NOINLINE bool handle_SLEEP(const SVMOperation& OP) {
	if (m_iNativeScriptCallDepth > 0) {
		SetErrorFormat(RTE_NESTED_NOT_ALLOWED, "sleep");
		return false;
	}
    int r = Sleep(m_iTimeout, GetVarPtr2(OP));
    if (r == 0) {
        return true; // Exit RunInternal
    }
    else if (r == 2) {
        m_op_process = 0;
    }
    return false; // Continue RunInternal
}

// 캡처 closure 생성(할당 + 캡처 복사 루프)은 함수값 대입에서만 실행되는 드문 경로다.
// FORCEINLINE으로 두면 FMOV1/FMOV2 두 곳에 전개돼 디스패치 루프의 인라인 크기만 키우므로
// 드문 경로의 본문은 별도 헬퍼로 분리하여 RunInternal의 크기를 줄인다.
NEOS_NOINLINE void set_script_closure_value(ValueRef dst, int functionIndex) {
    const SFunctionTable& fun = Functions()[functionIndex];
    ClosureInfo* closure = GetVM()->ClosureAlloc(functionIndex, fun._captureTypes);
    closure->_ownerWorkerId = GetWorkerID();
    bool mayContainContainerChild = false;
    for (size_t i = 0; i < fun._captures.size(); ++i)
    {
        // 원본 슬롯의 종류는 캡처 배치표가 준다. 부모 프레임 슬롯이라 현재 함수 표와 같다.
        ValueRef source(GetVarRaw_L(fun._captures[i].first), (*fun._captureTypes)[i]);
        if (source.IsContainerType())
            mayContainContainerChild = true;
        Move(closure->Capture(i), source);
    }
    if (mayContainContainerChild)
        closure->_cycleState._mayContainContainerChild = true;

    // f = fun() { return f; } 처럼 목적지와 캡처 원본이 같은 경우가 있다.
    // 캡처를 먼저 복사해야 기존 alloc 값을 지우고 난 뒤 null을 보관하지 않는다.
    if (dst.IsAllocType())
        Var_Release(dst);
    dst.SetType(VAR_CLOSURE);
    dst->_closure = closure;
    ++closure->_refCount;
}

// 캡처 없는 함수값 대입(대부분의 FMOV)만 인라인에 남긴다.
NEOS_FORCEINLINE void set_script_function_value(ValueRef dst, int functionIndex) {
    if (Functions()[functionIndex]._captures.empty()) {
        Var_SetFun(dst, functionIndex);
        return;
    }
    set_script_closure_value(dst, functionIndex);
}

NEOS_NOINLINE bool handle_FMOV1(const SVMOperation& OP) {
    set_script_function_value(GetVarPtr1(OP), OP.n2);
    return false;
}

NEOS_NOINLINE bool handle_FMOV2(const SVMOperation& OP) {
    BoundaryValue fun;
    set_script_function_value(&fun, OP.n3);
    CltInsert(GetVarPtr1(OP), GetVarPtr2(OP), &fun);
    Var_Release(&fun);
    return false;
}

NEOS_FORCEINLINE int GetCallReturnValueIndex(const SVMOperation& OP) {
    // NORESULT는 결과를 쓰지 않는 문장 호출이다(파서가 붙인다). 결과를 창 0번에 남기면 가져가는 명령이 없고,
    // 다음 PREPARE_CALL이 그 슬롯을 해제 없이 비워 소유 결과(문자열·벡터·컬렉션)가 호출마다 하나씩 샌다.
    // 함수값 호출과 같이 RETURN_DISCARD로 넘겨 피호출자의 프레임 정리가 결과를 해제하게 한다.
    // -1(창 0번에 남김)은 네이티브·호스트가 결과 슬롯을 직접 읽는 호출만 쓴다.
    if (OP.argFlag & NEOS_OP_CALL_NORESULT)
        return RETURN_DISCARD;
    if (OP.argFlag & NEOS_ARG_N3_LOCAL)
        return _iSP_Vars + OP.n3;
    // 전역 목적지는 opcode와 같은 음수 오프셋을 저장한다. 기준점은 상수 0이다.
    return OP.n3;
}

NEOS_FORCEINLINE bool handle_CALL(const SVMOperation& OP) {
    Call(OP.n1, OP.n2, GetCallReturnValueIndex(OP));
    return false;
}

NEOS_FORCEINLINE bool handle_CALL_L(const SVMOperation& OP) {
    // NORESULT 면 반환 슬롯 자체를 안 뽑으므로 PatchLocalOps 가 _L 로 바꾸지 않는다.
    Call(OP.n1, OP.n2, _iSP_Vars + OP.n3);
    return false;
}

// 본문은 base/_L 공용. pFunNamePre 가 있으면 그것을 쓰고(=_L, 로컬 확정),
// nullptr 이면 기존대로 필요한 분기에서만 늦게 뽑는다(n2 == -1 센티널 보호).
// 네이티브 라이브러리 디스패치. 본문 명령어의 대부분이 여기이고 PTRCALL/PTRCALL_L 두 곳에
// 전개되므로 드문 경로는 루프 밖으로 분리한다.
// pFunName 은 원본과 같이 분기 안에서 늦게 뽑는다(n2 == -1 센티널 보호).
NEOS_NOINLINE bool handle_PTRCALL_native(const SVMOperation& OP, ValueRef pVar1, ValueRef pFunNamePre) {
    short n3 = OP.n3;
    // 결과를 쓰지 않는 호출문은 창에 남는 결과를 바로 해제한다. 쓰는 호출은 뒤따르는 MOV가 가져간다.
    const bool keepResult = (OP.argFlag & NEOS_OP_CALL_NORESULT) == 0;
    const int discard = keepResult ? -1 : RETURN_DISCARD;
    if ((OP.argFlag & NEOS_ARG_N2_LOCAL) == 0 && OP.n2 == -1) {
        SetErrorUnsupport(g_sNeoRuntimeErrors[RTE_CALL_VALUE], pVar1);
        return false;
    }
    ValueRef pFunName = nullptr;
    switch (pVar1.GetType())
    {
    case VAR_STRING:
        pFunName = pFunNamePre ? pFunNamePre : GetVarPtr2(OP);
        CallNative(GetVM()->_funLib_String, std::addressof(pVar1), pFunName->_str, n3, nullptr, keepResult);
        break;
    case VAR_FP_NATIVE:
        pFunName = pFunNamePre ? pFunNamePre : GetVarPtr2(OP);
        CallNative(pVar1->_fpNative->_fun, pVar1->_fpNative->_pUserData, pFunName->_str, n3, nullptr, keepResult);
        break;
    case VAR_ARRAY:
        pFunName = pFunNamePre ? pFunNamePre : GetVarPtr2(OP);
        CallNative(GetVM()->_funLib_Array, std::addressof(pVar1), pFunName->_str, n3, nullptr, keepResult);
        break;
    case VAR_MAP:
    {
        pFunName = pFunNamePre ? pFunNamePre : GetVarPtr2(OP);
        ValueRef pVarMeta = pVar1->_tbl->Find(pFunName);
        if (pVarMeta != nullptr)
        {
            if (_iSP_Vars_Max2 < _iSP_VarsMax + (1 + n3))
                _iSP_Vars_Max2 = _iSP_VarsMax + (1 + n3);

            if (pVarMeta.GetType() == VAR_FUN)
            {
                CallFunctionValue(pVarMeta.FunctionIndex(), n3, discard);
                break;
            }
            if (pVarMeta.GetType() == VAR_CLOSURE)
            {
                Call(pVarMeta->_closure, n3, discard);
                break;
            }
        }
        CallNative(GetVM()->_funLib_Map, std::addressof(pVar1), pFunName->_str, n3, nullptr, keepResult);
        break;
    }
    case VAR_LIST:
        pFunName = pFunNamePre ? pFunNamePre : GetVarPtr2(OP);
        CallNative(GetVM()->_funLib_List, std::addressof(pVar1), pFunName->_str, n3, nullptr, keepResult);
        break;
    case VAR_SET:
        pFunName = pFunNamePre ? pFunNamePre : GetVarPtr2(OP);
        CallNative(GetVM()->_funLib_Set, std::addressof(pVar1), pFunName->_str, n3, nullptr, keepResult);
        break;
    case VAR_ASYNC:
        pFunName = pFunNamePre ? pFunNamePre : GetVarPtr2(OP);
        CallNative(GetVM()->_funLib_Async, std::addressof(pVar1), pFunName->_str, n3, nullptr, keepResult);
        break;
    default:
        // 함수값 호출(n2=-1)은 이름 슬롯이 없다. 멤버 호출만 이름을 읽는다.
        if ((OP.argFlag & NEOS_ARG_N2_LOCAL) || OP.n2 != -1)
        {
            pFunName = pFunNamePre ? pFunNamePre : GetVarPtr2(OP);
            if (pFunName.GetType() == VAR_STRING)
            {
                SetErrorFormat(pVar1.GetType() == VAR_NONE ? RTE_CALL_NULL_RECEIVER : RTE_CALL_NAMED, pFunName->_str->_str.c_str());
                break;
            }
        }
        SetErrorUnsupport(g_sNeoRuntimeErrors[RTE_CALL_VALUE], pVar1);
        break;
    }
    return false;
}

// 스크립트/네이티브 함수값을 바로 부르는 세 경우만 인라인에 남긴다. 각 두 줄이라
// 디스패치 루프를 거의 키우지 않으면서, 호출당 분기 하나로 끝난다.
NEOS_FORCEINLINE bool handle_PTRCALL_impl(const SVMOperation& OP, ValueRef pVar1, ValueRef pFunNamePre) {
    const int discard = (OP.argFlag & NEOS_OP_CALL_NORESULT) ? RETURN_DISCARD : -1;
    switch (pVar1.GetType())
    {
    case VAR_FUN:
        if ((OP.argFlag & NEOS_ARG_N2_LOCAL) == 0 && -1 == OP.n2) {
            CallFunctionValue(pVar1.FunctionIndex(), OP.n3, discard);
            return false;
        }
        break;
    case VAR_FUN_NATIVE:
        if ((OP.argFlag & NEOS_ARG_N2_LOCAL) == 0 && -1 == OP.n2) {
            Call(pVar1->_funPtr, OP.n3, nullptr, discard == -1);
            return false;
        }
        break;
    case VAR_CLOSURE:
        if ((OP.argFlag & NEOS_ARG_N2_LOCAL) == 0 && -1 == OP.n2) {
            Call(pVar1->_closure, OP.n3, discard);
            return false;
        }
        break;
    default:
        break;
    }
    return handle_PTRCALL_native(OP, pVar1, pFunNamePre);
}

NEOS_FORCEINLINE bool handle_PTRCALL(const SVMOperation& OP) {
    return handle_PTRCALL_impl(OP, GetVarPtr1(OP), nullptr);
}

NEOS_FORCEINLINE bool handle_PTRCALL_L(const SVMOperation& OP) {
    // n1/n2 모두 로컬로 확정 → 메서드명 슬롯도 미리 뽑아 넘긴다.
    return handle_PTRCALL_impl(OP, GetVarPtr_L(OP.n1), GetVarPtr_L(OP.n2));
}

// 본문 공용. pRet 은 NORESULT 를 이미 반영한 반환 슬롯(없으면 nullptr = 결과를 버린다).
NEOS_FORCEINLINE bool handle_PTRCALL2_impl(short n2, ValueRef pFunName, VarInfo* pRet) {
    if (pFunName.GetType() == VAR_STRING)
    {
        CallNative(GetVM()->_funLib_Default, nullptr, pFunName->_str, n2, pRet, false);
    }

    if (_iSP_Vars_Max2 < _iSP_VarsMax + (1 + n2))
        _iSP_Vars_Max2 = _iSP_VarsMax + (1 + n2);
    return false;
}

NEOS_FORCEINLINE bool handle_PTRCALL2(const SVMOperation& OP) {
    return handle_PTRCALL2_impl(OP.n2, GetVarPtr1(OP),
        (OP.argFlag & NEOS_OP_CALL_NORESULT) ? nullptr : GetVarRaw3(OP));
}

NEOS_FORCEINLINE bool handle_PTRCALL2_L(const SVMOperation& OP) {
    return handle_PTRCALL2_impl(OP.n2, GetVarPtr_L(OP.n1),
        (OP.argFlag & NEOS_OP_CALL_NORESULT) ? nullptr : GetVarRaw_L(OP.n3));
}

NEOS_FORCEINLINE bool handle_NATIVECALL(const SVMOperation& OP) {
    short n2 = OP.n2;
    CallDefaultNativeByIndex(OP.n1, n2, (OP.argFlag & NEOS_OP_CALL_NORESULT) ? nullptr : GetVarRaw3(OP));

    if (_iSP_Vars_Max2 < _iSP_VarsMax + (1 + n2))
        _iSP_Vars_Max2 = _iSP_VarsMax + (1 + n2);
    return false;
}

// 반환값을 목적지로 옮긴다. 지역 원본은 소유권을 넘기고(참조 증감 없음), 전역 원본은 복사한다.
NEOS_FORCEINLINE void StoreReturnValue(VarInfo* destination, VarInfo* source, bool sourceLocal, StoragePolicy policy) {
    if (source == nullptr) return;
    if (!PolicyOwns(policy)) { destination->_int = source->_int; return; }
    if (sourceLocal) TakeByPolicy(destination, source, policy);
    else CopyByPolicy(destination, source, policy);
}

// 본문 공용. pSrc = 반환값 슬롯(NORESULT 면 nullptr). 결과를 먼저 옮긴 뒤 프레임의 소유 슬롯을 해제한다.
template <bool PayloadFrame>
NEOS_FORCEINLINE bool handle_RETURN_impl(VarInfo* pSrc, bool sourceLocal, int functionIndex, u16 control) {
    // 반환 명령의 함수 번호는 현재 프레임과 같다. 함수 표를 다시 찾지 않고 프레임 기록을 쓴다.
    assert(functionIndex == _functionIndex);
    (void)functionIndex;
    const SFunctionTable& fun = *m_pFunction;
    const StoragePolicy resultPolicy = PayloadFrame ? StoragePolicy::Payload
        : static_cast<StoragePolicy>(control & SLOT_INFO_POLICY_MASK);
    if (m_iBreakingCallStack == (int)m_pCallStack->size())
    {
		// 최상위/코루틴 시작 closure는 호출 프레임이 없으므로 active 상태로 판별한다.
		if (!PayloadFrame && m_pActiveClosure != nullptr)
			FinishClosureReturn(pSrc);

        StoreReturnValue(m_pVarStack_Pointer, pSrc, sourceLocal, resultPolicy);
        if (!PayloadFrame) LeaveFrame(fun, _iSP_Vars);
        // 호스트는 반환 슬롯 타입을 함수의 반환 타입으로 읽어야 한다. 남은 창 계획은 없어야 한다.
        _activePlan = nullptr;

        if (m_iBreakingCallStack == 0 && !IsNativeScriptCallActive() && IsMainCoroutine(m_pCur) == false)
        {
            if (StopCoroutine(true) == true) // Other Coroutine Active (No Stop)
                return false; // break, continue loop
        }
        _iSP_VarsMax = _iSP_Vars;
        return true; // exit RunInternal
    }

    const SCallStack callStack = m_pCallStack->pop_back();
	const u32 returnIP = callStack._ReturnIP;
	if (!PayloadFrame && m_pActiveClosure != nullptr)
		FinishClosureReturn(pSrc);

    if (callStack._iReturnValueIndex == RETURN_DISCARD)
    {
        // 결과를 쓰지 않는 호출문. 지역 결과는 프레임 정리가 해제한다.
    }
    else if (callStack._iReturnValueIndex != -1)
    {
        VarInfo* pReturnValue = (callStack._iReturnValueIndex >= 0)
            ? m_pVarStackData + callStack._iReturnValueIndex
            : m_pVarGlobal_Pointer + callStack._iReturnValueIndex;
        StoreReturnValue(pReturnValue, pSrc, sourceLocal, resultPolicy);
    }
    else
    {
        StoreReturnValue(m_pVarStack_Pointer, pSrc, sourceLocal, resultPolicy);
    }

    if (!PayloadFrame) LeaveFrame(fun, _iSP_Vars);

    if (returnIP & FLAG_ASYNCWAIT)
    {
        if (m_pCur == nullptr || m_pCur->m_sAsyncWaitReturnStack.size() == 0)
        {
            SetError(RTE_ASYNC_RESUME_STATE);
            return false;
        }
        SAsyncWaitReturnState& state = m_pCur->m_sAsyncWaitReturnStack.pop_back();
        Var_SetBool(state._pReturnValue, state._returnValue);
    }

	SetCodePtr((int)(returnIP & IP_MASK));
	_iSP_Vars = callStack._iSP_Vars;
	SetStackPointer(_iSP_Vars);
	_iSP_VarsMax = callStack._iSP_VarsMax;
	SelectFunction(callStack._functionIndex);
	if (returnIP & FLAG_CLOSURE)
	{
		SClosureCallState& state = m_pClosureCallStack->pop_back();
		m_pActiveClosure = state._closure;
		if (m_pCur)
			m_pCur->_activeClosure = m_pActiveClosure;
	}
    return false; // break, continue loop
}

// 소유권·캡처 정리가 있는 본문은 디스패치 루프 밖에서 한 번만 생성한다.
NEOS_NOINLINE bool handle_RETURN_owned(VarInfo* source, bool sourceLocal, int functionIndex, u16 control) {
    return handle_RETURN_impl<false>(source, sourceLocal, functionIndex, control);
}

NEOS_FORCEINLINE bool handle_RETURN(const SVMOperation& OP) {
    VarInfo* source = (OP.argFlag & NEOS_OP_CALL_NORESULT) ? nullptr : GetVarRawF1(OP);
    const bool sourceLocal = (OP.argFlag & NEOS_ARG_N1_LOCAL) != 0;
    if (OP.n3 & RETURN_PAYLOAD_FRAME)
        return handle_RETURN_impl<true>(source, sourceLocal, OP.n2, static_cast<u16>(OP.n3));
    return handle_RETURN_owned(source, sourceLocal, OP.n2, static_cast<u16>(OP.n3));
}

NEOS_FORCEINLINE bool handle_RETURN_L(const SVMOperation& OP) {
    // NORESULT 면 슬롯을 안 뽑으므로 PatchLocalOps 가 _L 로 바꾸지 않는다.
    if (OP.n3 & RETURN_PAYLOAD_FRAME)
        return handle_RETURN_impl<true>(GetVarRaw_L(OP.n1), true, OP.n2, static_cast<u16>(OP.n3));
    return handle_RETURN_owned(GetVarRaw_L(OP.n1), true, OP.n2, static_cast<u16>(OP.n3));
}

NEOS_NOINLINE bool handle_TABLE_ALLOC(const SVMOperation& OP) {
    Var_SetTable(GetVarPtr1(OP), GetVM()->TableAlloc(OP.n23));
    return false;
}

NEOS_FORCEINLINE bool handle_CLT_READ(const SVMOperation& OP) {
    CltRead(GetVarPtr1(OP), GetVarPtr2(OP), GetVarPtr3(OP));
    return false;
}

NEOS_FORCEINLINE bool handle_CLT_READ_L(const SVMOperation& OP) {
    CltRead(GetVarPtr_L(OP.n1), GetVarPtr_L(OP.n2), GetVarPtr_L(OP.n3));
    return false;
}

// Program 로드 시 n2가 static 문자열 상수인 READ만 이 경로로 바뀐다.
// 키는 워커별 static 슬롯에서 매번 읽고 FindString도 매번 수행한다. 따라서 맵 수정이나
// 풀 재사용 뒤의 value 포인터를 보관하는 inline cache와 달리 수명/무효화 상태가 없다.
NEOS_FORCEINLINE bool handle_CLT_READ_STATIC_STRING(const SVMOperation& OP) {
    ValueRef pClt = GetVarPtr1(OP);
    ValueRef pKey = GetVarPtr_G(OP.n2);
    ValueRef pValue = GetVarPtr3(OP);
    if (pClt.GetType() == VAR_MAP) {
        ValueRef pFind = pClt->_tbl->FindString(pKey->_str);
        if (pFind)
            Move(pValue, pFind);
        else
            Var_Release(pValue);
        return false;
    }

    // static 문자열로 가능한 나머지 컨테이너/프로퍼티 의미론은 범용 경로와 같다.
    CltReadRare(pClt, pKey, pValue);
    return false;
}

NEOS_FORCEINLINE bool handle_CLT_MOV(const SVMOperation& OP) {
    CltInsert(GetVarPtr1(OP), GetVarPtr2(OP), GetVarPtr3(OP), &OP);
    return false;
}

NEOS_FORCEINLINE bool handle_CLT_MOV_L(const SVMOperation& OP) {
    CltInsert(GetVarPtr_L(OP.n1), GetVarPtr_L(OP.n2), GetVarPtr_L(OP.n3), &OP);
    return false;
}

// Keep the atomic vector body out of the dispatch loop. The common list/map
// read below stays inline, just like an ordinary CLT_READ.
NEOS_NOINLINE void WriteStoredVector(ValueRef target, ValueRef temp) {
    const SVMOperation write = *GetOP();
    Var_Release(temp); // release old values before deciding whether COW is needed
    CltInsert(target, GetVarPtr2(write), GetVarPtr3(write));
    if (GetCodeptr() != 0) CltRead(target, GetVarPtr2(write), temp);
}

// Stored vectors consume the validated pair atomically. Other values leave the
// ordinary write to normal dispatch, including after a suspended native getter.
NEOS_NOINLINE void handle_CLT_WRITE_REF(const SVMOperation& OP) {
    ValueRef temp = GetVarPtr3(OP);
    ValueRef owner = GetVarPtr1(OP);
    ValueRef key = GetVarPtr2(OP);
    ValueRef target = nullptr;
    if (owner.GetType() == VAR_LIST)
        target = GetTableItem(owner, key);
    else if (owner.GetType() == VAR_MAP) {
        // Match CLT_READ's common key paths without the generic Find dispatch.
        const VAR_TYPE kt = key.GetType();
        target = kt == VAR_STRING ? owner->_tbl->FindString(key->_str) :
            kt == VAR_INT ? owner->_tbl->FindInt(key->_int) : owner->_tbl->Find(key);
    }
    else if (owner.GetType() == VAR_ARRAY && owner->_array->VectorDimension()) {
        // 배열도 리스트와 같이 원소 VarInfo를 직접 빌려 원자적으로 성분을 수정한다.
        assert(key.GetType() == VAR_INT);
        if (!owner->_array->IsValidIndex(key->_int)) {
            SetError(RTE_ARRAY_INDEX_WRITE);
            return;
        }
        target = owner->_array->VectorAt(static_cast<int>(key->_int));
    }

    if (target) {
        if (target.IsVector()) WriteStoredVector(target, temp);
        else if ((OP.argFlag & NEOS_OP_REF_LOADED) == 0) Move(temp, target);
        return;
    }
    if (GetCodeptr() != 0 && (OP.argFlag & NEOS_OP_REF_LOADED) == 0)
        CltRead(owner, key, temp);
}



NEOS_NOINLINE bool handle_LIST_ALLOC(const SVMOperation& OP) {
    Var_SetList(GetVarPtr1(OP), GetVM()->ListAlloc(OP.n23));
    return false;
}

NEOS_NOINLINE bool handle_LIST_ALLOC_L(const SVMOperation& OP) {
    Var_SetList(GetVarPtr_L(OP.n1), GetVM()->ListAlloc(OP.n23));
    return false;
}

// 정수 슬롯은 내보내기가 이 명령을 빈 명령으로 바꾼다. 남는 것은 실수 슬롯의 정수 변환뿐이다.
NEOS_FORCEINLINE void ChangeSlotToInt(VarInfo* slot, VAR_TYPE declared) {
    if (declared == VAR_FLOAT) slot->_int = NeoFloatToInt(slot->_float);
}
NEOS_FORCEINLINE bool handle_CHANGE_INT(const SVMOperation& OP) {
    ChangeSlotToInt(GetVarRawF1(OP), SlotType1(OP));
    return false;
}

NEOS_FORCEINLINE bool handle_CHANGE_INT_L(const SVMOperation& OP) {
    ChangeSlotToInt(GetVarRaw_L(OP.n1), SlotType_L(OP.n1));
    return false;
}

NEOS_NOINLINE bool handle_YIELD(const SVMOperation& OP) {
	if (m_iNativeScriptCallDepth > 0) {
		SetErrorFormat(RTE_NESTED_NOT_ALLOWED, "yield");
		return false;
	}
    if (StopCoroutine(false) == false)
        return true; // Exit RunInternal
    return false; // Continue RunInternal
}

NEOS_NOINLINE bool handle_IDLE(const SVMOperation& OP) {
    if (m_pCur == nullptr || m_pCur->m_sAsyncResumeCodePtrs.empty())
    {
        SetError(RTE_ASYNC_RESUME_STATE);
        return false;
    }

    AsyncResumeInfo resumeInfo = m_pCur->m_sAsyncResumeCodePtrs.back();
    m_pCur->m_sAsyncResumeCodePtrs.pop_back();
    if (resumeInfo.pReturnValue != nullptr)
    {
        assert(resumeInfo.pReturnValue.GetType() == VAR_BOOL);
        resumeInfo.returnValue = resumeInfo.pReturnValue->_bl;
    }
    SetCodePtr(resumeInfo.codePtr);
    while (true)
    {
        AsyncInfo* p = GetVM()->Pop_AsyncInfo(this);
        if (p == nullptr) break;
        if (!EnsureStackRange(_iSP_VarsMax, 2))
        {
            GetVM()->Var_Release(&p->_LockReferance);
			GetVM()->Var_Release(&p->_callback);
			// EnsureStackRange redirected execution to the error opcode.
			// Continue the loop so RunInternal reports the VM error.
            return false;
        }
        // 비동기 완료 콜백의 void(bool, string) 계약은 고정이다. 호출 창은 비어 있으므로 바로 쓴다.
        VarInfo* window = GetStackFromBase(_iSP_VarsMax);
        for (int i = 0; i < 3; ++i) window[i]._int = 0;
        _iSP_Vars_Max2 = (std::max)(_iSP_Vars_Max2, _iSP_VarsMax + 3);
        window[1]._bl = p->_success;
        Var_SetStringA(ValueRef(window + 2, VAR_STRING), p->_resultValue);
		BoundaryValue callback;
		Move_DestNoRelease(&callback, &p->_callback);
		const int callStackSize = m_pCallStack->size();
		if (callback.GetType() == VAR_CLOSURE)
			Call(callback._closure, 2, RETURN_DISCARD);
		else
			Call(callback._fun_index, 2, RETURN_DISCARD);
        if (resumeInfo.pReturnValue != nullptr && m_pCallStack->size() == callStackSize + 1)
        {
            SAsyncWaitReturnState& state = m_pCur->m_sAsyncWaitReturnStack.push_back();
            state._pReturnValue = resumeInfo.pReturnValue;
            state._returnValue = resumeInfo.returnValue;
            (*m_pCallStack)[callStackSize]._ReturnIP |= FLAG_ASYNCWAIT;
        }
		GetVM()->Var_Release(&p->_LockReferance);
		GetVM()->Var_Release(&p->_callback);
		GetVM()->Var_Release(&callback);
    }
    return false;
}

// 벡터 생성 intrinsic (LoadVM 에서 PTRCALL2(#Vector*) 패치). native 호출 프레임 없이
// 인자 슬롯(_iSP_VarsMax+1..N)을 직접 읽어 값타입 세팅. n3=결과 슬롯.
// 벡터 저장을 이 핸들러에서 처리하므로 디스패치 루프에 전개되면 그만큼
// 루프가 커진다. 생성은 성분 쓰기·산술만큼 자주 돌지 않으므로 루프 밖에 둔다.
NEOS_NOINLINE bool handle_VEC_MAKE(const SVMOperation& OP) {
    // n2 = 인자 수 = 성분 수. Vector2/3/4/Quaternion 이 모두 이 op 으로 패치되며,
    // 무엇으로 해석할지는(예: wxyz 쿼터니언) 사용자 규약이지 VM 의 타입이 아니다.
    const int n = OP.n2;
    // 저수준 바이트코드 경로도 공유하므로 버퍼 크기를 정하는 명령 형식은 검증한다.
    if (n < 2 || n > 4) {
        SetError(RTE_CALL_INVALID);
        return false;
    }
    // 함수 진입에서 인자 영역까지 스택 범위와 해제 상한을 확정한다.
    // 인자와 목적지가 겹쳐도 값을 보존하도록 새 벡터를 완성한 뒤 대상에 연결한다.
    // 인자의 숫자 종류(int/float)는 호출 계획이 준다.
    VarInfo* args = GetStackFromBase(_iSP_VarsMax + 1);
    float tmp[4] = {};   // n 개만 쓰고 n 개만 복사한다
    for (int i = 0; i < n; ++i) {
        ValueRef argument(args + i, PlanType(1 + i));
        assert(argument.IsNumber());
        tmp[i] = NeoNarrowFloat32(argument.GetFloatNumber());
    }
    _activePlan = nullptr;
    if (OP.argFlag & NEOS_OP_CALL_NORESULT) return false;
    // 2성분은 직접 저장하고, 3·4성분은 단독 소유 저장소를 재사용한다.
    StoreVector(GetVarPtr3(OP), n, tmp[0], tmp[1], tmp[2], tmp[3]);
    return false;
}

// switch: n1 = table index, n2 = 조건식 위치. 매칭 실패/지원 외 타입이면 default 로.
// 점프는 op 단위 상대(다음 op 기준)라 기존 JMP 계열과 동일하다.
NEOS_NOINLINE bool handle_SWITCH(const SVMOperation& OP) {
    int jumpOffset = 0;
    if (false == TryGetSwitchJumpOffset((u16)OP.n1, GetVarPtr2(OP), jumpOffset))
    {   // 정상 컴파일 결과에서는 발생하지 않는다 (손상된 이미지 등)
        SetError(RTE_SWITCH_TABLE);
        return false;
    }
    SetCodeIncPtr(jumpOffset);
    return false;
}

NEOS_FORCEINLINE bool handle_NONE(const SVMOperation& OP) {
    // No operation
    return false;
}

NEOS_NOINLINE bool handle_ERROR(const SVMOperation& OP) {
    const int actualIndex = _isErrorOPIndex;
    const int idx = m_iLastErrorSourceOPIndex >= 0 ? m_iLastErrorSourceOPIndex : actualIndex;
    bool blDebugInfo = IsDebugInfo();
    int _lineseq = -1;
    if (blDebugInfo)
        _lineseq = GetDebugLine(idx);

    char chMsg[256];
#ifdef _WIN32
    snprintf(chMsg, _countof(chMsg), "%s : IP(%d), Line(%d)", GetVM()->_pErrorMsg.c_str(), idx, _lineseq);
#else
    sprintf(chMsg, "%s : IP(%d), Line(%d)", GetVM()->_pErrorMsg.c_str(), idx, _lineseq);
#endif

    std::string detail = chMsg;
#if 0
    if (GetVM()->_sErrorMsgDetail.empty())
        GetVM()->_sErrorMsgDetail = chMsg;
#else
    detail += FormatStackTrace(actualIndex);
    if (GetVM()->_sErrorMsgDetail.empty())
        GetVM()->_sErrorMsgDetail = detail;
#endif

    // Report the VM error even when a debugger converts execution into a
    // fault snapshot.  Keep the VM log payload bounded as well; the editor
    // log stores only its first line.
    if (NeoVMSystem::m_pFunError) {
        static const size_t kMaxErrorLogLength = 4096;
        if (detail.size() > kMaxErrorLogLength)
        {
            std::string logDetail = detail.substr(0, kMaxErrorLogLength);
            logDetail += "\n... Script call stack truncated in log (see debugger for full stack)";
            NeoVMSystem::m_pFunError(logDetail.c_str());
        }
        else
        {
            NeoVMSystem::m_pFunError(detail.c_str());
        }
    }

    if (m_pDebugListener || m_iDebugBreakCount > 0 || m_eDebugRunMode != DBG_CONTINUE || m_bDebugPauseRequested)
    {
        if (actualIndex >= 0 && actualIndex < (int)DebugData().size())
            StopDebug(actualIndex, NEO_DEBUG_STOP_EXCEPTION);
        return true;
    }

    _isErrorOPIndex = 0;
	// 오류 unwind는 정상 return을 거치지 않는다. 현재/부모 closure의 캡처 슬롯을
	// 먼저 동기화하고 실행 보유분을 모두 놓은 뒤, 모든 프레임의 소유 슬롯을 정책으로 해제한다.
	UnwindActiveClosures();
    _iSP_Vars = 0;
    SetStackPointer(_iSP_Vars);

    return true; // Exit RunInternal
}

// 정책이 실린 복사. 하위 4비트가 정책 또는 즉값 종류다. 호출 결과는 MODIFIER로 소유권만 옮긴다.
NEOS_FORCEINLINE void handle_MOV(const SVMOperation& OP) {
    const unsigned sub = OP.argFlag & NEOS_OP_SUB_MASK;
    VarInfo* target = GetVarRawF1(OP);
    switch (sub)
    {
    case static_cast<unsigned>(StoragePolicy::Payload):
        target->_int = GetVarRaw2(OP)->_int;
        return;
    case MOV_SUB_IMMEDIATE_INT:
        target->_int = OP.n23;
        return;
    case MOV_SUB_IMMEDIATE_FLOAT:
        MoveF(target, OP.n23);
        return;
    case MOV_SUB_BOXED:
        // 종류를 정하지 못한 복사. 배치표의 선언 타입으로 경계 복사를 한다.
        Move(GetVarPtr1(OP), GetVarPtr2(OP));
        return;
    default:
        if (OP.argFlag & NEOS_OP_MODIFIER) TakeByPolicy(target, GetVarRaw2(OP), static_cast<StoragePolicy>(sub));
        else CopyByPolicy(target, GetVarRaw2(OP), static_cast<StoragePolicy>(sub));
        return;
    }
}

// 실행 빈도가 낮은 opcode 의 핸들러. 디스패치 루프(RunInternal)는 타임아웃×디버그로 네 벌 생성되고 한 벌이
// 50 KB 를 넘어 uop 캐시를 벗어나므로, 드문 핸들러는 본문을 밖으로 빼 핫 루프를 작게 유지한다.
// 선정 근거: 벤치·smoke 의 동적 opcode 계측과 게임 프로젝트 스크립트 101개의 정적 opcode 빈도(둘 다 0.1% 미만인 것만).
// 비트·시프트 연산과 2-피연산자 산술은 계측에서는 드물었지만 xorshift 류 난수·해시 코드가 op 마다 호출을
// 치러 느려졌으므로(실측) 루프 안으로 되돌렸다. 여기 남은 것은 비교→bool, 논리 점프, LOADK 류다.
// 상수 복사. 목적지 정책이 하위 4비트에 있다. 문자열 상수는 참조를 나눠 가진다.
NEOS_NOINLINE void handle_LOADK_cold(const SVMOperation& OP) {
    const StoragePolicy policy = static_cast<StoragePolicy>(OP.argFlag & NEOS_OP_SUB_MASK);
    if (policy == StoragePolicy::Boxed) Move(GetVarPtr1(OP), GetVarPtr_G(static_cast<short>(OP.n23)));
    else CopyByPolicy(GetVarRawF1(OP), m_pVarGlobal_Pointer + OP.n23, policy);
}
// 참조 슬롯 비우기. 정책이 하위 4비트에 있다.
NEOS_FORCEINLINE void handle_VAR_CLEAR(const SVMOperation& OP) {
    const StoragePolicy policy = static_cast<StoragePolicy>(OP.argFlag & NEOS_OP_SUB_MASK);
    if (policy == StoragePolicy::Boxed) Var_Release(GetVarPtr1(OP));
    else { VarInfo* slot = GetVarRawF1(OP); ReleaseByPolicy(slot, policy); slot->_int = 0; }
}
NEOS_NOINLINE void handle_LOG_NOT_cold(const SVMOperation& OP) { StoreNumberBool(GetVarRawF1(OP), !GetVarRaw2(OP)->_bl); }
NEOS_NOINLINE void handle_GREAT_cold(const SVMOperation& OP) { StoreNumberBool(GetVarRawF1(OP), CompareGR(GetVarPtr2(OP), GetVarPtr3(OP))); }
NEOS_NOINLINE void handle_GREAT_EQ_cold(const SVMOperation& OP) { StoreNumberBool(GetVarRawF1(OP), CompareGE(GetVarPtr2(OP), GetVarPtr3(OP))); }
NEOS_NOINLINE void handle_LESS_cold(const SVMOperation& OP) { StoreNumberBool(GetVarRawF1(OP), CompareGR(GetVarPtr3(OP), GetVarPtr2(OP), true)); }
NEOS_NOINLINE void handle_LESS_EQ_cold(const SVMOperation& OP) { StoreNumberBool(GetVarRawF1(OP), CompareGE(GetVarPtr3(OP), GetVarPtr2(OP), true)); }
NEOS_NOINLINE void handle_EQUAL2_cold(const SVMOperation& OP) { StoreNumberBool(GetVarRawF1(OP), CompareEQ(GetVarPtr2(OP), GetVarPtr3(OP))); }
NEOS_NOINLINE void handle_NEQUAL_cold(const SVMOperation& OP) { StoreNumberBool(GetVarRawF1(OP), !CompareEQ(GetVarPtr2(OP), GetVarPtr3(OP))); }
