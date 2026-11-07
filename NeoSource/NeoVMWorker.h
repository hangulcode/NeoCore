#pragma once

#include <cassert>

#include <chrono>

#include "NeoVMInternal.h"
#include "NeoStringLimits.h"
#include "NeoNumeric.h"
#include "NeoVMError.h"

#include "NeoVMProgram.h"
#include "NeoVMFusion.h"



namespace NeoScript
{

//struct SNeoFunLib
//{
//	std::string pName;
//	FunctionPtrNative fn;
//};

struct SVarWrapper
{
	ValueRef _var;
	CNeoVMWorker*	_vmw;
	inline SVarWrapper(CNeoVMWorker* p, ValueRef var) { _var = var; _vmw = p; }

	void SetNone();
	void SetInt(int v);
	void SetFloat(NS_FLOAT v);
	void SetBool(bool v);
	void SetString(const char* str);
//	void SetTableFun(FunctionPtrNative fun);
};


// NEOS_OP_CALL_NORESULT 는 NeoVMInternal.h 의 argFlag 비트 정의로 이동
// JMP_FOREACH 전용: foreach(var k, v in ...) 2변수 형태. 대상이 list 인지는 런타임에만 알 수
// 있으므로, 컴파일러가 이 플래그만 실어 보내고 미지원 판정/에러는 ForEach 런타임에서 한다.


#define NEOS_GLOBAL_VAR(idx) (m_pVarGlobal_Pointer.SlotOffset(idx))

class mRND 
{
public:
	void seed(unsigned int s) {
		_seed = s;
	}
	int rnd() // 0 ~ 0x7fff
	{
		_seed = (a * _seed + c) % m;
		return (_seed >> 16);
	}
	mRND() {}
protected:
	int a = 214013;
	int c = 2531011;
	unsigned int m = 2147483648;
	unsigned int _seed = 0;
};


struct neo_libs;
class CNeoVM;
class CNeoVMWorker : public INeoVMWorker, public AllocBase, public CoroutineBase
{
	friend		CNeoVM;
	friend		SVarWrapper;
	friend		MapInfo;
	friend		neo_libs;
private:
	class NativeFrameScope;
	// 컴파일 이미지(코드/함수테이블/디버그정보/상수)는 프로그램이 소유하고 워커들이 공유한다.
	CNeoVMProgram*			_pProgram = nullptr;
	const u8 *				_pCodeBegin = nullptr;   // = _pProgram->GetCodeBegin()

	int					_isErrorOPIndex = 0;
	// 오류 핸들러가 IP와 스택을 비운 뒤에도 호출 결과에 가장 안쪽 원인 위치를 전달한다.
	int					m_iLastErrorOPIndex = -1;
	// 공통 도우미 안에서 실패하면 스택이 해제되기 전에 사용자 호출 위치도 따로 보관한다.
	int					m_iLastErrorSourceOPIndex = -1;

	bool					_isInitialized = false;
	int						_iRemainSleep = 0;
	// sleep 카운트다운 기준 시각. 슬라이스 타임아웃 측정(_preClock)과 반드시 분리해야 한다 —
	// 같은 변수를 쓰면 실행 루프가 타임아웃용으로 덮어써서 남은 sleep 이 밀린다.
	std::chrono::steady_clock::time_point _sleepClock;
	std::chrono::steady_clock::time_point _preClock;

	int m_iTimeout = -1;
	// NeoCore의 sleep(0)도 한 번 양보한다. 기존 호스트의 동기 sleep 동작은 유지한다.
	bool m_bCooperativeSleep = false;
	bool m_bCooperativeSleepPending = false;
	int m_iCheckOpCount = NEO_DEFAULT_CHECKOP;
	int m_op_process = 0;
	uint64_t m_instructionBudget = 0;
	bool m_budgetRestrictsNested = true;
	bool m_bSliceExpired = false; // 시간 제한으로 다음 ResumeTop까지 보류됨
	int m_iBreakingCallStack = 0;
	bool m_bTopExec = false;   // 최상위 실행/재개 중(=완료까지 실행)인지
	// std::bad_alloc이 실행 중 발생한 워커는 중간 상태의 안전성을 보장할 수 없다.
	// 이 플래그는 worker-local이며 VM 전체나 다른 인스턴스에는 영향을 주지 않는다.
	bool m_bOutOfMemoryPoisoned = false;
	// 인터프리터 루프(Run) 안인지. 중첩 Run 을 고려해 저장/복원한다.
	// 실행 중에는 실행 컨텍스트를 해제할 수 없으므로 CancelExecution 이 이 플래그로 거부한다.
	bool m_bInRun = false;

    enum EDebugRunMode
    {
        DBG_CONTINUE,
        DBG_STEP_INTO,
        DBG_STEP_OVER,
        DBG_STEP_OUT,
        DBG_PAUSED,
    };

    INeoVMDebugListener* m_pDebugListener = nullptr;
    std::vector<u8> m_sDebugBreakLineBits;
    std::vector<std::vector<u8>> m_sDebugBreakLocationBits;
    int m_iDebugBreakCount = 0;
    NeoDebugLocation m_sDebugLocation;
    EDebugRunMode m_eDebugRunMode = DBG_CONTINUE;
    bool m_bDebugPauseRequested = false;
	bool m_bDebugFaulted = false;
    struct DebugLineSkip
    {
        CoroutineInfo* context;
        int depth;
        int file;
        int line;
        int lastOp;
        int stoppedOp;
    };
    std::vector<DebugLineSkip> m_sDebugLineSkips;
    int m_iDebugStepDepth = -1;
    void PruneDebugLineSkips(CoroutineInfo* context, int maxDepth);
    CoroutineInfo* m_pDebugStepContext = nullptr;
    CoroutineInfo* m_pDebugStepReturnContext = nullptr;
    void PrepareDebugResume(EDebugRunMode mode);
    int m_iDebugSuppressCount = 0;
    // Script A -> native API -> Script B 동기 재진입 범위에서만 증가한다.
    int m_iNativeScriptCallDepth = 0;
    // 이 워커가 요청했고 아직 회수하지 않은 async 수. 소멸 시 고아 판정에 쓴다.
    int _asyncPendingCount = 0;

    void ClearDebugBreakpoints();
    void SetDebugBreakLineBit(std::vector<u8>& bits, int line);
    bool IsDebugBreakLineBit(const std::vector<u8>& bits, int line) const;
    bool IsDebugBreakpoint(int file, int line) const;

//	inline void SetCheckTime() { m_op_process = 0; }
	void JumpAsyncMsg();

	NEOS_FORCEINLINE const SVMOperation*	GetOP()
	{
		return _pCodeCurrent++;
	}
	int GetDebugLine(int iOPIndex);
    bool CheckDebugStop(int iOPIndex);
    void StopDebug(int iOPIndex, NeoDebugStopReason reason);
	void ResetFaultStateForNewExecution();
    int GetFunctionIndexFromCodeOffset(int codeOffset);
	std::string FormatStackTrace(int currentOpIndex);
	NEOS_FORCEINLINE int GetCodeptr() { return (int)((const u8*)_pCodeCurrent - _pCodeBegin); }
	NEOS_FORCEINLINE void SetCodePtr(int off) { _pCodeCurrent = (const SVMOperation*)(_pCodeBegin + off); }
	// 점프 offset 은 op(SVMOperation) 단위. SVMOperation* 포인터 산술로 op 수만큼 이동.
	NEOS_FORCEINLINE void SetCodeIncPtr(int opOff) { _pCodeCurrent += opOff; }
	NEOS_FORCEINLINE bool IsErrorTrap() const { return (const u8*)_pCodeCurrent == _pCodeBegin; }
	NEOS_FORCEINLINE void SetComparisonJump(int opOff)
	{
		// A failed comparison has already selected the error opcode.
		if (!IsErrorTrap()) SetCodeIncPtr(opOff);
	}
	bool CheckNestedCallDepth();

	// 워커 고유 상태 = 전역 변수 슬롯([static | global] 단일 배열).
	// static 구간은 로드 시 프로그램의 상수 서술자에서 이 VM 의 할당자로 실체화한다.
	std::vector<VarInfo>	m_sVarGlobal;

	NEOS_FORCEINLINE const std::vector<SFunctionTable>& Functions() const { return _pProgram->functions; }
	// switch table 은 Program 소유(읽기 전용 공유). 매칭 실패/지원 외 타입이면 default offset
	// 을 돌려주는 것이 정상 동작이다. n1 은 short 저장이지만 인덱스는 부호 없이 해석한다(0~65535).
	// 테이블 인덱스가 범위를 벗어나는 건 손상된 이미지/VM 버그뿐이므로 호출측에서 에러 처리한다.
	NEOS_FORCEINLINE bool TryGetSwitchJumpOffset(u16 tableIndex, ValueRef pKey, int& outOffset) const
	{
		const std::vector<ProgramSwitchTable>& t = _pProgram->switchTables;
		if ((size_t)tableIndex >= t.size())
			return false;
		outOffset = t[tableIndex].Find(pKey);
		return true;
	}
	template<bool negateOutside = false>
	NEOS_FORCEINLINE bool IsRangeInside(const SVMOperation& op, bool allLocal)
	{
		const ProgramRangeJump& range = _pProgram->rangeJumps[(u16)op.n3];
		ValueRef test = allLocal ? GetVarPtr_L(op.n2) : GetVarPtr2(op);
		ValueRef lower = allLocal || (range.flags & NEOS_RANGE_LOWER_LOCAL)
			? GetVarPtr_L(range.lower) : GetVarPtr_G(range.lower);
		ValueRef upper = allLocal || (range.flags & NEOS_RANGE_UPPER_LOCAL)
			? GetVarPtr_L(range.upper) : GetVarPtr_G(range.upper);

		const bool lowerInclusive = (range.flags & NEOS_RANGE_LOWER_INCLUSIVE) != 0;
		const bool upperInclusive = (range.flags & NEOS_RANGE_UPPER_INCLUSIVE) != 0;
		const bool upperFirst = (range.flags & NEOS_RANGE_UPPER_FIRST) != 0;
		// 세 피연산자가 모두 float 이면 CompareGR/GE 의 타입 분기와 중간 에러 검사 없이 _float 를 바로 비교한다
		// (파티클 류 범위 검사의 거의 전부). 비교식은 아래 일반 경로와 같게 두어 NaN 결과도 같고, float 끼리는
		// 에러가 없어 upperFirst 순서가 결과에 영향을 주지 않는다. int 변환을 섞으면 선택 코드가 오히려 느려 넣지 않는다.
		if (test.GetType() == VAR_FLOAT && lower.GetType() == VAR_FLOAT && upper.GetType() == VAR_FLOAT)
		{
			const NS_FLOAT v = test->_float, lo = lower->_float, hi = upper->_float;
			if (negateOutside)
				return !((lowerInclusive ? lo > v : lo >= v) || (upperInclusive ? v > hi : v >= hi));
			return (lowerInclusive ? v >= lo : v > lo) && (upperInclusive ? hi >= v : hi > v);
		}
		// OR ranges require !(x < lower || x > upper), not x >= lower &&
		// x <= upper: those differ for unordered (NaN) operands.
		if (negateOutside)
		{
			if (upperFirst)
			{
				if (upperInclusive ? CompareGR(test, upper) : CompareGE(test, upper)) return false;
				if (IsErrorTrap()) return false;
				return !(lowerInclusive ? CompareGR(lower, test, true) : CompareGE(lower, test, true));
			}
			if (lowerInclusive ? CompareGR(lower, test, true) : CompareGE(lower, test, true)) return false;
			if (IsErrorTrap()) return false;
			return !(upperInclusive ? CompareGR(test, upper) : CompareGE(test, upper));
		}
		if (upperFirst)
		{
			if (!(upperInclusive ? CompareGE(upper, test, true) : CompareGR(upper, test, true))) return false;
			return lowerInclusive ? CompareGE(test, lower) : CompareGR(test, lower);
		}
		if (!(lowerInclusive ? CompareGE(test, lower) : CompareGR(test, lower))) return false;
		return upperInclusive ? CompareGE(upper, test, true) : CompareGR(upper, test, true);
	}
	NEOS_FORCEINLINE const std::vector<debug_info>& DebugData() const { return _pProgram->debugData; }

	inline bool IsDebugInfo() { return _pProgram->IsDebugInfo(); }

	virtual int FindFunction(const std::string& name) override
	{
		return _pProgram->FindFunction(name);
	}

	ValueStack*	m_pVarStack_Base;
	// m_pVarStack_Base->size() 를 캐시한다. EnsureStackRange 는 호출마다 도는데(프로파일상
	// 7억 회) 매번 포인터 → 벡터 → size 로 의존 로드를 두 단 탔다. 스택은 실행 컨텍스트를
	// 대여할 때 한 번만 resize 되고 실행 중에는 절대 커지지 않으므로(EnsureStackRange 는
	// 넘치면 에러를 낼 뿐 늘리지 않는다) 베이스가 바뀔 때만 갱신하면 된다.
	int						m_iVarStackSize;
	ValueRef				m_pVarStack_Pointer;

	// 둘이 어긋나면 스택 범위 검사가 조용히 틀리므로 항상 함께 바꾼다.
	NEOS_FORCEINLINE void SetVarStackBase(ValueStack* p)
	{
		m_pVarStack_Base = p;
		m_iVarStackSize = (p != nullptr) ? (int)p->size() : 0;
	}
	SimpleVector<SCallStack>* m_pCallStack;
	SimpleVector<SClosureCallState>* m_pClosureCallStack;
	// 현재 실행 프레임이 어떤 값 캡처 closure에서 시작됐는지. 호출 중에는
	// 보조 closure 스택이 부모 프레임을 보존하고, return/error에서 캡처 슬롯
	// 변경을 동기화한 뒤 실행 보유 참조를 푼다.
	ClosureInfo* m_pActiveClosure = nullptr;

	std::vector<VarInfo>*	m_pVarGlobal;
	ValueRef m_pVarGlobal_Pointer;

	std::list< CoroutineInfo*> m_sCoroutines;

	NeoExecContextPool* m_pPool = nullptr;      // 실행 컨텍스트 풀(로드 시 주입, 실행 시 대여/반납)
	CoroutineInfo* m_pMainCtx = nullptr;           // 현재/정지된 최상위 실행의 컨텍스트. idle 이면 nullptr
	CoroutineInfo* m_pCur = nullptr;
	CoroutineInfo* m_pRegisterActive = nullptr;

	void    BindContext(CoroutineInfo* ctx);            // 컨텍스트를 활성 스택으로 바인딩
	void    CleanupContextVars(CoroutineInfo* ctx, int usedMax);  // 반납 전 BoundaryValue 참조 정리
    void PrepareCallStorage(int plan);
    void PrepareFunctionStorage(const SFunctionTable& function, int base, bool argumentsReady = false);
    void ReleaseFunctionStorage(const SFunctionTable& function, int base);
    void ReleaseStorageValue(ValueRef value, SlotKind kind);
    void CopyStorageReference(ValueRef target, ValueRef source, SlotKind kind);
    template <VAR_TYPE Type> void ReleaseKnownStorage(CNeoVM* vm, ValueRef value);
    template <VAR_TYPE Type> void CopyKnownStorage(CNeoVM* vm, ValueRef target, ValueRef source);
	// 이미 무효가 된 프레임의 closure는 sync하지 않는다. 실행 보유분만 반납한다.
	void    ReleaseDetachedClosureWithoutSync(ClosureInfo*& closure);
	// 오류로 실행을 폐기할 때, 컨텍스트의 실행 보유 closure 참조를 write-back
	// 없이 반납한다.
	void    ReleaseCoroutineClosuresWithoutSync(CoroutineInfo* pCI);
	void    UnwindActiveClosures();                    // return을 거치지 않는 error/cancel 정리
	void    ReleaseExecution(bool discardParkedClosuresWithoutSync = false); // 최상위 실행 컨텍스트를 풀로 반납
	int     RunSettle(bool retainCompletedResult = false);                                // Run() 후 완료/정지/에러 판정 (NeoExecStatus)
	void    PoisonOutOfMemory() noexcept { m_bOutOfMemoryPoisoned = true; }
	NEOS_NOINLINE bool ReportRunException();

    virtual void DebugSetListener(INeoVMDebugListener* listener) override;
    virtual void DebugSetBreakpoints(const std::vector<int>& lines) override;
    virtual void DebugSetBreakpoints(const std::vector<NeoDebugBreakpoint>& breakpoints) override;
    virtual void DebugContinue() override;
    virtual void DebugStepInto() override;
    virtual void DebugStepOver() override;
    virtual void DebugStepOut() override;
    virtual void DebugPause() override;
    virtual bool DebugIsPaused() override;
    virtual void DebugGetStackTrace(std::vector<NeoDebugStackFrame>& frames) override;
    virtual void DebugGetFrameVariables(int frameId, std::vector<NeoDebugVariable>& vars) override;
    virtual void DebugGetExecutableLocations(std::vector<NeoDebugLocation>& locations) override;

	bool	IsMainCoroutine(CoroutineInfo* p) { return (m_pMainCtx == p); }
	virtual bool	Setup(int iFunctionID, std::vector<BoundaryValue>& _args) override;
	virtual bool	Run() override;
	virtual int	ExecuteTop(int iFunctionID, std::vector<BoundaryValue>& _args) override;
	virtual int	ResumeTop(bool retainCompletedResult = false) override;
	virtual NeoExecutionState GetExecutionState() override;
	virtual bool IsOutOfMemoryPoisoned() const override { return m_bOutOfMemoryPoisoned; }
	virtual bool IsSuspended() override;
	virtual NeoHostCallBegin BeginHostCall() override;
	virtual void EndHostCall(NeoHostCallBegin begin) override;
	virtual void BeginNestedScriptCall() override;
	virtual void EndNestedScriptCall() override;
	virtual int RunHostCall(int iFunctionID, std::vector<BoundaryValue>& _args, ValueRef closureValue = nullptr, ValueRef completedResult = nullptr) override;
	virtual bool CancelExecution() override;
	virtual bool IsInterpreterRunning() const override { return m_bInRun; }

	template<bool TIMEOUT, bool DEBUG>
	bool	RunInternal(int iBreakingCallStack);

	bool	StopCoroutine(bool doDead = true);
	void	DeadCoroutine(CoroutineInfo* pCI);

	//BoundaryValue _intA1;
	//BoundaryValue _intA2, _intA3;
	BoundaryValue _funA3;

	NEOS_FORCEINLINE ValueRef GetVarPtrF1(const SVMOperation& OP)
	{
		if (OP.argFlag & NEOS_ARG_N1_LOCAL) return GetVarPtr_L(OP.n1);
		return NEOS_GLOBAL_VAR(OP.n1);
	}
	NEOS_FORCEINLINE ValueRef GetVarPtr2(const SVMOperation& OP)
	{
		if (OP.argFlag & NEOS_ARG_N2_LOCAL) return GetVarPtr_L(OP.n2);
		return NEOS_GLOBAL_VAR(OP.n2);
	}
	NEOS_FORCEINLINE ValueRef GetVarPtr3(const SVMOperation& OP)
	{
		if (OP.argFlag & NEOS_ARG_N3_LOCAL) return GetVarPtr_L(OP.n3);
		return NEOS_GLOBAL_VAR(OP.n3);
	}
	NEOS_FORCEINLINE ValueRef GetVarPtr_L(short n) { return m_pVarStack_Pointer.SlotOffset(n); }
	NEOS_FORCEINLINE ValueRef GetVarPtr_G(short n) { return NEOS_GLOBAL_VAR(n); }

	NEOS_FORCEINLINE void SetStackPointer(int n) { m_pVarStack_Pointer = &(*m_pVarStack_Base)[n]; }
	// base부터 lastOffset까지(반환값 슬롯은 offset 0) 접근 가능한지 확인한다.
	bool EnsureStackRange(int base, int lastOffset);
public:
	NEOS_FORCEINLINE CNeoVM* GetVM() { return _pVM;  }
	void EnableCooperativeSleep() { m_bCooperativeSleep = true; }
	bool IsNativeScriptCallActive() const { return m_iNativeScriptCallDepth > 0; }
	bool IsSynchronousExecutionLimited() const {
		return m_iTimeout >= 0 || (m_instructionBudget != 0 && m_budgetRestrictsNested);
	}
	virtual void SetInstructionBudget(uint64_t budget, bool restrictNested = true) override {
		m_instructionBudget = budget;
		m_budgetRestrictsNested = restrictNested;
	}
	virtual void SetTimeout(int iTimeout, int iCheckOpCount) override {
		m_iTimeout = iTimeout;
		m_iCheckOpCount = iCheckOpCount > 0 ? iCheckOpCount : NEO_DEFAULT_CHECKOP;
	}
private:



public:
	mRND m_sRand;

	virtual void Var_Move(ValueRef v1, ValueRef v2) override
	{
		Move(v1, v2);
	}
	void Move(ValueRef v1, ValueRef v2);
	void MoveTake(ValueRef v1, ValueRef v2);
	void MoveI(ValueRef v1, int v);
	void MoveF(ValueRef v1, int bits);


	void Swap(ValueRef v1, ValueRef v2);
private:

	// 산술 결과 저장. 대입 대상이 숫자/none 이면 바로 쓰고, 참조형이면 분리 함수에서 놓는다.
	// 해제 코드를 핫 경로에 인라인하면 MSVC 가 저장 블록을 멀리 흩어 놓아 실수 산술이 느려졌다(dumpbin 확인).
	void StoreNumberInt(ValueRef r, NS_INT v);
	void StoreNumberFloat(ValueRef r, NS_FLOAT v);
	void StoreNumberBool(ValueRef r, bool v);
	NEOS_NOINLINE void ReleaseBeforeNumber(ValueRef r);
	bool CheckIntegerDivision(NS_INT dividend, NS_INT divisor);
	bool CheckIntegerDivisionOverflow(NS_INT dividend);
	void DivI64(ValueRef r, ValueRef left, ValueRef right);
	bool CheckShiftCount(NS_INT count);


	bool CompareEQ(ValueRef v1, ValueRef v2);
	bool CompareGR(ValueRef v1, ValueRef v2, bool reverseDiagnostic = false);
	bool CompareGE(ValueRef v1, ValueRef v2, bool reverseDiagnostic = false);
	// 숫자 외 비교는 interpreter switch 안에 복제하지 않는다. 문자열/벡터/오류 처리는
	// 드문 경로라 noinline으로 두어 숫자 비교 opcode의 기계 코드가 안정적으로 유지된다.
	NEOS_NOINLINE bool CompareEQRare(ValueRef v1, ValueRef v2);
	NEOS_NOINLINE bool CompareGRRare(ValueRef v1, ValueRef v2, bool reverseDiagnostic);
	NEOS_NOINLINE bool CompareGERare(ValueRef v1, ValueRef v2, bool reverseDiagnostic);
	bool For(ValueRef v1);
	bool ForRare(ValueRef v1, NS_INT step);   // step != 1 (일반 양수 스텝·역방향·0) — For 의 인라인 본문을 step==1 로만 유지
	// bTwoVar: foreach(var k, v in ...) 형태(컴파일러가 op argFlag 로 전달).
	// list 는 2변수 순회를 지원하지 않으므로 런타임에 여기서 에러를 낸다.
	bool ForEach(ValueRef v1, ValueRef v2, bool bTwoVar);
	// string/set 순회와 미지원 타입. 본문이 커서 디스패치 루프 밖으로 뺀다.
	bool ForEachRare(ValueRef v1, ValueRef v2, bool bTwoVar);
	int Sleep(int iTimeout, ValueRef v1);
	void Call(FunctionPtr* fun, int n2, ValueRef pReturnValue = nullptr);
	void Call(int n1, int n2, int returnValueIndex = -1);
	void CallFunctionValue(int functionIndex, int argumentCount);
	void Call(ClosureInfo* closure, int n2, int returnValueIndex = -1);
	// return 값 슬롯은 Sync 뒤에도 caller가 읽으므로 보존한다. 나머지 캡처
	// 슬롯은 closure 보관함으로 소유권을 넘긴다.
	void SyncActiveClosure(ValueRef keepOnStack = nullptr);
	void SyncClosureAtFrame(ClosureInfo* closure, int stackBase, ValueRef keepOnStack = nullptr,
		ValueStack* varStack = nullptr);
	NEOS_NOINLINE void FinishClosureReturn(ValueRef keepOnStack);
	void DrainCoroutineClosures(CoroutineInfo* pCI); // close/cancel 시 실행 보유 closure를 동기화·반납

	bool CallNative(FunctionPtrNative functionPtrNative, void* pUserData, StringInfo *pStr, int n3, ValueRef pRet = nullptr);
	bool CallDefaultNativeByIndex(int nativeIndex, int n3, ValueRef pRet = nullptr);
	bool PropertyNative(FunctionPtrNative functionPtrNative, void* pUserData, StringInfo* pStr, ValueRef pRet, bool get,
        int* deferredTransferFrame = nullptr);
    bool FinishPropertyTransfer(int argSP_Vars);

	static std::string ToString(ValueRef v1);
	NS_INT ToInt(ValueRef v1);
	NS_FLOAT ToFloat(ValueRef v1);
	int ToSize(ValueRef v1);

	void CltInsert(ValueRef pClt, ValueRef pArray, ValueRef pValue, const SVMOperation* write = nullptr);
	void CommitRefVector(ValueRef value);
	void CltInsertRare(ValueRef pClt, ValueRef pArray, ValueRef pValue);
	void CltInsert(ValueRef pClt, int array, ValueRef v);
	void CltInsert(ValueRef pClt, ValueRef pArray, NS_INT v);
	void CltInsert(ValueRef pClt, NS_INT key, NS_INT v);
	void CltRead(ValueRef pClt, ValueRef pArray, ValueRef pValue);
	void CltReadRare(ValueRef pClt, ValueRef pArray, ValueRef pValue);
	void SetArrayAssignError(ArrayInfo* array, ValueRef value);
	void TableRemove(ValueRef pTable, ValueRef pArray);
	ValueRef GetTableItem(ValueRef pTable, ValueRef pArray);


	bool ChangeNumber(ValueRef p);


	//void ClearArgs()
	//{
	//	_args.clear();
	//}

public:
//	virtual ValueRef GetReturnVar() { return &(*m_pVarStack_Base)[_iSP_Vars]; }
	virtual ValueRef GetReturnVar() override { return m_pVarStack_Pointer; }
	virtual ValueRef GetStackVar(int idx) override{ return GetStack (idx); }
	virtual bool ResetVarType(ValueRef p, VAR_TYPE type, int capa) override;

	virtual void GC() override
	{
		for (int i = _iSP_Vars + 1; i < _iSP_Vars_Max2; i++)
			Var_Release(&(*m_pVarStack_Base)[i]);
		_iSP_Vars_Max2 = _iSP_Vars;
	}

	NEOS_FORCEINLINE ValueRef GetStack(int idx) { return m_pVarStack_Pointer + idx; }
	NEOS_FORCEINLINE ValueRef GetStackFromBase(int idx) { return &(*m_pVarStack_Base)[idx]; }

	template<typename T>
	T read(int idx) { T r; _read(m_pVarStack_Pointer + idx, r); return r; }






	virtual ValueRef GetVar(const std::string& name) override
	{
		int idx = _pProgram->FindGlobalVar(name);
		if (!_pProgram->IsGlobalVariable(idx))
			return nullptr;
		return NEOS_GLOBAL_VAR(idx);
	}

	ValueRef testCall(int iFID, ValueRef args, int argc);
	ValueRef testCall(ValueRef function, ValueRef args, int argc);
	bool StartCoroutione(int argSP_Vars, int n3);

	void SetError(const char* pErrMsg);
	void RememberErrorLocation();
	bool IsGeneratedHelperOp(int opIndex) const;
	int UserErrorOpIndex(int opIndex) const;
	NeoDebugLocation GetLastErrorLocation() const;
	void SetErrorUnsupport(const char* pErrMsg, ValueRef p);
	void SetErrorFormat(const char* pErrMsg, ...);
	// 런타임 에러 테이블(NeoVMError.h) 기반. 새 코드는 이쪽을 쓴다.
	void SetError(ENeoRuntimeError e);
	void SetErrorFormat(int error, ...);
	void SetErrorOperator(const char* op, ValueRef v1, ValueRef v2 = nullptr);
public:
	CNeoVMWorker(CNeoVM* pVM, u32 id, int iStackSize);
	virtual ~CNeoVMWorker();

	// pProgram 은 호출측이 소유권 지분을 넘기지 않는다. 성공 시 워커가 AddRef 한다.
	bool Init(const NeoLoadVMParam* vparam, CNeoVMProgram* pProgram, int iStackSize);

	NEOS_FORCEINLINE static void Var_AddRef(ValueRef d)
	{
		switch (d.GetType())
		{
		case VAR_STRING:
			++d->_str->_refCount;
			break;
		case VAR_VEC3:
		case VAR_VEC4:
			// 짝인 Var_ReleaseInternal / Move_DestNoRelease 는 처리하는데 여기만 빠져 있었다.
			// alloc 타입은 하나도 빠짐없이 여기 있어야 한다.
			++d->_vec->_refCount;
			break;
		case VAR_FP_NATIVE:
			++d->_fpNative->_refCount;
			break;
		case VAR_ARRAY:
			++d->_array->_refCount;
			break;
		case VAR_MAP:
			++d->_tbl->_refCount;
			break;
		case VAR_LIST:
			++d->_lst->_refCount;
			break;
		case VAR_SET:
			++d->_set->_refCount;
			break;
		case VAR_COROUTINE:
			++d->_cor->_refCount;
			break;
		case VAR_MODULE:
			++((CNeoVMWorker*)(d->_module))->_refCount;
			break;
		case VAR_ASYNC:
			++d->_async->_refCount;
			break;
		case VAR_CLOSURE:
			++d->_closure->_refCount;
			break;
		default:
			break;
		}
	}

#include "NeoVMWorker_Handlers.inl"
#include "NeoVMWorker_Typed.inl"
#include "NeoVMWorker_Fusion.inl"

	// 파괴 재진입 방지와 순환 후보 intrusive FIFO 링크. 전역/실행 컨텍스트가
	// 컨테이너를 들 수 있으므로 worker는 보수적으로 후보 가능으로 초기화한다.
	CycleState<CNeoVMWorker> _cycleState;

};

extern std::string GetDataType(VAR_TYPE t);

// INeoVMWorker 인라인 정의. 호출하는 모든 TU 에 정의가 보여야 한다(파일 상단 주석 참고).
#include "NeoVMWorker_Var.inl"

// NeoLib.cpp 에서도 직접 호출하는 concrete worker fast path. .cpp 전용 .inl 에 두면
// GCC/Clang 비-LTO 빌드에서 inline 정의를 찾지 못해 undefined reference가 난다.
#include "NeoVMWorker_Move.inl"
};
