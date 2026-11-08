#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <stdarg.h>
#include <new>
#include <thread>
#include <chrono>
#include <unordered_set>
#include "NeoVMInternal.h"
#include "NeoVMWorker.h"
#include "NeoArchive.h"
#include "UTFString.h"

namespace NeoScript
{


void	SetCompileError(const char*	lpszString, ...);


// 네이티브가 보조 스크립트 프레임을 직접 실행할 때, 호출자 람다의 캡처를
// 자식 RET가 동기화하거나 반납하지 못하게 한다. 이 포인터는 실행 보유 참조를
// 나타내므로 AddRef/Release 없이 실행 범위에서만 분리한다.
class ActiveClosureIsolation
{
	ClosureInfo*& _active;
	ClosureInfo* const _saved;
public:
	explicit ActiveClosureIsolation(ClosureInfo*& active) : _active(active), _saved(active)
	{
		_active = nullptr;
	}
	~ActiveClosureIsolation()
	{
		_active = _saved;
	}
};

// 네이티브 콜백이 예외로 끝나도 클로저 정리는 원래 스크립트 프레임을 읽어야 한다.
// 임시 값의 해제는 여기서 하지 않는다. 예외 경로에서는 기존 상한을 유지해 실행 정리가 맡는다.
class CNeoVMWorker::NativeFrameScope
{
	CNeoVMWorker& _worker;
	CoroutineInfo* const _context;
	ValueStack* const _stack;
	const int _sp;
	const int _max;
	const int _breaking;
	const int _function;
	const CallStoragePlan* const _plan;
	bool _restored = false;
public:
	explicit NativeFrameScope(CNeoVMWorker& worker)
		: _worker(worker), _context(worker.m_pCur), _stack(worker.m_pVarStack_Base),
		_sp(worker._iSP_Vars), _max(worker._iSP_VarsMax), _breaking(worker.m_iBreakingCallStack),
		_function(worker._functionIndex), _plan(worker._activePlan)
	{
		// 네이티브가 스크립트를 다시 부르면 이 프레임과 호출 창은 콜스택에 없다.
		// 오류 정리가 소유 슬롯을 찾도록 컨텍스트에 기록하고, 복원 때 걷어낸다.
		if (_context != nullptr)
		{
			SNativeFrame& record = _context->m_sNativeFrames.push_back();
			record._iSP_Vars = _sp;
			record._functionIndex = _function;
			record._window = _max;
			record._plan = _plan;
		}
	}
	bool IsCurrent() const
	{
		return _worker.m_pCur == _context && _worker.m_pVarStack_Base == _stack;
	}
	void Restore()
	{
		if (_restored) return;
		_restored = true;
		// 중첩 호출이 실행 컨텍스트를 폐기했다면 이전 스택과 레지스터를 되살리지 않는다.
		if (!IsCurrent()) return;
		// 오류 정리가 먼저 비웠을 수 있다.
		if (_context != nullptr && _context->m_sNativeFrames.size() > 0) _context->m_sNativeFrames.pop_back();
		_worker._iSP_Vars = _sp;
		_worker.SetStackPointer(_sp);
		_worker._iSP_VarsMax = _max;
		_worker.m_iBreakingCallStack = _breaking;
		// 중첩 스크립트 호출이 바꾼 프레임 배치와 호출 창 계획을 되돌린다.
		if (_function >= 0) _worker.SelectFunction(_function);
		else _worker._functionIndex = -1;
		_worker._activePlan = _plan;
	}
	~NativeFrameScope() { Restore(); }
};

void SVarWrapper::SetNone() { _vmw->Var_SetNone(_var); }
void SVarWrapper::SetInt(int v) { _vmw->Var_SetInt(_var, v); }
void SVarWrapper::SetFloat(NS_FLOAT v) { _vmw->Var_SetFloat(_var, v); }
void SVarWrapper::SetBool(bool v) { _vmw->Var_SetBool(_var, v); }
void SVarWrapper::SetString(const char* str) { _vmw->Var_SetString(_var, str); }
//void SVarWrapper::SetTableFun(FunctionPtrNative fun) { _vmw->Var_SetTableFun(_var, fun); }

#include "NeoVMWorker.inl"

// 벡터 세터 (native 호출 내부에서만 쓰이므로 non-inline. NeoLib.cpp 에서 링크됨)
//
void INeoVMWorker::Var_SetVec2(ValueRef d, float x, float y)
{
	StoreVector(d, 2, x, y, 0, 0);
}
void INeoVMWorker::Var_SetVec3(ValueRef d, float x, float y, float z)
{
	StoreVector(d, 3, x, y, z, 0);
}
void INeoVMWorker::Var_SetVec4(ValueRef d, float x, float y, float z, float w)
{
	VecInfo* p = VecStoreFor(d, 4);
	p->v[0] = x; p->v[1] = y; p->v[2] = z; p->v[3] = w;
}
void INeoVMWorker::Var_SetQuat(ValueRef d, float w, float x, float y, float z)
{
	VecInfo* p = VecStoreFor(d, 4);
	p->v[0] = w; p->v[1] = x; p->v[2] = y; p->v[3] = z;
}

int& GetModuleRefCount(ValueRef p)
{
	return ((CNeoVMWorker*)(p->_module))->_refCount;
}

CNeoVMWorker::CNeoVMWorker(CNeoVM* pVM, u32 id, int iStackSize)
{
	_pVM = pVM;
	_idWorker = id;
	//SetCodeData(pVM->_pCodePtr, pVM->_iCodeLen);
	m_pVarGlobal = &m_sVarGlobal;
	m_pVarGlobal_Pointer = nullptr;

	// 실행 스택은 더 이상 워커가 소유하지 않는다. 실행 시 풀에서 컨텍스트를 대여해 바인딩한다.
	m_pCur = nullptr;
	m_pMainCtx = nullptr;
	SetVarStackBase(nullptr);
	m_pCallStack = nullptr;
	m_pClosureCallStack = nullptr;
	m_pVarStack_Pointer = nullptr;

	ClearSP();

	_funA3.SetType(VAR_FUN);
	(void)iStackSize;
}
CNeoVMWorker::~CNeoVMWorker()
{
	// 이 워커가 주인인 완료 async 를 먼저 폐기한다. 남겨두면 아무도 pop 하지 않아
	// 큐와 _LockReferance 가 VM 종료까지 유지된다(누수).
	GetVM()->DiscardAsyncByWorker(GetWorkerID(), _asyncPendingCount);

	// 정지 상태로 남은(retain 된) 최상위 실행 컨텍스트를 풀로 반납.
	if (m_pMainCtx != nullptr)
		ReleaseExecution();
	Var_Release(&m_sTestCallResult);

	if (_pProgram && m_pVarGlobal_Pointer)
	{
		for (const SlotLayout& slot : _pProgram->globalStorage)
			ReleaseByPolicy(m_pVarGlobal_Pointer + slot.slot, PolicyOf(slot.kind));
		for (int i = -1; i < _pProgram->header._iStaticVarCount; ++i)
			Var_Release(ValueRef(m_pVarGlobal_Pointer + i, m_pGlobalTypes + i));
	}
	m_sVarGlobal.clear();
	m_pVarGlobal_Pointer = nullptr;

	_pCodeBegin = nullptr;
	_pCodeCurrent = nullptr;
	// 코드는 프로그램 소유. 이 워커가 마지막 참조면 여기서 해제된다.
	// (디버거에 정지한 워커가 살아있는 동안 핫 리로드된 옛 이미지가 유지되는 이유)
	if (_pProgram != nullptr)
	{
		_pProgram->Release();
		_pProgram = nullptr;
	}
}



// -1 : Error
//  0 : SlieceRun
//  1 : Continue
//  2 : Timeout - 
int CNeoVMWorker::Sleep(int iTimeout, ValueRef v1)
{
	int iSleepTick;
	switch (v1.GetType())
	{
	case VAR_INT:
		if (v1->_int > INT32_MAX) { SetError(RTE_SLEEP_VALUE); return -1; }
		iSleepTick = v1->_int < 0 ? 0 : static_cast<int>(v1->_int);
		break;
	case VAR_FLOAT:
		if (!std::isfinite(v1->_float) || double(v1->_float) > INT32_MAX)
		{ SetError(RTE_SLEEP_VALUE); return -1; }
		iSleepTick = v1->_float < 0 ? 0 : static_cast<int>(v1->_float);
		break;
	default:
		SetError(RTE_SLEEP_VALUE);
		return -1;
	}
	if (iSleepTick < 0)
		iSleepTick = 0;

	if (iTimeout >= 0 || m_bCooperativeSleep)
	{
		m_bCooperativeSleepPending = m_bCooperativeSleep;
		_iRemainSleep = iSleepTick;
		_sleepClock = std::chrono::steady_clock::now();
		return 0;
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(iSleepTick));
	return 1;
}

//#ifdef NS_SINGLE_PRECISION
//	#define FLOAT_FORMAT	"%f"
//#else
//	#define FLOAT_FORMAT	"%lf"
//#endif

#ifdef NS_SINGLE_PRECISION
	#define FLOAT_FORMAT	"%g"
#else
	#define FLOAT_FORMAT	"%g"
#endif


std::string CNeoVMWorker::ToString(ValueRef v1)
{
	char ch[256];
	switch (v1.GetType())
	{
	case VAR_INT:
	{
		char* end = ch + sizeof(ch);
		char* begin = end;
		NS_UINT magnitude = v1->_int < 0 ? NS_UINT(0) - NS_UINT(v1->_int) : NS_UINT(v1->_int);
		do { *--begin = char('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
		if (v1->_int < 0) *--begin = '-';
		return std::string(begin, end);
	}
	case VAR_FLOAT:
		
#ifdef _WIN32
		snprintf(ch, _countof(ch), FLOAT_FORMAT, v1->_float);
#else
		sprintf(ch, FLOAT_FORMAT, v1->_float);
#endif
		return ch;
	case VAR_BOOL:
		return v1->_bl ? "true" : "false";
	case VAR_NONE:
		return "null";
	case VAR_FUN:
	case VAR_FUN_NATIVE:
		return "function";
	case VAR_VEC:
	{
		const int n = v1.VectorComponentCount();
		if (n <= 2)      snprintf(ch, sizeof(ch), "(%g, %g)", v1.VectorComponent(0), v1.VectorComponent(1));
		else if (n == 3) snprintf(ch, sizeof(ch), "(%g, %g, %g)", v1.VectorComponent(0), v1.VectorComponent(1), v1.VectorComponent(2));
		else             snprintf(ch, sizeof(ch), "(%g, %g, %g, %g)", v1.VectorComponent(0), v1.VectorComponent(1), v1.VectorComponent(2), v1.VectorComponent(3));
		return ch;
	}
	case VAR_STRING:
		return v1->_str->_str;
	case VAR_VEC3:
	case VAR_VEC4:
	{
		const int n = v1.VectorComponentCount();
		if (n <= 2)      snprintf(ch, sizeof(ch), "(%g, %g)", v1.VectorComponent(0), v1.VectorComponent(1));
		else if (n == 3) snprintf(ch, sizeof(ch), "(%g, %g, %g)", v1.VectorComponent(0), v1.VectorComponent(1), v1.VectorComponent(2));
		else             snprintf(ch, sizeof(ch), "(%g, %g, %g, %g)", v1.VectorComponent(0), v1.VectorComponent(1), v1.VectorComponent(2), v1.VectorComponent(3));
		return ch;
	}
	case VAR_FP_NATIVE:
		return "native_object";
	case VAR_ARRAY:
		return "array";
	case VAR_MAP:
		return "map";
	case VAR_LIST:
		return "list";
	case VAR_SET:
		return "set";
	case VAR_COROUTINE:
		return "coroutine";
	case VAR_MODULE:
		return "module";
	case VAR_ASYNC:
		return "async";
	case VAR_CLOSURE:
		return "function";
	default:
		break;
	}
	return "null";
}
NS_INT CNeoVMWorker::ToInt(ValueRef v1)
{
	switch (v1.GetType())
	{
	case VAR_INT:
		return v1->_int;
	case VAR_FLOAT:
		return NeoFloatToInt(v1->_float);
	case VAR_BOOL:
		return v1->_bl ? 1 : 0;
	case VAR_NONE:
		return -1;
	case VAR_STRING:
		return std::strtoll(v1->_str->_str.c_str(), nullptr, 10);
	case VAR_FP_NATIVE:
		return -1;
	case VAR_ARRAY:
		return -1;
	case VAR_MAP:
		return -1;
	default:
		break;
	}
	return -1;
}
NS_FLOAT CNeoVMWorker::ToFloat(ValueRef v1)
{
	switch (v1.GetType())
	{
	case VAR_INT:
		return (NS_FLOAT)v1->_int;
	case VAR_FLOAT:
		return v1->_float;
	case VAR_BOOL:
		return v1->_bl ? (NS_FLOAT)1 : (NS_FLOAT)0;
	case VAR_NONE:
		return -1;
	case VAR_STRING:
		return (NS_FLOAT)atof(v1->_str->_str.c_str());
	case VAR_FP_NATIVE:
		return -1;
	case VAR_ARRAY:
		return -1;
	case VAR_MAP:
		return -1;
	default:
		break;
	}
	return -1;
}
int CNeoVMWorker::ToSize(ValueRef v1)
{
	switch (v1.GetType())
	{
	case VAR_INT:
		return 0;
	case VAR_FLOAT:
		return 0;
	case VAR_BOOL:
		return 0;
	case VAR_NONE:
		// null 참조의 길이는 0 이 아니라 오류다. 정적 프런트엔드가 len() 을 이 명령으로 내린다.
		SetError(RTE_LENGTH_NULL);
		return 0;
	case VAR_VEC:
		return v1.VectorComponentCount();
	case VAR_STRING: // UTF-8 문자 수(string.len()과 동일)
		return v1->_str->_StringLen;
	case VAR_VEC3:
	case VAR_VEC4:
		return v1.VectorComponentCount();
	case VAR_FP_NATIVE:
		return 0;
	case VAR_ARRAY:
		return v1->_array->_count;
	case VAR_MAP:
		return (int)v1->_tbl->_itemCount;
	case VAR_LIST:
		return (int)v1->_lst->_itemCount;
	case VAR_SET:
		return (int)v1->_set->_itemCount;
	default:
		break;
	}
	return 0;
}
void CNeoVMWorker::Call(FunctionPtr* fun, int n2, VarInfo* pReturnValue, bool keepResult)
{
	if (!EnsureStackRange(_iSP_VarsMax, n2))
		return;

	if (_iSP_Vars_Max2 < _iSP_VarsMax + (1 + n2))
		_iSP_Vars_Max2 = _iSP_VarsMax + (1 + n2);

	if (fun->_func == nullptr)
	{	// Error
		SetError(RTE_CALL_NULL);
		return;
	}
	NativeFrameScope frame(*this);
	const CallStoragePlan* plan = _activePlan;

	_iSP_Vars = _iSP_VarsMax;
	SetStackPointer(_iSP_Vars);
	VarInfo* window = m_pVarStack_Pointer;

	if ((*fun->_fn)(this, fun, n2) < 0)
	{
		// SetError 는 오류 opcode로 점프시키므로 코드 포인터는 복구하지 않는다.
		// 다만 native 호출 프레임으로 옮긴 스택 레지스터는 부모 프레임으로 되돌려야 한다.
		frame.Restore();
		if (GetVM()->IsLastErrorMsg())
			SetCodePtr(0);
		else
			SetError(RTE_CALL_ARG_COUNT);
		return;
	}
	if (!frame.IsCurrent())
		return;
	// 창은 호출이 끝나면 비어 있어야 한다. 결과는 소유권째 옮기고 인자는 계획의 정책으로 놓는다.
	// 결과를 뒤따르는 MOV가 가져가는 호출은 창 0번만 남긴다.
	if (plan != nullptr)
	{
		if (pReturnValue != nullptr) TakeByPolicy(pReturnValue, window, static_cast<StoragePolicy>(plan->policies[0]));
		ReleaseCallWindow(*plan, window, pReturnValue == nullptr && !keepResult);
	}
	frame.Restore();
	_activePlan = nullptr;
}

// CNeoVMWorker::Call(int,int,int) 는 NeoVMWorker_Move.inl 로 옮겼다(핫패스 인라인 실험).

bool CNeoVMWorker::Init(const NeoLoadVMParam* vparam, CNeoVMProgram* pProgram, int iStackSize)
{
	if (pProgram == nullptr)
		return false;

	// 실행 컨텍스트 풀: 명시 주입 우선, 없으면 VM(임플) 기본 풀 상속(모듈 로드 워커 등).
	if (vparam != nullptr && vparam->execPool != nullptr)
		m_pPool = vparam->execPool;
	else
		m_pPool = GetVM()->GetExecPool();

	pProgram->AddRef();
	_pProgram = pProgram;
	_BytesSize = pProgram->byteSize;

	// 코드는 프로그램 소유(패치 완료, 불변). 워커는 IP 만 갖는다.
	_pCodeBegin = pProgram->GetCodeBegin();
	_pCodeCurrent = (const SVMOperation*)_pCodeBegin;

	// 전역 슬롯 = [역순 전역 변수 | -1 예약 | static 상수]. 기준 포인터는 상수 0.
	// 전역은 음수, 작은 상수는 양수 16-bit offset으로 분기 없이 접근한다.
	// 상수 개수가 늘어도 전역 번호/한도는 바뀌지 않는다. static 은 프로그램 서술자에서
	// 이 VM 의 할당자로 실체화한다 (StringInfo 는 VM 별 풀 소유라 공유 불가).
	const int staticCount = pProgram->header._iStaticVarCount;
	const int iMaxVar = pProgram->GetGlobalSlotCount();
	m_sVarGlobal.clear();
	m_sVarGlobal.resize(iMaxVar);
	m_pVarGlobal_Pointer = m_sVarGlobal.data() + pProgram->ConstantBase();
	// 전역 뷰가 원소를 바로 가리키는 읽기 전용 사본이다. 값으로 쓸 때는 SlotType_G가 비트를 벗긴다.
	m_pGlobalTypes = pProgram->globalTypesFlagged.data() + pProgram->ConstantBase();
	// 전역은 모두 0(숫자 0, null 참조)에서 시작한다. 선언 초기화는 전역 초기화 함수가 한다.

	for (int i = 0; i < staticCount; i++)
	{
		const SStaticConst& sc = pProgram->staticValues[i];
		ValueRef vi(m_pVarGlobal_Pointer + i, sc._type);
		switch (sc._type)
		{
		case VAR_INT:
			vi->_int = sc._int;
			break;
		case VAR_FLOAT:
			vi->_float = sc._float;
			break;
		case VAR_BOOL:
			vi->_bl = sc._bl;
			break;
		case VAR_STRING:
			vi->_str = GetVM()->StringIntern(sc._str);
			++vi->_str->_refCount;
			break;
		default:
			vi.ClearType();
			SetError(RTE_INVALID_VAR_TYPE);
			return false;
		}
	}

	if(vparam && vparam->NeoGlobalInterface)
	{
		vparam->NeoGlobalInterface(this, vparam->param);
	}
	return true;
}
int CNeoVMWorker::GetDebugLine(int iOPIndex)
{
//	int idx = int((u8*)_pCodeCurrent - _pCodeBegin - 1) / sizeof(SVMOperation);
	const std::vector<debug_info>& dbg = DebugData();
	if ((int)dbg.size() <= iOPIndex || iOPIndex < 0) return -1;
	return dbg[iOPIndex]._lineseq;
}
int CNeoVMWorker::GetFunctionIndexFromCodeOffset(int codeOffset)
{
	int iFind = -1;
	int iFindCodePtr = -1;
	const std::vector<SFunctionTable>& funs = Functions();
	for (int i = 0; i < (int)funs.size(); ++i)
	{
		int codePtr = funs[i]._codePtr;
		if (codePtr <= codeOffset && codePtr >= iFindCodePtr)
		{
			iFind = i;
			iFindCodePtr = codePtr;
		}
	}
	return iFind;
}
std::string CNeoVMWorker::FormatStackTrace(int currentOpIndex)
{
	// 오류 보고가 다시 대량 할당을 일으키지 않도록 총 64 프레임까지만 보인다.
	// 현재 프레임을 포함한 앞 32개와 가장 오래된 뒤 32개를 남겨 재귀의 양 끝을
	// 모두 볼 수 있게 한다.
	static const int kMaxFrames = 64;
	static const int kHeadFrames = 32;
	static const int kTailFrames = kMaxFrames - kHeadFrames;

	std::string trace;
	trace.reserve(512);
	trace += "\nScript Call Stack:";
	int visibleFrame = 0;

	auto appendFrame = [&](int, int opIndex, int stackBase)
	{
		if (IsGeneratedHelperOp(opIndex)) return;
		int functionId = GetFunctionIndexFromCodeOffset(opIndex * (int)sizeof(SVMOperation));
		std::string functionName;
		if (functionId >= 0)
		{
			auto itName = _pProgram->debugFunctionNames.find(functionId);
			if (itName != _pProgram->debugFunctionNames.end())
				functionName = itName->second;
		}
		if (functionName.empty())
		{
			functionName = "function#";
			functionName += std::to_string(functionId);
		}

		int file = -1;
		int line = -1;
		const std::vector<debug_info>& dbg = DebugData();
		if (opIndex >= 0 && opIndex < (int)dbg.size())
		{
			file = dbg[opIndex]._fileseq;
			line = dbg[opIndex]._lineseq;
		}

		trace += "\n  #";
		trace += std::to_string(visibleFrame++);
		trace += " ";
		trace += functionName;
		trace += " : IP(";
		trace += std::to_string(opIndex);
		trace += "), File(";
		trace += std::to_string(file);
		trace += "), Line(";
		trace += std::to_string(line);
		trace += "), SP(";
		trace += std::to_string(stackBase);
		trace += ")";

		if (functionId >= 0 && functionId < (int)Functions().size())
		{
			const SFunctionTable& fun = Functions()[functionId];
			trace += ", Args[";
			for (int argIndex = 0; argIndex < fun._argsCount; ++argIndex)
			{
				if (argIndex > 0)
					trace += ", ";

				int varIndex = stackBase + 1 + argIndex;
				std::string argName;
				auto itFunNames = _pProgram->debugVarNames.find(functionId);
				if (itFunNames != _pProgram->debugVarNames.end())
				{
					auto itName = itFunNames->second.find(1 + argIndex);
					if (itName != itFunNames->second.end())
						argName = itName->second;
				}
				if (argName.empty())
				{
					argName = "arg";
					argName += std::to_string(argIndex + 1);
				}

				trace += argName;
				trace += ":";
				if (varIndex >= 0 && varIndex < (int)m_pVarStack_Base->size())
					trace += GetDataType(ValueRef(&(*m_pVarStack_Base)[varIndex], fun._slotTypes[1 + argIndex]).GetType());
				else
					trace += "out_of_range";
			}
			trace += "]";
		}
	};

	if (currentOpIndex < 0)
		currentOpIndex = 0;
	appendFrame(0, currentOpIndex, _iSP_Vars);

	if (m_pCallStack == nullptr)
		return trace;

	const int callFrameCount = (int)m_pCallStack->size();
	const int totalFrameCount = callFrameCount + 1; // 현재 실행 프레임 포함
	const int headCallFrameCount = kHeadFrames - 1;
	if (totalFrameCount <= kMaxFrames)
	{
		for (int i = callFrameCount - 1; i >= 0; --i)
		{
			SCallStack& cs = (*m_pCallStack)[i];
			int opIndex = (int)(cs._ReturnIP & IP_MASK) / (int)sizeof(SVMOperation);
			if (opIndex > 0)
				--opIndex;
			appendFrame(callFrameCount - i, opIndex, cs._iSP_Vars);
		}
		return trace;
	}

	for (int i = callFrameCount - 1; i >= callFrameCount - headCallFrameCount; --i)
	{
		SCallStack& cs = (*m_pCallStack)[i];
		int opIndex = (int)(cs._ReturnIP & IP_MASK) / (int)sizeof(SVMOperation);
		if (opIndex > 0)
			--opIndex;
		appendFrame(callFrameCount - i, opIndex, cs._iSP_Vars);
	}

	const int omitted = totalFrameCount - kMaxFrames;
	trace += "\n  ... ";
	trace += std::to_string(omitted);
	trace += " frames omitted";

	for (int i = kTailFrames - 1; i >= 0; --i)
	{
		SCallStack& cs = (*m_pCallStack)[i];
		int opIndex = (int)(cs._ReturnIP & IP_MASK) / (int)sizeof(SVMOperation);
		if (opIndex > 0)
			--opIndex;
		appendFrame(callFrameCount - i, opIndex, cs._iSP_Vars);
	}
	return trace;
}
bool CNeoVMWorker::CheckDebugStop(int iOPIndex)
{
    if (m_eDebugRunMode == DBG_PAUSED) return true;
    if (!IsDebugInfo() || iOPIndex < 0 || iOPIndex >= static_cast<int>(DebugData().size())) return false;
    const debug_info& info = DebugData()[iOPIndex];
    const int file = info._fileseq, line = info._lineseq;
    if (line <= 0) return false;
    const int depth = m_pCallStack ? static_cast<int>(m_pCallStack->size()) : 0;
    const bool sameContext = m_pCur == m_pDebugStepContext;
    if (m_bDebugPauseRequested) { StopDebug(iOPIndex, NEO_DEBUG_STOP_PAUSE); return true; }

    // 자식 중단점이 부모 호출 줄의 기록을 덮지 않는다. 복귀한 프레임 기록만 버린다.
    PruneDebugLineSkips(m_pCur, depth);
    for (auto it = m_sDebugLineSkips.begin(); it != m_sDebugLineSkips.end(); ++it)
    {
        if (it->context != m_pCur || it->depth != depth) continue;
        const bool originalOp = iOPIndex == it->stoppedOp;
        it->stoppedOp = -1;
        const bool sameLine = file == it->file && line == it->line;
        if (sameLine && (originalOp || iOPIndex > it->lastOp))
        {
            it->lastOp = iOPIndex;
            return false;
        }
        // 다른 줄이나 역방향 점프에 도달하면 다음 반복의 중단점을 다시 허용한다.
        m_sDebugLineSkips.erase(it);
        break;
    }
    // 한 단계 건너뛰기 중에도 자식 함수·코루틴의 명시적 중단점은 유효하다.
    if (m_iDebugBreakCount > 0 && IsDebugBreakpoint(file, line))
    { StopDebug(iOPIndex, NEO_DEBUG_STOP_BREAKPOINT); return true; }

    bool step = m_eDebugRunMode == DBG_STEP_INTO;
    if (m_eDebugRunMode == DBG_STEP_OVER) step = sameContext && depth <= m_iDebugStepDepth;
    if (m_eDebugRunMode == DBG_STEP_OUT)
        // 중첩 함수의 yield는 반환이 아니다. 코루틴 진입 함수에서만 재개자 복귀를 인정한다.
        step = (sameContext && depth < m_iDebugStepDepth) ||
            (m_iDebugStepDepth == 0 && !sameContext && m_pDebugStepReturnContext && m_pCur == m_pDebugStepReturnContext);
    if (step) { StopDebug(iOPIndex, NEO_DEBUG_STOP_STEP); return true; }
    return false;
}

void CNeoVMWorker::StopDebug(int iOPIndex, NeoDebugStopReason reason)
{
	const debug_info& info = DebugData()[iOPIndex];
	m_sDebugLocation.opIndex = iOPIndex;
	m_sDebugLocation.file = info._fileseq;
	m_sDebugLocation.line = info._lineseq;
	m_sDebugLocation.callDepth = m_pCallStack ? (int)m_pCallStack->size() : 0;
	m_bDebugFaulted = (reason == NEO_DEBUG_STOP_EXCEPTION);
	m_bDebugPauseRequested = false;
	m_eDebugRunMode = DBG_PAUSED;
	if (m_pDebugListener)
	{
		// 실행 프레임은 실제 IP를 유지하고, 예외 알림 위치만 사용자 호출식으로 보고한다.
		const NeoDebugLocation reported = reason == NEO_DEBUG_STOP_EXCEPTION ? GetLastErrorLocation() : m_sDebugLocation;
		m_pDebugListener->OnNeoDebugStopped(this, reported, reason);
	}
}
void CNeoVMWorker::ResetFaultStateForNewExecution()
{
    m_pDebugStepContext = m_pDebugStepReturnContext = nullptr;
    m_sDebugLineSkips.clear();
	m_bDebugFaulted = false;
	// 에러 위치는 실행 단위로 리셋해야 한다. SetError 는 _isErrorOPIndex 가 0 일 때만 기록하므로
	// (= 가장 안쪽 원인 보존), 여기서 안 지우면 다음 실행의 에러가 이전 에러의 함수/라인/스택으로
	// 보고된다. 이 함수는 새 실행 시작 지점(ExecuteTop/BeginHostCall)에서만 불린다.
	_isErrorOPIndex = 0;
	m_iLastErrorOPIndex = -1;
	m_iLastErrorSourceOPIndex = -1;
}
const char* g_sNeoRuntimeErrors[RTE_COUNT] =
{
#define X(name, message) message,
	VM_RUNTIME_ERROR_LIST(X)
#undef X
};

void CNeoVMWorker::SetError(const char* pErrMsg)
{
	RememberErrorLocation();
	SetCodePtr(0); // Jump To Error
	GetVM()->SetError(std::string(pErrMsg));
}
void CNeoVMWorker::RememberErrorLocation()
{
	if (m_iLastErrorOPIndex >= 0)
	{
		// 중첩 오류 뒤 바깥 native가 다시 실패해도 최초 원인 IP를 덮지 않는다.
		if (_isErrorOPIndex == 0) _isErrorOPIndex = m_iLastErrorOPIndex;
		return;
	}
	if (_isErrorOPIndex == 0 && _pCodeBegin != nullptr && _pCodeCurrent != nullptr)
		_isErrorOPIndex = int((const u8*)_pCodeCurrent - _pCodeBegin - 1) / sizeof(SVMOperation);
	m_iLastErrorOPIndex = _isErrorOPIndex;
	m_iLastErrorSourceOPIndex = UserErrorOpIndex(_isErrorOPIndex);
}
bool CNeoVMWorker::IsGeneratedHelperOp(int opIndex) const
{
	return _pProgram != nullptr && opIndex >= 0 && opIndex < (int)DebugData().size()
		&& DebugData()[opIndex]._lineseq == 0;
}
int CNeoVMWorker::UserErrorOpIndex(int opIndex) const
{
	if (!IsGeneratedHelperOp(opIndex) || m_pCallStack == nullptr) return opIndex;
	for (int i = (int)m_pCallStack->size() - 1; i >= 0; --i)
	{
		const int returnIP = (int)((*m_pCallStack)[i]._ReturnIP & IP_MASK);
		const int caller = returnIP / (int)sizeof(SVMOperation) - 1;
		if (caller >= 0 && !IsGeneratedHelperOp(caller)) return caller;
	}
	return opIndex;
}
NeoDebugLocation CNeoVMWorker::GetLastErrorLocation() const
{
	NeoDebugLocation location;
	location.opIndex = m_iLastErrorSourceOPIndex;
	if (_pProgram == nullptr || location.opIndex < 0 || location.opIndex >= (int)DebugData().size())
		return location;
	const debug_info& info = DebugData()[location.opIndex];
	location.file = info._fileseq;
	location.line = info._lineseq;
	return location;
}
void CNeoVMWorker::SetError(ENeoRuntimeError e)
{
	SetError(g_sNeoRuntimeErrors[e]);
}
void CNeoVMWorker::SetErrorFormat(int error, ...)
{
	char buff[1024];
	va_list ap;
	va_start(ap, error);
	vsnprintf(buff, sizeof(buff), g_sNeoRuntimeErrors[error], ap);
	va_end(ap);
	buff[sizeof(buff) - 1] = '\0';
	SetError(buff);
}
// 연산자 에러 전용. 피연산자 타입까지 붙여 "invalid operator '+' for 'string' and 'map'" 형태로 만든다.
void CNeoVMWorker::SetErrorOperator(const char* op, ValueRef v1, ValueRef v2)
{
	char operands[256];
	if (v2 == nullptr)
		snprintf(operands, sizeof(operands), "'%s'", GetDataType(v1.GetType()).c_str());
	else
		snprintf(operands, sizeof(operands), "'%s' and '%s'",
			GetDataType(v1.GetType()).c_str(), GetDataType(v2.GetType()).c_str());
	SetErrorFormat(RTE_INVALID_OPERATOR, op, operands);
}
void CNeoVMWorker::SetErrorUnsupport(const char* pErrMsg, ValueRef p)
{
	char buff[1024];
#ifdef _WIN32
	snprintf(buff, _countof(buff), pErrMsg, GetDataType(p.GetType()).c_str());
#else
	sprintf(buff, pErrMsg, GetDataType(p.GetType()).c_str());
#endif
	SetError(buff);
}
void CNeoVMWorker::SetErrorFormat(const char* fmt, ...)
{
	char buff[1024];

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buff, sizeof(buff), fmt, ap);
	va_end(ap);

	buff[sizeof(buff)-1] = '\0';
	SetError(buff);
}

// ─── 실행 컨텍스트(풀) 기반 최상위 실행/재개 ──────────────────────────────
void CNeoVMWorker::BindContext(CoroutineInfo* ctx)
{
	m_pCur = ctx;
	ctx->_program = _pProgram;
	SetVarStackBase(&ctx->m_sVarStack);
	m_pCallStack = &ctx->m_sCallStack;
	m_pClosureCallStack = &ctx->m_sClosureCallStack;
	m_pVarStack_Pointer = &(*m_pVarStack_Base)[0];
}

// 컨텍스트의 모든 프레임이 소유한 참조를 정책으로 해제하고 사용 구간을 0으로 채운다.
// 실행 중인 컨텍스트는 호출자가 먼저 _info를 저장해야 한다(ReleaseExecution, StopCoroutine).
void CNeoVMWorker::ReleaseContextFrames(CoroutineInfo* ctx)
{
	if (ctx == nullptr) return;
	ctx->_program = _pProgram;
	ForEachContextFrameSlot(ctx, [this](VarInfo* slot, VAR_TYPE type)
	{
		const StoragePolicy policy = PolicyOfType(type);
		if (PolicyOwns(policy)) ReleaseByPolicy(slot, policy);
	});
	int usedMax = ctx->_info._iSP_Vars_Max2;
	if (usedMax < ctx->_info._iSP_Vars + 1) usedMax = ctx->_info._iSP_Vars + 1;
	if (usedMax > (int)ctx->m_sVarStack.size()) usedMax = (int)ctx->m_sVarStack.size();
	if (usedMax > 0) memset(ctx->m_sVarStack.data(), 0, sizeof(VarInfo) * usedMax);
	ctx->_info._functionIndex = -1;
	ctx->_info._activePlan = nullptr;
	// 프레임 기록은 더 이상 유효한 슬롯을 가리키지 않는다.
	ctx->m_sCallStack.clear();
	ctx->m_sNativeFrames.clear();
}

void CNeoVMWorker::ReleaseCallWindow(const CallStoragePlan& plan, VarInfo* window, bool includeResult)
{
	for (std::size_t i = includeResult ? 0 : 1; i < plan.policies.size(); ++i)
	{
		const StoragePolicy policy = static_cast<StoragePolicy>(plan.policies[i]);
		if (PolicyOwns(policy)) ReleaseByPolicy(window + i, policy);
		else window[i]._int = 0;
	}
}

// 함수 슬롯 payload: 0은 null, 홀수는 이름 함수 번호, 짝수는 클로저 포인터다.
static NEOS_FORCEINLINE ClosureInfo* CallableClosure(const VarInfo* slot)
{
	return slot->_int != 0 && (slot->_int & 1) == 0 ? slot->_closure : nullptr;
}

// 프레임의 소유 슬롯 구간 [_releaseFirst, _releaseCount)를 정책으로 해제한다. LeaveFrame이 구간이 있을 때만 부른다.
NEOS_NOINLINE void CNeoVMWorker::ReleaseFrameOwned(const SFunctionTable& fun, int base)
{
	const u8* info = fun._slotInfo.data();
	VarInfo* slots = &(*m_pVarStack_Base)[base];
	for (int i = fun._releaseFirst; i < fun._releaseCount; ++i)
	{
		const StoragePolicy policy = SlotInfoPolicy(info[i]);
		if (policy != StoragePolicy::Payload) ReleaseByPolicy(slots + i, policy);
	}
}

void CNeoVMWorker::ReleaseByPolicy(VarInfo* slot, StoragePolicy policy)
{
	CNeoVM* vm = GetVM();
	switch (policy)
	{
	case StoragePolicy::Payload:
	case StoragePolicy::Boxed: return;
	case StoragePolicy::String: ReleaseKnownStorage<VAR_STRING>(vm, slot); return;
	case StoragePolicy::Vec: ReleaseKnownStorage<VAR_VEC3>(vm, slot); return;
	case StoragePolicy::Native: ReleaseKnownStorage<VAR_FP_NATIVE>(vm, slot); return;
	case StoragePolicy::Array: ReleaseKnownStorage<VAR_ARRAY>(vm, slot); return;
	case StoragePolicy::Map: ReleaseKnownStorage<VAR_MAP>(vm, slot); return;
	case StoragePolicy::List: ReleaseKnownStorage<VAR_LIST>(vm, slot); return;
	case StoragePolicy::Set: ReleaseKnownStorage<VAR_SET>(vm, slot); return;
	case StoragePolicy::Coroutine: ReleaseKnownStorage<VAR_COROUTINE>(vm, slot); return;
	case StoragePolicy::Async: ReleaseKnownStorage<VAR_ASYNC>(vm, slot); return;
	case StoragePolicy::Module: Var_Release(ValueRef(slot, VAR_MODULE)); return;
	case StoragePolicy::Callable:
		if (CallableClosure(slot) != nullptr) Var_Release(ValueRef(slot, VAR_CLOSURE));
		slot->_int = 0;
		return;
	}
}

void CNeoVMWorker::CopyByPolicy(VarInfo* target, const VarInfo* source, StoragePolicy policy)
{
	CNeoVM* vm = GetVM();
	VarInfo* from = const_cast<VarInfo*>(source);
	switch (policy)
	{
	case StoragePolicy::Payload:
	case StoragePolicy::Boxed: *target = *source; return;
	case StoragePolicy::String: CopyKnownStorage<VAR_STRING>(vm, target, source); return;
	case StoragePolicy::Vec: CopyKnownStorage<VAR_VEC3>(vm, target, source); return;
	case StoragePolicy::Native: CopyKnownStorage<VAR_FP_NATIVE>(vm, target, source); return;
	case StoragePolicy::Array: CopyKnownStorage<VAR_ARRAY>(vm, target, source); return;
	case StoragePolicy::Map: CopyKnownStorage<VAR_MAP>(vm, target, source); return;
	case StoragePolicy::List: CopyKnownStorage<VAR_LIST>(vm, target, source); return;
	case StoragePolicy::Set: CopyKnownStorage<VAR_SET>(vm, target, source); return;
	case StoragePolicy::Coroutine: CopyKnownStorage<VAR_COROUTINE>(vm, target, source); return;
	case StoragePolicy::Async: CopyKnownStorage<VAR_ASYNC>(vm, target, source); return;
	case StoragePolicy::Module: Move(ValueRef(target, VAR_MODULE), ValueRef(from, VAR_MODULE)); return;
	case StoragePolicy::Callable:
	{
		// 새 참조를 먼저 잡아 자기 대입과 별칭을 안전하게 처리한다.
		const VarInfo copy = *source;
		ClosureInfo* closure = CallableClosure(&copy);
		if (closure != nullptr) ++closure->_refCount;
		ReleaseByPolicy(target, StoragePolicy::Callable);
		*target = copy;
		return;
	}
	}
}

// 태그 있는 경계 값은 GC 큐에 넣을 때만 만든다. 일반 참조 증감은 고정 타입 포인터로 끝낸다.
template <VAR_TYPE Type, typename Container>
static NEOS_FORCEINLINE void ReleaseStorageContainer(CNeoVM* vm, Container* pointer, const VarInfo* value)
{
    if (pointer == nullptr) return;
    if (--pointer->_refCount <= 0) vm->QueueContainerForDestroy(BoundaryFromSlot(value, Type));
    else if (pointer->_cycleState._mayContainContainerChild) vm->QueueContainerForCycleCheck(BoundaryFromSlot(value, Type));
}

template <VAR_TYPE Type>
NEOS_FORCEINLINE void CNeoVMWorker::ReleaseKnownStorage(CNeoVM* vm, VarInfo* value)
{
    // Type은 템플릿 상수다. Release 빌드에서는 해당 타입의 해제 코드만 남는다.
    switch (Type)
    {
    case VAR_STRING: if (value->_str && --value->_str->_refCount <= 0) vm->FreeString(ValueRef(value, Type)); break;
    case VAR_VEC3:
    case VAR_VEC4: if (value->_vec && --value->_vec->_refCount <= 0) vm->FreeVec(value->_vec); break;
    case VAR_FP_NATIVE: if (value->_fpNative && --value->_fpNative->_refCount <= 0) vm->FreeFunctionProperty(value->_fpNative); break;
    case VAR_ARRAY: if (value->_array && --value->_array->_refCount <= 0) vm->FreeArray(value->_array); break;
    case VAR_MAP: ReleaseStorageContainer<Type>(vm, value->_tbl, value); break;
    case VAR_LIST: ReleaseStorageContainer<Type>(vm, value->_lst, value); break;
    case VAR_SET: ReleaseStorageContainer<Type>(vm, value->_set, value); break;
    case VAR_COROUTINE: ReleaseStorageContainer<Type>(vm, value->_cor, value); break;
    case VAR_ASYNC: ReleaseStorageContainer<Type>(vm, value->_async, value); break;
    default: assert(false); return;
    }
    value->_int = 0;
}

template <VAR_TYPE Type>
NEOS_FORCEINLINE void CNeoVMWorker::CopyKnownStorage(CNeoVM* vm, VarInfo* target, const VarInfo* source)
{
    // 먼저 새 참조를 보관하므로 자기 대입과 같은 객체를 가리키는 별칭도 안전하다.
    const VarInfo copy = *source;
    switch (Type)
    {
    case VAR_STRING: if (copy._str) ++copy._str->_refCount; break;
    case VAR_VEC3:
    case VAR_VEC4: if (copy._vec) ++copy._vec->_refCount; break;
    case VAR_FP_NATIVE: if (copy._fpNative) ++copy._fpNative->_refCount; break;
    case VAR_ARRAY: if (copy._array) ++copy._array->_refCount; break;
    case VAR_MAP: if (copy._tbl) ++copy._tbl->_refCount; break;
    case VAR_LIST: if (copy._lst) ++copy._lst->_refCount; break;
    case VAR_SET: if (copy._set) ++copy._set->_refCount; break;
    case VAR_COROUTINE: if (copy._cor) ++copy._cor->_refCount; break;
    case VAR_ASYNC: if (copy._async) ++copy._async->_refCount; break;
    default: assert(false); return;
    }
    ReleaseKnownStorage<Type>(vm, target);
    *target = copy;
}

// 호스트가 준 태그 있는 값을 피호출자의 인자 슬롯에 넣는다. 선언 타입과 다른 값은 거부한다.
bool CNeoVMWorker::StoreArgument(const SFunctionTable& fun, int base, int index, ValueRef value)
{
	VarInfo* slot = &(*m_pVarStack_Base)[base + 1 + index];
	const VAR_TYPE declared = fun._slotTypes[1 + index];
	const VAR_TYPE actual = value ? value.GetType() : VAR_NONE;
	if (declared == VAR_NONE)
	{
		// 종류를 정하지 못한 인자 슬롯은 payload만 받는다. 참조는 소유권을 나눌 수 없다.
		if (actual >= VAR_STRING) return false;
		if (value) *slot = *value.Data(); else slot->_int = 0;
		return true;
	}
	ValueRef target(slot, declared);
	switch (declared)
	{
	case VAR_INT:
		if (actual == VAR_INT) slot->_int = value->_int;
		else if (actual == VAR_FLOAT) slot->_int = NeoFloatToInt(value->_float);
		else return false;
		return true;
	case VAR_FLOAT:
		if (actual == VAR_FLOAT) slot->_float = value->_float;
		else if (actual == VAR_INT) slot->_float = static_cast<NS_FLOAT>(value->_int);
		else return false;
		return true;
	case VAR_BOOL:
		if (actual != VAR_BOOL) return false;
		slot->_int = 0; slot->_bl = value->_bl;
		return true;
	default:
		break;
	}
	// 참조·벡터·함수값: null 또는 같은 타입만 받는다. 함수 슬롯은 이름 함수와 클로저를 모두 받는다.
	const bool callable = declared == ValueRef::CallableStorageType() && (actual == VAR_FUN || actual == VAR_CLOSURE);
	const bool objectId = declared == ValueRef::ObjectIdStorageType() && actual == VAR_INT;
	if (actual != VAR_NONE && actual != declared && !callable && !objectId) return false;
	Move(target, value);
	return true;
}

void CNeoVMWorker::ReleaseDetachedClosureWithoutSync(ClosureInfo*& closure)
{
	if (closure == nullptr)
		return;
	if (m_pActiveClosure == closure)
		m_pActiveClosure = nullptr;
	if (m_pCur && m_pCur->_activeClosure == closure)
		m_pCur->_activeClosure = nullptr;
	BoundaryValue active(VAR_CLOSURE);
	active._closure = closure;
	closure = nullptr;
	Var_Release(&active);
}

void CNeoVMWorker::ReleaseCoroutineClosuresWithoutSync(CoroutineInfo* pCI)
{
	if (pCI == nullptr)
		return;

	// 스택 값을 보관함으로 되쓰지 않고, yield/일반 호출이 보유한 실행 참조만
	// 각각 한 번 반납한다.
	if (pCI->_activeClosure != nullptr)
	{
		BoundaryValue active(VAR_CLOSURE);
		active._closure = pCI->_activeClosure;
		pCI->_activeClosure = nullptr;
		Var_Release(&active);
	}
	for (int i = (int)pCI->m_sClosureCallStack.size() - 1; i >= 0; --i)
	{
		SClosureCallState& state = pCI->m_sClosureCallStack[i];
		if (state._closure == nullptr)
			continue;
		BoundaryValue active(VAR_CLOSURE);
		active._closure = state._closure;
		state._closure = nullptr;
		Var_Release(&active);
	}
	pCI->m_sClosureCallStack.clear();
}

void CNeoVMWorker::UnwindActiveClosures()
{
	if (m_pVarStack_Base == nullptr) return;
	if (m_pActiveClosure != nullptr)
	{
		SyncActiveClosure();
		if (m_pCur && m_pCur->_activeClosure == m_pActiveClosure)
			m_pCur->_activeClosure = nullptr;
		BoundaryValue active(VAR_CLOSURE);
		active._closure = m_pActiveClosure;
		m_pActiveClosure = nullptr;
		Var_Release(&active);
	}
	if (m_pClosureCallStack != nullptr)
	{
		for (int i = (int)m_pClosureCallStack->size() - 1; i >= 0; --i)
		{
			const SClosureCallState& state = (*m_pClosureCallStack)[i];
			if (state._closure == nullptr) continue;
			SyncClosureAtFrame(state._closure, state._iSP_Vars);
			// The saved pointer can alias a parent when its named callee fails.
			// Clear only that alias: a direct yield parks an independent active ref.
			if (m_pCur && m_pCur->_activeClosure == state._closure)
				m_pCur->_activeClosure = nullptr;
			BoundaryValue active(VAR_CLOSURE);
			active._closure = state._closure;
			Var_Release(&active);
		}
		m_pClosureCallStack->clear();
	}
	// 캡처를 보관함으로 돌린 뒤, 아직 남은 모든 프레임의 소유 슬롯을 정책으로 해제한다.
	// 콜스택을 지우면 프레임 배치를 다시 찾을 수 없으므로 반드시 그 전에 한다.
	if (m_pCur != nullptr)
	{
		m_pCur->_info = *(CoroutineBase*)this;
		ReleaseContextFrames(m_pCur);
		_functionIndex = -1;
		_activePlan = nullptr;
	}
	if (m_pCallStack != nullptr)
		m_pCallStack->clear();
	if (m_pCur != nullptr)
		m_pCur->m_sAsyncWaitReturnStack.clear();
}

// 최상위 실행 종료: 메인 컨텍스트를 풀로 반납한다.
// (코루틴 컨텍스트는 각자의 BoundaryValue 가 GC/스택 해제될 때 FreeCoroutine 으로 반납된다.)
void CNeoVMWorker::ReleaseExecution(bool discardParkedClosuresWithoutSync)
{
    m_sDebugLineSkips.clear();
	// The running coroutine's saved high-water is updated only on a switch.
	// Preserve it before error unwinding, including a failure on first resume.
	if (m_pCur != nullptr) m_pCur->_info = *(CoroutineBase*)this;
	CoroutineInfo* activeCoroutine = (m_pCur != m_pMainCtx) ? m_pCur : nullptr;
	if (activeCoroutine != nullptr) ++activeCoroutine->_refCount;
	for (CoroutineInfo* context : m_sCoroutines)
		if (context != m_pMainCtx && context != activeCoroutine) ++context->_refCount;

	// cancel/강제 정리는 RET를 밟지 않으므로, 아직 프레임에 남은 closure 참조와
	// 캡처 슬롯을 실행 컨텍스트 반납 전에 정리한다.
	UnwindActiveClosures();
	// park되어 있던 컨텍스트도 이 실행과 함께 더 이상 재개되지 않는다. raw
	// scheduler 목록을 clear하기 전에 실행 보유 closure를 해제해야 다음 pool
	// Acquire의 clear()가 참조를 잃지 않는다. 오류일 때만 write-back을 생략한다.
	if (m_pCur != nullptr)
	{
		if (discardParkedClosuresWithoutSync)
			ReleaseCoroutineClosuresWithoutSync(m_pCur);
		else
			DrainCoroutineClosures(m_pCur);
	}
	for (CoroutineInfo* pCI : m_sCoroutines)
	{
		if (pCI != m_pCur)
		{
			if (discardParkedClosuresWithoutSync)
				ReleaseCoroutineClosuresWithoutSync(pCI);
			else
				DrainCoroutineClosures(pCI);
		}
	}
	// These contexts belong to the discarded execution. Hold them until all
	// stacks are cleared because a parent stack may own its running child.
	if (activeCoroutine != nullptr) DeadCoroutine(activeCoroutine);
	for (CoroutineInfo* context : m_sCoroutines)
		if (context != m_pMainCtx && context != activeCoroutine) DeadCoroutine(context);
	if (m_pMainCtx != nullptr && m_pPool != nullptr)
	{
		// 정상 완료 시 m_pCur == m_pMainCtx 이며 레지스터는 위에서 _info 로 저장됐다.
		// 남은 프레임(최상위 반환 슬롯 포함)을 정책으로 해제하고 사용 구간을 비운다.
		ReleaseContextFrames(m_pMainCtx);
		m_pMainCtx->m_sCallStack.clear();
		m_pPool->Release(m_pMainCtx);
	}
	if (activeCoroutine != nullptr)
	{
		BoundaryValue held(VAR_COROUTINE); held._cor = activeCoroutine; Var_Release(&held);
	}
	for (CoroutineInfo* context : m_sCoroutines)
	{
		if (context == m_pMainCtx || context == activeCoroutine) continue;
		BoundaryValue held(VAR_COROUTINE); held._cor = context; Var_Release(&held);
	}
	m_pCur = nullptr;
	m_pMainCtx = nullptr;
	m_pRegisterActive = nullptr;
	m_sCoroutines.clear();
	ClearSP();
	m_bCooperativeSleepPending = false;
	_iRemainSleep = 0;
	m_bSliceExpired = false;
	SetVarStackBase(nullptr);
	m_pCallStack = nullptr;
	m_pClosureCallStack = nullptr;
	m_pVarStack_Pointer = nullptr;
}

// Run() 후 완료/정지/오류를 판정한다. 완료된 반환값 보유 요청이 없으면 컨텍스트를 반납한다.
int CNeoVMWorker::RunSettle(bool retainCompletedResult)
{
	m_bTopExec = true;
	bool ok = Run();
	m_bTopExec = false;
	int status;
	if (ok == false)
	{
		ReleaseExecution(true);
		status = NEOEXEC_ERROR;
	}
	// 정지(retain) 조건: sleep, debugger pause, 또는 시간 제한 슬라이스.
	else if (IsSuspended())
	{
		status = NEOEXEC_SUSPENDED;
	}
	else
	{
		if (!retainCompletedResult) ReleaseExecution();
		status = NEOEXEC_COMPLETED;
	}
	return status;
}

// 최상위 실행: 풀에서 컨텍스트를 대여해 iFID 를 처음부터 실행.
int CNeoVMWorker::ExecuteTop(int iFunctionID, std::vector<BoundaryValue>& _args)
{
	try
	{
		if (m_bOutOfMemoryPoisoned)
			return NEOEXEC_ERROR;
		if (m_pPool == nullptr)
			return NEOEXEC_ERROR;
		if (m_pMainCtx != nullptr)
			return NEOEXEC_ERROR;   // 이미 정지된 실행이 있음 → ResumeTop 을 써야 함

		m_pMainCtx = m_pPool->Acquire();
		BindContext(m_pMainCtx);
		m_pRegisterActive = nullptr;
		m_sCoroutines.clear();
		ResetFaultStateForNewExecution();
		_iSP_Vars = 0;
		_iSP_VarsMax = 0;
		_iSP_Vars_Max2 = 0;
		m_bCooperativeSleepPending = false;
		_iRemainSleep = 0;
		m_bSliceExpired = false;
		_isInitialized = true;

		if (Setup(iFunctionID, _args) == false)
		{
			ReleaseExecution();
			GetVM()->PublishAllocStats();
			return NEOEXEC_ERROR;
		}
		int status = RunSettle();
		GetVM()->PublishAllocStats();
		return status;
	}
	catch (const std::bad_alloc&)
	{
		// Acquire/Setup은 Run()의 보호 범위보다 앞에 있으므로 여기서도 잡는다.
		if (m_pMainCtx != nullptr)
			ReleaseExecution(true);
		PoisonOutOfMemory();
		GetVM()->PublishAllocStats();
		return NEOEXEC_ERROR;
	}
}

// 정지된 최상위 실행을 이어서 재개 (retain 된 컨텍스트/레지스터로 계속).
int CNeoVMWorker::ResumeTop(bool retainCompletedResult)
{
	if (m_bInRun) return NEOEXEC_ERROR;
	try
	{
		if (m_bOutOfMemoryPoisoned)
			return NEOEXEC_ERROR;
		if (m_pMainCtx == nullptr)
			return NEOEXEC_COMPLETED;   // 정지 상태 아님
		int status = RunSettle(retainCompletedResult);
		GetVM()->PublishAllocStats();
		return status;
	}
	catch (const std::bad_alloc&)
	{
		if (m_pMainCtx != nullptr)
			ReleaseExecution(true);
		PoisonOutOfMemory();
		GetVM()->PublishAllocStats();
		return NEOEXEC_ERROR;
	}
}

NeoExecutionState CNeoVMWorker::GetExecutionState()
{
	if (m_pMainCtx == nullptr)
		return NeoExecutionState::Idle;
	if (m_eDebugRunMode == DBG_PAUSED)
		return NeoExecutionState::SuspendedDebugger;
	if (m_bCooperativeSleepPending || _iRemainSleep > 0)
		return NeoExecutionState::SuspendedSleep;
	if (m_bSliceExpired)
		return NeoExecutionState::SuspendedSlice;
	return NeoExecutionState::Running;
}

bool CNeoVMWorker::IsSuspended()
{
	NeoExecutionState state = GetExecutionState();
	return state == NeoExecutionState::SuspendedSleep ||
		state == NeoExecutionState::SuspendedDebugger ||
		state == NeoExecutionState::SuspendedSlice;
}

// 호스트 Invocation용 컨텍스트 대여.
NeoHostCallBegin CNeoVMWorker::BeginHostCall()
{
	if (m_bOutOfMemoryPoisoned)
		return NeoHostCallBegin::OutOfMemory;
	if (m_pMainCtx != nullptr)
	{
		switch (GetExecutionState())
		{
		case NeoExecutionState::Running:
			return NeoHostCallBegin::Nested;
		case NeoExecutionState::SuspendedSleep:
		case NeoExecutionState::SuspendedDebugger:
		case NeoExecutionState::SuspendedSlice:
			return NeoHostCallBegin::Suspended;
		default:
			return NeoHostCallBegin::InvalidState;
		}
	}
	if (m_pPool == nullptr)
		return NeoHostCallBegin::NoPool;

	try
	{
		m_pMainCtx = m_pPool->Acquire();
	}
	catch (const std::bad_alloc&)
	{
		PoisonOutOfMemory();
		// Script -> native -> Script 재진입 중이면 이 예외를 최외곽 Run까지
		// 전파해야 바깥 스크립트가 이후 opcode를 계속 실행하지 않는다.
		if (m_bInRun)
			throw;
		return NeoHostCallBegin::OutOfMemory;
	}
	BindContext(m_pMainCtx);
	m_pRegisterActive = nullptr;
	m_sCoroutines.clear();
	ResetFaultStateForNewExecution();
	_iSP_Vars = 0;
	_iSP_VarsMax = 0;
	_iSP_Vars_Max2 = 0;
	m_bCooperativeSleepPending = false;
	_iRemainSleep = 0;
	m_bSliceExpired = false;
	_isInitialized = true;
	return NeoHostCallBegin::Acquired;
}
void CNeoVMWorker::EndHostCall(NeoHostCallBegin begin)
{
	if (begin != NeoHostCallBegin::Acquired)
		return;
	// An error can also be reported as a debugger pause.  It is inspect-only:
	// never retain the failed execution context for a later F5/F10/F11 resume.
	if (m_bDebugFaulted)
	{
		ReleaseExecution(true);
	}
	else if (IsSuspended())
	{
		GetVM()->PublishAllocStats();
		return;   // 호출이 정지(sleep/브레이크)됐으면 컨텍스트 retain
	}
	else
	{
		ReleaseExecution();
	}
	GetVM()->PublishAllocStats();
}

// 정지/바인딩된 실행을 버린다. 전역 변수(m_sVarGlobal)는 워커 소유라 그대로 살아있고,
// 실행 컨텍스트(스택/코루틴/IP)만 풀로 반납해 idle 로 되돌린다.
// 인터프리터 실행 중에는 지금 밟고 있는 스택을 반납하게 되므로 거부한다.
bool CNeoVMWorker::CancelExecution()
{
	if (m_bInRun)
		return false;
	if (m_pMainCtx == nullptr)
		return true;   // 이미 idle
	ReleaseExecution();           // 컨텍스트 반납 + ClearSP + _iRemainSleep=0
	m_eDebugRunMode = DBG_CONTINUE;
	m_bDebugPauseRequested = false;
	m_bDebugFaulted = false;
	_isErrorOPIndex = 0;
	m_iLastErrorOPIndex = -1;
	m_iLastErrorSourceOPIndex = -1;
	_isInitialized = false;
	return true;
}

bool CNeoVMWorker::CheckNestedCallDepth()
{
	// Every synchronous callback keeps native C++ frames alive as well as VM frames.
	if (m_iNativeScriptCallDepth <= 64) return true;
	SetError(RTE_CALL_STACK_OVERFLOW);
	return false;
}

void CNeoVMWorker::BeginNestedScriptCall()
{
	++m_iNativeScriptCallDepth;
}

void CNeoVMWorker::EndNestedScriptCall()
{
	--m_iNativeScriptCallDepth;
}

int CNeoVMWorker::RunHostCall(int iFunctionID, std::vector<BoundaryValue>& _args, ValueRef closureValue, ValueRef completedResult)
{
	// BeginHostCall이 실행 컨텍스트를 확보한 뒤에만 이 경로로 진입한다. ExecuteTop은
	// 완료 시 컨텍스트를 즉시 반납하므로, 반환 슬롯을 읽어야 하는 Invocation에는 맞지 않는다.
	ClosureInfo* outerClosure = nullptr;
	bool debugSuppressed = false;
	try
	{
		if (m_bOutOfMemoryPoisoned)
			return NEOEXEC_ERROR;
		if (m_pMainCtx == nullptr)
			return NEOEXEC_ERROR;

		// Script A -> native API -> Script B는 동일한 코드 버퍼를 공유한다.
		// Script B가 정상 반환하면 Script A의 다음 opcode부터 계속 실행해야 한다.
		const bool isNestedScriptCall = (m_iNativeScriptCallDepth > 0);
		if (!CheckNestedCallDepth()) return NEOEXEC_ERROR;
		if (isNestedScriptCall && IsSynchronousExecutionLimited())
		{
			SetErrorFormat(RTE_NESTED_NOT_ALLOWED, "limited execution");
			return NEOEXEC_ERROR;
		}
		const int returnCodePtr = isNestedScriptCall ? GetCodeptr() : 0;
		const int nativeBase = _iSP_Vars;
		const int nativeMax = _iSP_VarsMax;
		const int nativeHighWater = _iSP_Vars_Max2;
		// 네이티브를 부른 스크립트 프레임과 창은 네이티브 호출 범위가 컨텍스트에 기록했다.
		// 자식 프레임은 그 위(nativeHighWater)에서 시작하므로 겹치지 않는다.
		const int nativeFunction = _functionIndex;
		const CallStoragePlan* nativePlan = _activePlan;
		// Validate before detaching the parent or touching its stack frame.
		if (closureValue != nullptr && (closureValue.GetType() != VAR_CLOSURE ||
			closureValue->_closure == nullptr || closureValue->_closure->_funIndex != iFunctionID ||
			closureValue->_closure->_ownerWorkerId != GetWorkerID() ||
			iFunctionID < 0 || iFunctionID >= (int)Functions().size() ||
			closureValue->_closure->_captures.size() != Functions()[iFunctionID]._captures.size()))
			return NEOEXEC_ERROR;
		// Native arguments remain live throughout reentry, including while the
		// child Invocation is alive. Child locals must not reuse that window.
		if (isNestedScriptCall) _iSP_Vars = nativeHighWater;
		// 네이티브에서 재진입한 일반 함수는 부모 람다의 active closure를 공유하면
		// 안 된다. 자식 RET가 부모 프레임 슬롯을 동기화하는 것을 막기 위해 실행 중
		// 잠시 분리하고, Run이 끝난 뒤 원래 프레임에 복원한다.
		outerClosure = m_pActiveClosure;
		m_pActiveClosure = nullptr;
		if (Setup(iFunctionID, _args) == false)
		{
			_iSP_Vars = nativeBase;
			_iSP_VarsMax = nativeMax;
			_iSP_Vars_Max2 = nativeHighWater;
			SetStackPointer(nativeBase);
			if (nativeFunction >= 0) SelectFunction(nativeFunction);
			_activePlan = nativePlan;
			m_pActiveClosure = outerClosure;
			return NEOEXEC_ERROR;
		}
		if (closureValue != nullptr)
		{
			ClosureInfo* closure = closureValue->_closure;
			const SFunctionTable& fun = Functions()[iFunctionID];
			for (size_t i = 0; i < fun._captures.size(); ++i)
				Move(GetVarPtr_L(fun._captures[i].second), closure->Capture(i));
			++closure->_refCount; // 실행 프레임이 closure를 잡는다; RET/ERROR에서 반납.
			m_pActiveClosure = closure;
		}

		// 중첩 호출은 sleep/yield와 시간 제한 정지를 이미 거부한다. 여기서 디버거
		// 정지도 막아야 부모 native 프레임으로 돌아온 뒤에 자식 closure만 남은
		// suspended 컨텍스트가 생기지 않는다.
		if (isNestedScriptCall)
		{
			++m_iDebugSuppressCount;
			debugSuppressed = true;
		}
		const bool ok = Run();
		if (debugSuppressed)
		{
			--m_iDebugSuppressCount;
			debugSuppressed = false;
		}
		if (ok && isNestedScriptCall)
		{
			// 자식 프레임은 RET가 정리했고 결과만 반환 슬롯에 남았다. 복사한 뒤 슬롯을 비운다.
			if (completedResult) Move(completedResult, GetReturnVar());
			ReleaseByPolicy(m_pVarStack_Pointer, Functions()[iFunctionID].SlotPolicy(0));
			m_pVarStack_Pointer->_int = 0;
			_iSP_Vars = nativeBase;
			_iSP_VarsMax = nativeMax;
			_iSP_Vars_Max2 = nativeHighWater;
			SetStackPointer(nativeBase);
			if (nativeFunction >= 0) SelectFunction(nativeFunction);
			_activePlan = nativePlan;
			SetCodePtr(returnCodePtr);
		}
		// sleep/debugger로 정지한 closure는 실행 컨텍스트와 함께 active 환경도
		// 보존해야 ResumeTop의 RET/ERROR가 변경분을 동기화하고 실행 보유분을 반납한다.
		const bool suspended = IsSuspended();
		if (ok == false)
		{
			// 오류 opcode와 달리 C++ 예외는 자식 클로저가 아직 활성 상태일 수 있다.
			// 호스트가 임시 슬롯을 정리하기 전에 현재 자식 프레임에서 캡처를 보존한다.
			UnwindActiveClosures();
			// outer를 되살리면 이후 ReleaseExecution이 자식 stack base에서 캡처를 sync한다.
			ReleaseDetachedClosureWithoutSync(outerClosure);
			SetCodePtr(0);
		}
		else if (!suspended)
		{
			m_pActiveClosure = outerClosure;
		}
		GetVM()->PublishAllocStats();
		if (ok == false)
			return NEOEXEC_ERROR;
		return suspended ? NEOEXEC_SUSPENDED : NEOEXEC_COMPLETED;
	}
	catch (const std::bad_alloc&)
	{
		if (debugSuppressed)
			--m_iDebugSuppressCount;
		// OOM은 부모 Run까지 전파되어 해당 실행을 폐기한다. 부모 프레임을 복원해
		// ReleaseExecution이 잘못된 stack base로 write-back하지 않게 한다.
		ReleaseDetachedClosureWithoutSync(outerClosure);
		PoisonOutOfMemory();
		// 중첩 스크립트 호출의 OOM은 호출한 native 함수만 실패시키는 것으로
		// 끝내면 바깥 인터프리터가 손상 가능 상태에서 계속 돈다.
		if (m_bInRun)
			throw;
		GetVM()->PublishAllocStats();
		return NEOEXEC_ERROR;
	}
}

bool	CNeoVMWorker::Setup(int iFunctionID, std::vector<BoundaryValue>& _args)
{
	if (m_bOutOfMemoryPoisoned)
		return false;
	if (iFunctionID < 0 || iFunctionID >= (int)Functions().size())
		return false;

	const SFunctionTable& fun = Functions()[iFunctionID];
	int iArgs = (int)_args.size();
	if (iArgs != fun._argsCount)
		return false;
	if (!EnsureStackRange(_iSP_Vars, fun._stackSlotCount - 1))
		return false;
	// 호스트 진입 프레임은 깨끗한 구간에서 시작한다. 인자 슬롯도 비운 뒤 채운다.
	memset(&(*m_pVarStack_Base)[_iSP_Vars], 0, sizeof(VarInfo) * fun._localAddCount);
	EnterFrame(iFunctionID, _iSP_Vars);
	_activePlan = nullptr;

	SetCodePtr(fun._codePtr);

//	_iSP_Vars = 0;// _header._iStaticVarCount;
	SetStackPointer(_iSP_Vars);
	_iSP_VarsMax = _iSP_Vars + fun._localAddCount;
	if (_iSP_Vars_Max2 < _iSP_Vars + fun._stackSlotCount)
		_iSP_Vars_Max2 = _iSP_Vars + fun._stackSlotCount;

	for (int iCur = 0; iCur < iArgs; iCur++)
		if (!StoreArgument(fun, _iSP_Vars, iCur, &_args[iCur]))
		{
			LeaveFrame(fun, _iSP_Vars);
			SetError(RTE_CALL_ARG_TYPE);
			return false;
		}

	return true;
}

void CNeoVMWorker::DebugSetListener(INeoVMDebugListener* listener)
{
	m_pDebugListener = listener;
}
void CNeoVMWorker::ClearDebugBreakpoints()
{
	m_sDebugBreakLineBits.clear();
	m_sDebugBreakLocationBits.clear();
	m_iDebugBreakCount = 0;
}
void CNeoVMWorker::SetDebugBreakLineBit(std::vector<u8>& bits, int line)
{
	if (line <= 0)
		return;
	size_t byteIndex = (size_t)line >> 3;
	u8 mask = (u8)(1 << (line & 7));
	if (bits.size() <= byteIndex)
		bits.resize(byteIndex + 1, 0);
	if ((bits[byteIndex] & mask) == 0)
	{
		bits[byteIndex] |= mask;
		++m_iDebugBreakCount;
	}
}
bool CNeoVMWorker::IsDebugBreakLineBit(const std::vector<u8>& bits, int line) const
{
	if (line <= 0)
		return false;
	size_t byteIndex = (size_t)line >> 3;
	if (byteIndex >= bits.size())
		return false;
	u8 mask = (u8)(1 << (line & 7));
	return (bits[byteIndex] & mask) != 0;
}
bool CNeoVMWorker::IsDebugBreakpoint(int file, int line) const
{
	if (IsDebugBreakLineBit(m_sDebugBreakLineBits, line))
		return true;
	if (file < 0 || file >= (int)m_sDebugBreakLocationBits.size())
		return false;
	return IsDebugBreakLineBit(m_sDebugBreakLocationBits[file], line);
}
void CNeoVMWorker::DebugSetBreakpoints(const std::vector<int>& lines)
{
	ClearDebugBreakpoints();
	for (int line : lines)
	{
		SetDebugBreakLineBit(m_sDebugBreakLineBits, line);
	}
}
void CNeoVMWorker::DebugSetBreakpoints(const std::vector<NeoDebugBreakpoint>& breakpoints)
{
	ClearDebugBreakpoints();
	for (const NeoDebugBreakpoint& bp : breakpoints)
	{
		if (bp.line <= 0)
			continue;
		if (bp.file < 0)
		{
			SetDebugBreakLineBit(m_sDebugBreakLineBits, bp.line);
			continue;
		}
		if ((int)m_sDebugBreakLocationBits.size() <= bp.file)
			m_sDebugBreakLocationBits.resize((size_t)bp.file + 1);
		SetDebugBreakLineBit(m_sDebugBreakLocationBits[bp.file], bp.line);
	}
}
void CNeoVMWorker::DebugGetExecutableLocations(std::vector<NeoDebugLocation>& locations)
{
	locations.clear();
	if (IsDebugInfo() == false)
		return;

	std::set<u32> uniqueLocations;
	const std::vector<debug_info>& dbg = DebugData();
	for (int i = 0; i < (int)dbg.size(); ++i)
	{
		const debug_info& info = dbg[i];
		if (info._lineseq <= 0)
			continue;
		u32 key = (((u32)info._fileseq) << 16) | (u32)info._lineseq;
		if (uniqueLocations.insert(key).second)
		{
			NeoDebugLocation location;
			location.opIndex = i;
			location.file = info._fileseq;
			location.line = info._lineseq;
			locations.push_back(location);
		}
	}
}
void CNeoVMWorker::PruneDebugLineSkips(CoroutineInfo* context, int maxDepth)
{
    for (auto it = m_sDebugLineSkips.begin(); it != m_sDebugLineSkips.end(); )
    {
        if (it->context == context && it->depth > maxDepth) it = m_sDebugLineSkips.erase(it);
        else ++it;
    }
}
void CNeoVMWorker::PrepareDebugResume(EDebugRunMode mode)
{
    m_bDebugPauseRequested = false;
    m_eDebugRunMode = mode;
    m_iDebugStepDepth = m_sDebugLocation.callDepth;
    PruneDebugLineSkips(m_pCur, m_iDebugStepDepth - 1);
    m_sDebugLineSkips.push_back({m_pCur, m_iDebugStepDepth, m_sDebugLocation.file,
        m_sDebugLocation.line, m_sDebugLocation.opIndex, m_sDebugLocation.opIndex});
    m_pDebugStepContext = m_pCur;
    m_pDebugStepReturnContext = m_sCoroutines.empty() ? nullptr : m_sCoroutines.front();
}
void CNeoVMWorker::DebugContinue() { PrepareDebugResume(DBG_CONTINUE); }
void CNeoVMWorker::DebugStepInto() { PrepareDebugResume(DBG_STEP_INTO); }
void CNeoVMWorker::DebugStepOver() { PrepareDebugResume(DBG_STEP_OVER); }
void CNeoVMWorker::DebugStepOut() { PrepareDebugResume(DBG_STEP_OUT); }

void CNeoVMWorker::DebugPause()
{
	m_bDebugPauseRequested = true;
}
bool CNeoVMWorker::DebugIsPaused()
{
	return GetExecutionState() == NeoExecutionState::SuspendedDebugger;
}
static const int NEO_DEBUG_MAX_COLLECTION_DEPTH = 4;
static const int NEO_DEBUG_MAX_COLLECTION_ITEMS = 64;

struct NeoDebugSnapshotBudget
{
    int nodes = 1024;
    size_t bytes = 64 * 1024;
    std::unordered_set<const void*> visited;
    std::string Text(const std::string& value)
    {
        size_t count = std::min(value.size(), std::min<size_t>(bytes, 4096));
        if (count < value.size())
            while (count && (static_cast<unsigned char>(value[count]) & 0xc0) == 0x80) --count;
        bytes -= count;
        std::string text(value.data(), count);
        if (count < value.size()) text += "...";
        return text;
    }
};
static void NeoDebugFormatValue(ValueRef pVar, NeoDebugVariable& out, NeoDebugSnapshotBudget& budget, int collectionDepth = 0)
{
    if (!pVar || budget.nodes <= 0) { out.type = "unavailable"; out.value = "snapshot limit"; return; }
    --budget.nodes;
    bool shared = false;
    if (pVar.GetType() == VAR_LIST || pVar.GetType() == VAR_MAP ||
        pVar.GetType() == VAR_ARRAY || pVar.GetType() == VAR_CLOSURE)
        {
        const void* identity = pVar.GetType() == VAR_LIST ? static_cast<void*>(pVar->_lst) :
            pVar.GetType() == VAR_MAP ? static_cast<void*>(pVar->_tbl) :
            pVar.GetType() == VAR_ARRAY ? static_cast<void*>(pVar->_array) : static_cast<void*>(pVar->_closure);
        shared = !budget.visited.insert(identity).second;
    }
    const bool expand = !shared && budget.nodes > 0 && collectionDepth < NEO_DEBUG_MAX_COLLECTION_DEPTH;
	char buf[128];
	switch (pVar.GetType())
	{
	case VAR_INT:
		out.type = "int";
		snprintf(buf, sizeof(buf), "%lld", (long long)pVar->_int);
		out.value = buf;
		break;
	case VAR_FLOAT:
		out.type = "float";
		snprintf(buf, sizeof(buf), "%g", (double)pVar->_float);
		out.value = buf;
		break;
	case VAR_BOOL:
		out.type = "bool";
		out.value = pVar->_bl ? "true" : "false";
		break;
	case VAR_NONE:
		out.type = "none";
		out.value = "none";
		break;
	case VAR_FUN:
		out.type = "function";
		snprintf(buf, sizeof(buf), "#%d", pVar.FunctionIndex());
		out.value = buf;
		break;
	case VAR_ITERATOR:
		out.type = "iterator";
		out.value = "iterator";
		break;
	case VAR_FUN_NATIVE:
		out.type = "native_function";
		out.value = "native_function";
		break;
	case VAR_VEC:
	{
		const int n = pVar.VectorComponentCount();
		out.type = (n <= 2) ? "Vector2" : (n == 3 ? "Vector3" : "Vector4");
		if (n <= 2)      snprintf(buf, sizeof(buf), "(%g, %g)", pVar.VectorComponent(0), pVar.VectorComponent(1));
		else if (n == 3) snprintf(buf, sizeof(buf), "(%g, %g, %g)", pVar.VectorComponent(0), pVar.VectorComponent(1), pVar.VectorComponent(2));
		else             snprintf(buf, sizeof(buf), "(%g, %g, %g, %g)", pVar.VectorComponent(0), pVar.VectorComponent(1), pVar.VectorComponent(2), pVar.VectorComponent(3));
		out.value = buf;
		break;
	}
	case VAR_STRING:
		out.type = "string";
		out.value = pVar->_str ? budget.Text(pVar->_str->_str) : "";
		break;
	case VAR_VEC3:
	case VAR_VEC4:
	{
		const int n = pVar.VectorComponentCount();
		out.type = (n <= 2) ? "Vector2" : (n == 3 ? "Vector3" : "Vector4");
		if (n <= 2)      snprintf(buf, sizeof(buf), "(%g, %g)", pVar.VectorComponent(0), pVar.VectorComponent(1));
		else if (n == 3) snprintf(buf, sizeof(buf), "(%g, %g, %g)", pVar.VectorComponent(0), pVar.VectorComponent(1), pVar.VectorComponent(2));
		else             snprintf(buf, sizeof(buf), "(%g, %g, %g, %g)", pVar.VectorComponent(0), pVar.VectorComponent(1), pVar.VectorComponent(2), pVar.VectorComponent(3));
		out.value = buf;
		break;
	}
	case VAR_FP_NATIVE:
		out.type = "native_object";
		out.value = "native_object";
		break;
	case VAR_ARRAY:
	{
		ArrayInfo* array = pVar->_array;
		const int count = array ? array->_count : 0;
		out.type = "array";
		snprintf(buf, sizeof(buf), "array<%s>(%d)", array ? GetArrayElementTypeName(array->_elementType) : "?", count);
		out.value = buf;
		if (array != nullptr && expand)
		{
			const int childCount = count < NEO_DEBUG_MAX_COLLECTION_ITEMS ? count : NEO_DEBUG_MAX_COLLECTION_ITEMS;
			out.children.reserve(childCount + (count > childCount ? 1 : 0));
			for (int i = 0; i < childCount && budget.nodes > 0; ++i)
			{
				NeoDebugVariable child;
				child.name = "[" + std::to_string(i) + "]";
				--budget.nodes;
				switch (array->_elementType)
				{
				case NeoArrayElementType::Bool:
					child.type = "bool";
					child.value = array->GetBool(i) ? "true" : "false";
					break;
				case NeoArrayElementType::Int:
					child.type = "int";
					child.value = std::to_string(array->IntData()[i]);
					break;
				case NeoArrayElementType::Float:
					child.type = "float";
					snprintf(buf, sizeof(buf), "%g", (double)array->FloatData()[i]);
					child.value = buf;
					break;
				case NeoArrayElementType::Vector2:
				case NeoArrayElementType::Vector3:
				case NeoArrayElementType::Vector4:
				{
					float v[4];
					array->VectorAt(i).CopyVectorComponents(v);
					child.type = GetArrayElementTypeName(array->_elementType);
					const int n = array->VectorDimension();
					if (n == 2) snprintf(buf, sizeof(buf), "(%g, %g)", v[0], v[1]);
					else if (n == 3) snprintf(buf, sizeof(buf), "(%g, %g, %g)", v[0], v[1], v[2]);
					else snprintf(buf, sizeof(buf), "(%g, %g, %g, %g)", v[0], v[1], v[2], v[3]);
					child.value = buf;
					break;
				}
				case NeoArrayElementType::Int32:
					child.type = "int";
					child.value = std::to_string(array->Int32Data()[i]);
					break;
				case NeoArrayElementType::Float32:
					child.type = "float";
					snprintf(buf, sizeof(buf), "%g", (double)array->Float32Data()[i]);
					child.value = buf;
					break;
				}
				out.children.push_back(std::move(child));
			}
			if (count > childCount)
			{
				NeoDebugVariable more;
				more.name = "...";
				more.type = "array";
				more.value = std::to_string(count - childCount) + " more items";
				out.children.push_back(std::move(more));
			}
		}
		break;
	}
	case VAR_MAP:
		out.type = "map";
		{
			MapInfo* map = pVar->_tbl;
			const int count = map ? map->GetCount() : 0;
			snprintf(buf, sizeof(buf), "map(%d)", count);
			if (map != nullptr && expand)
			{
				const int childCount = count < NEO_DEBUG_MAX_COLLECTION_ITEMS ? count : NEO_DEBUG_MAX_COLLECTION_ITEMS;
				out.children.reserve(childCount + (count > childCount ? 1 : 0));
				CollectionIterator iterator = map->FirstNode();
				for (int i = 0; i < childCount && budget.nodes > 0 && iterator._pTableNode != nullptr; ++i)
				{
					MapNode* node = iterator._pTableNode;
					NeoDebugVariable child;
					if (node->data->key.GetType() == VAR_STRING && node->data->key._str != nullptr)
						child.name = "[\"" + budget.Text(node->data->key._str->_str) + "\"]";
					else
					{
						NeoDebugVariable key;
						NeoDebugFormatValue(&node->data->key, key, budget, collectionDepth + 1);
						child.name = "[" + key.value + "]";
					}
					NeoDebugFormatValue(&node->data->value, child, budget, collectionDepth + 1);
					out.children.push_back(std::move(child));
					map->NextNode(iterator);
				}
				if (count > childCount)
				{
					NeoDebugVariable more;
					more.name = "...";
					more.type = "map";
					more.value = std::to_string(count - childCount) + " more items";
					out.children.push_back(std::move(more));
				}
			}
		}
		out.value = buf;
		break;
	case VAR_LIST:
		out.type = "list";
		{
			const int count = pVar->_lst ? pVar->_lst->GetCount() : 0;
			snprintf(buf, sizeof(buf), "list(%d)", count);
			if (pVar->_lst != nullptr && expand)
			{
				const int childCount = count < NEO_DEBUG_MAX_COLLECTION_ITEMS ? count : NEO_DEBUG_MAX_COLLECTION_ITEMS;
				out.children.reserve(childCount + (count > childCount ? 1 : 0));
				for (int i = 0; i < childCount && budget.nodes > 0; ++i)
				{
					ValueRef pChildValue = pVar->_lst->GetValue(i);
					if (pChildValue == nullptr)
						continue;

					NeoDebugVariable child;
					child.name = "[" + std::to_string(i) + "]";
					NeoDebugFormatValue(pChildValue, child, budget, collectionDepth + 1);
					out.children.push_back(std::move(child));
				}
				if (count > childCount)
				{
					NeoDebugVariable more;
					more.name = "...";
					more.type = "list";
					more.value = std::to_string(count - childCount) + " more items";
					out.children.push_back(std::move(more));
				}
			}
		}
		out.value = buf;
		break;
	case VAR_SET:
		out.type = "set";
		snprintf(buf, sizeof(buf), "set(%d)", pVar->_set ? pVar->_set->GetCount() : 0);
		out.value = buf;
		break;
	case VAR_COROUTINE:
		out.type = "coroutine";
		out.value = "coroutine";
		break;
	case VAR_MODULE:
		out.type = "module";
		out.value = "module";
		break;
	case VAR_ASYNC:
		out.type = "async";
		out.value = "async";
		break;
	case VAR_CLOSURE:
		out.type = "function";
		if (pVar->_closure == nullptr)
		{
			out.value = "invalid closure";
			break;
		}
		snprintf(buf, sizeof(buf), "#%d", pVar->_closure->_funIndex);
		out.value = buf;
		if (expand)
		{
			std::vector<VarInfo>& captures = pVar->_closure->_captures;
			const int childCount = (int)std::min<size_t>(captures.size(), NEO_DEBUG_MAX_COLLECTION_ITEMS);
			out.children.reserve(childCount + (captures.size() > (size_t)childCount ? 1 : 0));
			for (int i = 0; i < childCount && budget.nodes > 0; ++i)
			{
				NeoDebugVariable child;
				child.name = "capture[" + std::to_string(i) + "]";
				NeoDebugFormatValue(pVar->_closure->Capture(i), child, budget, collectionDepth + 1);
				out.children.push_back(std::move(child));
			}
			if (captures.size() > (size_t)childCount)
			{
				NeoDebugVariable more;
				more.name = "...";
				more.type = "function";
				more.value = std::to_string(captures.size() - childCount) + " more captures";
				out.children.push_back(std::move(more));
			}
		}
		break;
	default:
		out.type = "unknown";
		out.value = "unknown";
		break;
	}
    if (shared) out.value += " [shared]";
}
void CNeoVMWorker::DebugGetStackTrace(std::vector<NeoDebugStackFrame>& frames)
{
	frames.clear();

	int codeOffset = DebugIsPaused() ? m_sDebugLocation.opIndex * (int)sizeof(SVMOperation) : GetCodeptr();
	if (codeOffset < 0)
		codeOffset = 0;

	NeoDebugStackFrame cur;
	cur.frameId = 0;
	cur.functionId = GetFunctionIndexFromCodeOffset(codeOffset);
	if (cur.functionId >= 0)
	{
		auto itName = _pProgram->debugFunctionNames.find(cur.functionId);
		if (itName != _pProgram->debugFunctionNames.end())
			cur.functionName = itName->second.substr(0, 4096);
	}
	cur.opIndex = codeOffset / (int)sizeof(SVMOperation);
	cur.file = (cur.opIndex >= 0 && cur.opIndex < (int)DebugData().size()) ? DebugData()[cur.opIndex]._fileseq : -1;
	cur.line = GetDebugLine(cur.opIndex);
	cur.stackBase = _iSP_Vars;
	if (cur.functionId >= 0)
	{
		const SFunctionTable& fun = Functions()[cur.functionId];
		cur.argsCount = fun._argsCount;
		cur.localCount = fun._localVarCount;
		cur.tempCount = fun._localTempMax;
	}
	frames.push_back(cur);

	if (m_pCallStack == nullptr)
		return;

	for (int i = (int)m_pCallStack->size() - 1; i >= 0 && frames.size() < 64; --i)
	{
		SCallStack& cs = (*m_pCallStack)[i];
		NeoDebugStackFrame frame;
		frame.frameId = (int)frames.size();
		frame.stackBase = cs._iSP_Vars;
		const int returnIP = (int)(cs._ReturnIP & IP_MASK);
		frame.opIndex = returnIP / (int)sizeof(SVMOperation);
		if (frame.opIndex > 0) --frame.opIndex;
		frame.functionId = GetFunctionIndexFromCodeOffset(frame.opIndex * (int)sizeof(SVMOperation));
		if (frame.functionId >= 0)
		{
			auto itName = _pProgram->debugFunctionNames.find(frame.functionId);
			if (itName != _pProgram->debugFunctionNames.end())
				frame.functionName = itName->second.substr(0, 4096);
		}
		frame.file = (frame.opIndex >= 0 && frame.opIndex < (int)DebugData().size()) ? DebugData()[frame.opIndex]._fileseq : -1;
		frame.line = GetDebugLine(frame.opIndex);
		if (frame.functionId >= 0)
		{
			const SFunctionTable& fun = Functions()[frame.functionId];
			frame.argsCount = fun._argsCount;
			frame.localCount = fun._localVarCount;
			frame.tempCount = fun._localTempMax;
		}
		frames.push_back(frame);
	}
}
void CNeoVMWorker::DebugGetFrameVariables(int frameId, std::vector<NeoDebugVariable>& vars)
{
	vars.clear();
    NeoDebugSnapshotBudget budget;
	// Worker가 실행 컨텍스트에 바인딩되기 전에는 변수 스택이 없다.
	// 이 시점의 디버그 UI 조회는 빈 변수 목록으로 처리한다.
	if (m_pVarStack_Base == nullptr)
		return;

	std::vector<NeoDebugStackFrame> frames;
	DebugGetStackTrace(frames);
	if (frameId < 0 || frameId >= (int)frames.size())
		return;

	const NeoDebugStackFrame& frame = frames[frameId];
	int base = frame.stackBase;
	int total = 1 + frame.argsCount + frame.localCount + frame.tempCount;
	auto itFunNames = _pProgram->debugVarNames.find(frame.functionId);
	bool hasDebugVarNames = itFunNames != _pProgram->debugVarNames.end();
	// 태그 없는 슬롯은 프레임 함수의 배치표가 타입을 준다. 창 슬롯과 범위 밖은 알 수 없는 값으로 본다.
	const SFunctionTable* layout = frame.functionId >= 0 && frame.functionId < (int)Functions().size()
		? &Functions()[frame.functionId] : nullptr;
	auto slotType = [&](int slot) -> VAR_TYPE
	{
		return layout != nullptr && slot < (int)layout->_slotTypes.size() ? layout->_slotTypes[slot] : VAR_NONE;
	};
	for (int i = 0; i < total && budget.nodes > 0; ++i)
	{
		int stackIndex = base + i;
		if (stackIndex < 0 || stackIndex >= (int)m_pVarStack_Base->size())
			continue;

		NeoDebugVariable var;
		if (i == 0)
			var.name = "return";
		else
		{
			if (hasDebugVarNames)
			{
				auto itName = itFunNames->second.find(i);
				if (itName != itFunNames->second.end())
					var.name = budget.Text(itName->second);
			}
			if (hasDebugVarNames && var.name.empty())
				continue;
			if (var.name.empty())
			{
				if (i <= frame.argsCount)
					var.name = "arg" + std::to_string(i);
				else if (i <= frame.argsCount + frame.localCount)
					var.name = "local" + std::to_string(i - frame.argsCount);
				else
					var.name = "temp" + std::to_string(i - frame.argsCount - frame.localCount);
			}
		}

		var.stackIndex = stackIndex;
		NeoDebugFormatValue(ValueRef(&(*m_pVarStack_Base)[stackIndex], slotType(i)), var, budget);
		if (i == 0 && var.type == "none")
			continue;
		vars.push_back(var);
	}
	// 선택한 호출 프레임에서도 전역을 표시한다. 같은 이름의 지역값은 앞에서 먼저 조회된다.
	{
		for (auto it = _pProgram->debugGlobalNames.begin(); it != _pProgram->debugGlobalNames.end() && budget.nodes > 0; ++it)
		{
			int globalIndex = it->first;
			if (!_pProgram->IsGlobalVariable(globalIndex))
				continue;

			NeoDebugVariable var;
			var.name = budget.Text(it->second);
			var.stackIndex = globalIndex;
			NeoDebugFormatValue(GetVarPtr_G(static_cast<short>(globalIndex)), var, budget);
			vars.push_back(var);
		}
	}
}
NEOS_NOINLINE bool CNeoVMWorker::ReportRunException()
{
	try
	{
		try { throw; }
		catch (const std::length_error& error) { SetError(error.what()); }
		catch (...) { SetError(RTE_EXCEPTION); }
		const int sourceIndex = m_iLastErrorSourceOPIndex >= 0 ? m_iLastErrorSourceOPIndex : _isErrorOPIndex;
		bool blDebugInfo = IsDebugInfo();
		int _lineseq = -1;
		if (blDebugInfo)
			_lineseq = GetDebugLine(sourceIndex);

		char chMsg[256];
		snprintf(chMsg, sizeof(chMsg), "%s : IP(%d), Line(%d)", GetVM()->_pErrorMsg.c_str(), sourceIndex, _lineseq);

		GetVM()->_sErrorMsgDetail = std::string(chMsg) + FormatStackTrace(_isErrorOPIndex);
		if (m_pDebugListener || m_iDebugBreakCount > 0 || m_eDebugRunMode != DBG_CONTINUE || m_bDebugPauseRequested)
		{
			if (_isErrorOPIndex >= 0 && _isErrorOPIndex < (int)DebugData().size())
				StopDebug(_isErrorOPIndex, NEO_DEBUG_STOP_EXCEPTION);
		}
		return false;
	}
	catch (const std::bad_alloc&)
	{
		// 일반 예외의 상세 메시지를 만드는 도중에도 OOM이 날 수 있다.
		PoisonOutOfMemory();
		return true;
	}
}

bool	CNeoVMWorker::Run()
{
	if (m_bOutOfMemoryPoisoned)
		return false;
	bool b = true;
	const bool nestedRun = m_bInRun;
	const bool previousSliceExpired = m_bSliceExpired;
	if (!nestedRun)
		m_bSliceExpired = false;
	struct SliceStateGuard
	{
		bool* flag;
		bool restore;
		bool previous;
		~SliceStateGuard() { if (restore) *flag = previous; }
	} sliceStateGuard{ &m_bSliceExpired, nestedRun, previousSliceExpired };
	// 인터프리터 진입 표시(중첩 Run 대비 저장/복원). CancelExecution 이 이걸 보고 거부한다.
	struct InRunGuard
	{
		bool* p; bool prev;
		InRunGuard(bool* f) : p(f), prev(*f) { *f = true; }
		~InRunGuard() { *p = prev; }
	} inRunGuard(&m_bInRun);
	try
	{
		bool debugActive = (m_iDebugSuppressCount == 0) &&
			(m_iNativeScriptCallDepth == 0) &&
			(m_iDebugBreakCount > 0 ||
			 m_eDebugRunMode != DBG_CONTINUE ||
			 m_bDebugPauseRequested);
		// 최상위 실행/재개는 완전 완료(depth 0)까지. 이 플래그는 최외곽 Run 에만 적용되어야 하므로
		// 즉시 소비(reset)한다 → 실행 중 발생하는 중첩(C++→스크립트) Run 은 현재 깊이에서 복귀.
		bool topExec = m_bTopExec;
		m_bTopExec = false;
		int breakingCallStack = (topExec || debugActive) ? 0 : (int)m_pCallStack->size();
		if (m_iTimeout >= 0 || (m_instructionBudget != 0 && !nestedRun))
			b = debugActive ? RunInternal<true, true>(breakingCallStack) : RunInternal<true, false>(breakingCallStack);
		else
			b = debugActive ? RunInternal<false, true>(breakingCallStack) : RunInternal<false, false>(breakingCallStack);
		// handle_ERROR reports the source location to the debugger, but the VM result
		// must remain an error so RunSettle releases this failed execution context.
	}
	catch (...)
	{
		// 정상 실행 경로에는 기준 버전과 같이 하나의 예외 경계만 남긴다. OOM 판별과
		// 오류 detail 생성은 catch funclet/콜드 함수로 밀어 opcode 코드 배치를 보존한다.
		bool outOfMemory = false;
		try
		{
			throw;
		}
		catch (const std::bad_alloc&)
		{
			outOfMemory = true;
		}
		catch (...)
		{
		}

		if (outOfMemory)
		{
			// OOM 직후에는 stack trace/detail 문자열을 만들지 않는다. 그 자체가 다시
			// bad_alloc을 던져 프로세스를 종료시킬 수 있기 때문이다.
			PoisonOutOfMemory();
			if (nestedRun)
				throw;
			return false;
		}

		if (ReportRunException() && nestedRun)
			throw std::bad_alloc();
		return false;
	}
	return b;
}
void CNeoVMWorker::JumpAsyncMsg()
{
	if (m_pCur == nullptr)
	{
		SetError(RTE_ASYNC_NO_CONTEXT);
		return;
	}

	const int resumeCodePtr = GetCodeptr();
	if (resumeCodePtr < int(2 * sizeof(SVMOperation))) // 0 : Error, 1 : Idle(Already?)
		return;

	// NOP_IDLE의 jump operand를 변경하지 않는다. 중첩 async.wait는 현재
	// 실행 컨텍스트의 resume stack으로 분리되어 LIFO 순서로 복귀한다.
	AsyncResumeInfo resume;
	resume.codePtr = resumeCodePtr;
	resume.pReturnValue = GetReturnVar();
	m_pCur->m_sAsyncResumeCodePtrs.push_back(resume);
	SetCodePtr(sizeof(SVMOperation));
}
// 원소 읽기·쓰기는 루프 안에, 생성·추가는 루프 밖 ExecuteColdOp에 둔다.
#define NEOCORE_ARRAY_ACCESS_DISPATCH(NAME, ELEMENT) \
		case NOP_ARRAY_##NAME##_GET: TypedArrayGet<NeoArrayElementType::ELEMENT>(OP); break; \
		case NOP_ARRAY_##NAME##_SET: TypedArraySet<NeoArrayElementType::ELEMENT>(OP); break;
#define NEOCORE_ARRAY_COLD_DISPATCH(NAME, ELEMENT) \
		case NOP_ARRAY_##NAME##_NEW: TypedArrayNew<NeoArrayElementType::ELEMENT>(OP); break; \
		case NOP_ARRAY_##NAME##_APPEND: TypedArrayAppend<NeoArrayElementType::ELEMENT>(OP); break;
#define NEOCORE_SCALAR_CONTAINER_DISPATCH(OWNER, INDEX, TYPE, TAG) \
        case NOP_##OWNER##_##TYPE##_LOAD: ScalarContainerLoad<INDEX, TAG>(GetVarRawF1(OP), GetVarRaw2(OP), GetVarRaw3(OP)); break; \
        case NOP_##OWNER##_##TYPE##_STORE: ScalarContainerStore<INDEX, TAG>(GetVarRawF1(OP), GetVarRaw2(OP), GetVarRaw3(OP)); break;
#define NEOCORE_SCALAR_OWNER_DISPATCH(OWNER, INDEX) \
        NEOCORE_SCALAR_CONTAINER_DISPATCH(OWNER, INDEX, I64, VAR_INT) \
        NEOCORE_SCALAR_CONTAINER_DISPATCH(OWNER, INDEX, F64, VAR_FLOAT) \
        NEOCORE_SCALAR_CONTAINER_DISPATCH(OWNER, INDEX, BOOL, VAR_BOOL)
#define NEOCORE_SCALAR_ARRAY_STORE(TYPE, ELEMENT) \
        case NOP_ARRAY_##TYPE##_STORE: ScalarArrayStore<NeoArrayElementType::ELEMENT>(GetVarRawF1(OP), GetVarRaw2(OP), GetVarRaw3(OP)); break;

// 드문 명령의 처리. 디스패치 루프를 작게 유지하려고 루프 밖에 둔다. 실행을 멈춰야 하면 true를 돌려준다.
// 뜨거운 명령의 목록은 벤치·스모크의 동적 계측과 게임 스크립트의 정적 빈도로 정했다(RunInternal 참조).
NEOS_NOINLINE bool CNeoVMWorker::ExecuteColdOp(const SVMOperation& OP)
{
	switch (OP.op)
	{
	case NOP_LOADK:         handle_LOADK_cold(OP); break;
	case NOP_LOG_NOT:       handle_LOG_NOT_cold(OP); break;
	case NOP_COMPARE:       handle_COMPARE(OP); break;
	case NOP_JMP_COMPARE:   handle_JMP_COMPARE(OP); break;
	case NOP_JMP_COMPARE_L: handle_JMP_COMPARE_L(OP); break;
	case NOP_SWITCH:        handle_SWITCH(OP); break;
	case NOP_TOINT:         handle_TOINT(OP); break;
	case NOP_TOINT_L:       handle_TOINT_L(OP); break;
	case NOP_TOFLOAT:       handle_TOFLOAT(OP); break;
	case NOP_SLEEP:         return handle_SLEEP(OP);
	case NOP_FMOV1:         handle_FMOV1(OP); break;
	case NOP_FMOV2:         handle_FMOV2(OP); break;
	case NOP_PTRCALL:       handle_PTRCALL(OP); break;
	case NOP_PTRCALL_L:     handle_PTRCALL_L(OP); break;
	case NOP_TABLE_ALLOC:   handle_TABLE_ALLOC(OP); break;
	case NOP_CLT_READ:      handle_CLT_READ(OP); break;
	case NOP_CLT_READ_L:    handle_CLT_READ_L(OP); break;
	case NOP_CLT_MOV:       handle_CLT_MOV(OP); break;
	case NOP_CLT_MOV_L:     handle_CLT_MOV_L(OP); break;
	case NOP_LIST_ALLOC:    handle_LIST_ALLOC(OP); break;
	case NOP_LIST_ALLOC_L:  handle_LIST_ALLOC_L(OP); break;
	case NOP_CHANGE_INT:    handle_CHANGE_INT(OP); break;
	case NOP_CHANGE_INT_L:  handle_CHANGE_INT_L(OP); break;
	case NOP_YIELD:         return handle_YIELD(OP);
	case NOP_IDLE:          handle_IDLE(OP); break;
	case NOP_VEC_MAKE:      handle_VEC_MAKE(OP); break;
	case NOP_CLT_READ_STATIC_STRING: handle_CLT_READ_STATIC_STRING(OP); break;
	case NOP_JMP_RANGE_INSIDE: if (IsRangeInside<true>(OP, false)) SetComparisonJump(OP.n1); break;
	case NOP_JMP_RANGE_OUTSIDE: if (IsRangeInside(OP, false) == false) SetComparisonJump(OP.n1); break;
	case NOP_JMP_RANGE_INSIDE_L: if (IsRangeInside<true>(OP, true)) SetComparisonJump(OP.n1); break;
	case NOP_JMP_RANGE_OUTSIDE_L: if (IsRangeInside(OP, true) == false) SetComparisonJump(OP.n1); break;
	case NOP_CLT_WRITE_REF: handle_CLT_WRITE_REF(OP); break;
	// 펼치지 못한 조합은 세부 연산을 실행 중 고르는 기존 경로로 간다.
	case NOP_I64_ARITH: handle_I64_ARITH<false>(OP); break;
	case NOP_F64_ARITH: handle_F64_ARITH<false>(OP); break;
	case NOP_NUM_CONVERT: handle_NUM_CONVERT(OP); break;
	case NOP_I64_COMPARE: handle_I64_COMPARE(OP); break;
	case NOP_F64_COMPARE: handle_F64_COMPARE(OP); break;
	case NOP_JMP_I64_COMPARE: handle_JMP_I64_COMPARE(OP); break;
	case NOP_JMP_F64_COMPARE: handle_JMP_F64_COMPARE(OP); break;
	case NOP_I64_FIXED: handle_I64_ARITH<true>(OP); break;
	case NOP_F64_FIXED: handle_F64_ARITH<true>(OP); break;
	case NOP_V2_MAKE: TypedVectorMake<2>(OP); break;
	case NOP_V2_GET: TypedVectorGet<2>(OP); break;
	case NOP_V2_SET: TypedVectorSet<2>(OP); break;
	case NOP_V3_MAKE: TypedVectorMake<3>(OP); break;
	case NOP_V3_GET: TypedVectorGet<3>(OP); break;
	case NOP_V3_SET: TypedVectorSet<3>(OP); break;
	case NOP_V4_MAKE: TypedVectorMake<4>(OP); break;
	case NOP_V4_GET: TypedVectorGet<4>(OP); break;
	case NOP_V4_SET: TypedVectorSet<4>(OP); break;
	case NOP_CELL_NEW: TypedCellNew(OP); break;
	case NOP_CELL_GET: TypedCellGet(OP); break;
	case NOP_CELL_SET: TypedCellSet(OP); break;
	NEOCORE_ARRAY_COLD_DISPATCH(I64, Int)
	NEOCORE_ARRAY_COLD_DISPATCH(F64, Float)
	NEOCORE_ARRAY_COLD_DISPATCH(BOOL, Bool)
	NEOCORE_ARRAY_COLD_DISPATCH(V2, Vector2)
	NEOCORE_ARRAY_COLD_DISPATCH(V3, Vector3)
	NEOCORE_ARRAY_COLD_DISPATCH(V4, Vector4)
	NEOCORE_ARRAY_COLD_DISPATCH(I32, Int32)
	NEOCORE_ARRAY_COLD_DISPATCH(F32, Float32)
	case NOP_ARRAY_LEN:
		if (CheckTypedArrayReceiver(GetVarRaw2(OP)))
			StoreNumberInt(GetVarRawF1(OP), GetVarRaw2(OP)->_array->_count);
		break;
	case NOP_ARRAY_RESIZE: TypedArrayResize(OP); break;
	case NOP_ARRAY_RESERVE: TypedArrayReserve(OP); break;
	case NOP_SET_I64_CONTAINS: TypedSetOperation<false, 0>(OP); break;
	case NOP_SET_I64_ADD: TypedSetOperation<false, 1>(OP); break;
	case NOP_SET_I64_REMOVE: TypedSetOperation<false, 2>(OP); break;
	case NOP_SET_STRING_CONTAINS: TypedSetOperation<true, 0>(OP); break;
	case NOP_SET_STRING_ADD: TypedSetOperation<true, 1>(OP); break;
	case NOP_SET_STRING_REMOVE: TypedSetOperation<true, 2>(OP); break;
	case NOP_V2_ARITH: handle_V2_ARITH(OP); break;
	case NOP_V3_ARITH: handle_V3_ARITH(OP); break;
	case NOP_V4_ARITH: handle_V4_ARITH(OP); break;
	case NOP_NONE:          handle_NONE(OP); break;
	default:
		SetError(RTE_UNKNOWN_OP);
		break;
	}
	return false;
}

template<bool TIMEOUT, bool DEBUG>
bool	CNeoVMWorker::RunInternal(int iBreakingCallStack)
{
	if (false == _isInitialized)
		return false;

	m_bCooperativeSleepPending = false;
	if (_iRemainSleep > 0)
	{
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _sleepClock).count();
		if (elapsed > 0)
		{
			_iRemainSleep -= (int)elapsed;
			// 소비한 정수 밀리초만큼만 기준을 전진시킨다. now 로 덮으면 1ms 미만 잔여가
			// 매번 버려져서, 1ms 보다 자주 호출하면 sleep 이 영원히 안 끝난다.
			_sleepClock += std::chrono::milliseconds(elapsed);
		}

		if (_iRemainSleep > 0)
			return true;
	}

	m_iBreakingCallStack = iBreakingCallStack;

	//SCallStack callStack;
//	int iTemp;
//	char chMsg[256];

	m_op_process = m_iCheckOpCount;
	const bool countInstructions = TIMEOUT && m_instructionBudget != 0 && m_iNativeScriptCallDepth == 0;
	uint64_t remainingInstructions = m_instructionBudget;
	if (TIMEOUT && m_iTimeout >= 0)
		_preClock = std::chrono::steady_clock::now();

	while (true)
	{
		if (TIMEOUT)
		{
			// 마지막 허용 명령이 실패해도 오류 처리를 다음 슬라이스로 미루지 않는다.
			if (countInstructions && remainingInstructions == 0 && !IsErrorTrap())
			{
				m_bSliceExpired = true;
				break;
			}
			// 디버거 정지로 반환하면 이 지역 계수는 버려지므로 여기서 미리 줄여도 결과가 같다.
			if (countInstructions && remainingInstructions != 0) --remainingInstructions;
			if (m_iTimeout >= 0 && --m_op_process <= 0)
			{
				m_op_process = m_iCheckOpCount;
//				if (isTimeout)
				{
					auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now() - _preClock).count();
					if (elapsed >= m_iTimeout || elapsed < 0)
					{
						m_bSliceExpired = true;
						break;
					}
				}
			}
		}

		if (DEBUG)
		{
			int iOPIndex = int((u8*)_pCodeCurrent - _pCodeBegin) / sizeof(SVMOperation);
			if (CheckDebugStop(iOPIndex))
				return true;
		}

		const SVMOperation& OP = *GetOP();
		// 뜨거운 명령만 이 switch에 둔다. 드문 명령은 ExecuteColdOp으로 보내 루프를 작게 유지한다.
		// 루프가 크면 컴파일러가 레지스터를 잃고 꼬리를 합쳐 뜨거운 명령마다 점프가 하나씩 늘었다(실측).
		switch (OP.op)
		{
		case NOP_MOV:           handle_MOV(OP); break;
		case NOP_VAR_CLEAR:     handle_VAR_CLEAR(OP); break;
		case NOP_JMP:           SetCodeIncPtr(OP.n1); break;
		// 조건은 컴파일러가 bool로 확정했다. payload만 읽는다.
		case NOP_JMP_FALSE:     if (!GetVarRaw2(OP)->_bl) SetCodeIncPtr(OP.n1); break;
		case NOP_JMP_TRUE:      if (GetVarRaw2(OP)->_bl) SetCodeIncPtr(OP.n1); break;
		case NOP_JMP_FOR:       if (For(GetVarRaw_L(OP.n2))) SetCodeIncPtr(OP.n1); break;
		case NOP_JMP_FOREACH:
			// 키·값·반복자·버전 슬롯은 n3부터 연속이다. 값 슬롯은 자기 선언 타입의 뷰로 넘긴다.
			if (ForEach(GetVarPtr2(OP), GetVarPtr3(OP),
				(OP.argFlag & NEOS_ARG_N3_LOCAL) ? GetVarPtr_L(OP.n3 + 1) : GetVarPtr_G(OP.n3 + 1),
				(OP.argFlag & NEOS_OP_FOREACH_TWOVAR) != 0)) SetCodeIncPtr(OP.n1);
			break;
		case NOP_STR_ADD:       handle_STR_ADD(OP); break;
		case NOP_STR_ADD_L:     handle_STR_ADD_L(OP); break;
		case NOP_TOSTRING:      handle_TOSTRING(OP); break;
		case NOP_TOSIZE:        handle_TOSIZE(OP); break;
		case NOP_CALL:          handle_CALL(OP); break;
		case NOP_CALL_L:        handle_CALL_L(OP); break;
		case NOP_PTRCALL2:      handle_PTRCALL2(OP); break;
		case NOP_PTRCALL2_L:    handle_PTRCALL2_L(OP); break;
		case NOP_NATIVECALL:    handle_NATIVECALL(OP); break;
		case NOP_RETURN:
			if (handle_RETURN(OP)) return true;
			break;
		case NOP_RETURN_L:
			if (handle_RETURN_L(OP)) return true;
			break;
		// 타입이 확정된 범위 점프는 오류가 없어 점프 자체만 한다.
		case NOP_RT_JMP_I64_RANGE_INSIDE: if (IsRangeInsideRT<false, true>(OP)) SetCodeIncPtr(OP.n1); break;
		case NOP_RT_JMP_I64_RANGE_OUTSIDE: if (!IsRangeInsideRT<false, false>(OP)) SetCodeIncPtr(OP.n1); break;
		case NOP_RT_JMP_F64_RANGE_INSIDE: if (IsRangeInsideRT<true, true>(OP)) SetCodeIncPtr(OP.n1); break;
		case NOP_RT_JMP_F64_RANGE_OUTSIDE: if (!IsRangeInsideRT<true, false>(OP)) SetCodeIncPtr(OP.n1); break;
		// 로드가 펼친 런타임 opcode. 세부 연산이 템플릿 인자라 switch 한 번으로 끝난다.
#define NEOCORE_RT_CASE_ARITH(T, N, S) \
		case NOP_RT_##T##_##N: T##ArithmeticRT<ArithmeticOp::S, false>(OP); break; \
		case NOP_RT_##T##_##N##2: T##ArithmeticRT<ArithmeticOp::S, true>(OP); break;
		NEOCORE_RUNTIME_I64_ARITH(NEOCORE_RT_CASE_ARITH)
		NEOCORE_RUNTIME_F64_ARITH(NEOCORE_RT_CASE_ARITH)
#undef NEOCORE_RT_CASE_ARITH
#define NEOCORE_RT_CASE_IMM(T, N, S) case NOP_RT_##T##_##N##_IMM: T##ArithmeticImmRT<ArithmeticOp::S>(OP); break;
		NEOCORE_RUNTIME_I64_IMM_ARITH(NEOCORE_RT_CASE_IMM)
#undef NEOCORE_RT_CASE_IMM
#define NEOCORE_RT_CASE_JMP_IMM(T, N, S) case NOP_RT_JMP_##T##_##N##_IMM: T##JumpCompareImmRT<ComparisonOp::S>(OP); break;
		NEOCORE_RUNTIME_COMPARE(NEOCORE_RT_CASE_JMP_IMM, I64)
#undef NEOCORE_RT_CASE_JMP_IMM
		case NOP_RT_I64_NEG: I64ArithmeticRT<ArithmeticOp::Neg, false>(OP); break;
		case NOP_RT_F64_NEG: F64ArithmeticRT<ArithmeticOp::Neg, false>(OP); break;
		case NOP_RT_I64_TO_F64: StoreNumberFloat(GetVarRawF1(OP), static_cast<NS_FLOAT>(GetVarRaw2(OP)->_int)); break;
		case NOP_RT_F64_TO_I64: StoreNumberInt(GetVarRawF1(OP), NeoFloatToInt(GetVarRaw2(OP)->_float)); break;
#define NEOCORE_RT_CASE_COMPARE(T, N, S) \
		case NOP_RT_##T##_##N: T##CompareRT<ComparisonOp::S>(OP); break; \
		case NOP_RT_JMP_##T##_##N: T##JumpCompareRT<ComparisonOp::S>(OP); break;
		NEOCORE_RUNTIME_COMPARE(NEOCORE_RT_CASE_COMPARE, I64)
		NEOCORE_RUNTIME_COMPARE(NEOCORE_RT_CASE_COMPARE, F64)
#undef NEOCORE_RT_CASE_COMPARE
		// 타입이 확정된 컨테이너 원소 접근과 맵 조회.
		case NOP_MAP_I64_READ: TypedMapRead<false, false>(OP); break;
		case NOP_MAP_STRING_READ: TypedMapRead<true, false>(OP); break;
		case NOP_MAP_I64_CONTAINS: TypedMapRead<false, true>(OP); break;
		case NOP_MAP_STRING_CONTAINS: TypedMapRead<true, true>(OP); break;
		NEOCORE_SCALAR_OWNER_DISPATCH(LIST, 0)
		NEOCORE_SCALAR_OWNER_DISPATCH(MAP_INT, 1)
		NEOCORE_SCALAR_OWNER_DISPATCH(MAP_STR, 2)
		NEOCORE_SCALAR_ARRAY_STORE(I64, Int)
		NEOCORE_SCALAR_ARRAY_STORE(F64, Float)
		NEOCORE_SCALAR_ARRAY_STORE(BOOL, Bool)
		NEOCORE_SCALAR_ARRAY_STORE(I32, Int32)
		NEOCORE_SCALAR_ARRAY_STORE(F32, Float32)
		NEOCORE_ARRAY_ACCESS_DISPATCH(I64, Int)
		NEOCORE_ARRAY_ACCESS_DISPATCH(F64, Float)
		NEOCORE_ARRAY_ACCESS_DISPATCH(BOOL, Bool)
		NEOCORE_ARRAY_ACCESS_DISPATCH(V2, Vector2)
		NEOCORE_ARRAY_ACCESS_DISPATCH(V3, Vector3)
		NEOCORE_ARRAY_ACCESS_DISPATCH(V4, Vector4)
		NEOCORE_ARRAY_ACCESS_DISPATCH(I32, Int32)
		NEOCORE_ARRAY_ACCESS_DISPATCH(F32, Float32)
		case NOP_PREPARE_CALL: SelectCallPlan(OP.n1); break;
		case NOP_ERROR:
			handle_ERROR(OP);
			return false;
		default:
			if (ExecuteColdOp(OP)) return true;
			break;
		}
	}
	return true;
}

#undef NEOCORE_ARRAY_ACCESS_DISPATCH
#undef NEOCORE_ARRAY_COLD_DISPATCH
#undef NEOCORE_SCALAR_ARRAY_STORE
#undef NEOCORE_SCALAR_CONTAINER_DISPATCH
#undef NEOCORE_SCALAR_OWNER_DISPATCH

void	CNeoVMWorker::DeadCoroutine(CoroutineInfo* pCI)
{
	if (pCI == nullptr)
		return;
    if (!m_sDebugLineSkips.empty()) PruneDebugLineSkips(pCI, -1);
	DrainCoroutineClosures(pCI);
	pCI->_state = COROUTINE_STATE_DEAD;
	// 저장된 프레임 배치로 소유 슬롯을 해제하고 사용 구간을 비운다.
	ReleaseContextFrames(pCI);

	pCI->_info.ClearSP();
	pCI->m_sAsyncWaitReturnStack.clear();
}

void CNeoVMWorker::DrainCoroutineClosures(CoroutineInfo* pCI)
{
	if (pCI == nullptr)
		return;

	// cancel/close는 RET를 밟지 않는다. 캡처 슬롯을 먼저 보관함으로 돌린 뒤,
	// active 또는 일반 호출 프레임이 들고 있던 실행 참조를 정확히 한 번 반납한다.
	ClosureInfo*& activeClosure = pCI->_activeClosure;
	if (activeClosure != nullptr)
	{
		SyncClosureAtFrame(activeClosure, pCI->_info._iSP_Vars, nullptr, &pCI->m_sVarStack);
		BoundaryValue active(VAR_CLOSURE);
		active._closure = activeClosure;
		if (m_pActiveClosure == activeClosure)
			m_pActiveClosure = nullptr;
		activeClosure = nullptr;
		Var_Release(&active);
	}

	for (int i = (int)pCI->m_sClosureCallStack.size() - 1; i >= 0; --i)
	{
		SClosureCallState& state = pCI->m_sClosureCallStack[i];
		if (state._closure == nullptr)
			continue;
		SyncClosureAtFrame(state._closure, state._iSP_Vars, nullptr, &pCI->m_sVarStack);
		BoundaryValue active(VAR_CLOSURE);
		active._closure = state._closure;
		state._closure = nullptr;
		Var_Release(&active);
	}
	pCI->m_sClosureCallStack.clear();
}
bool CNeoVMWorker::StopCoroutine(bool doDead)
{
	if (m_pCur == nullptr || (doDead && IsMainCoroutine(m_pCur)))
	{
		SetError("cannot close the main coroutine");
		return false;
	}
	CoroutineBase* pThis = (CoroutineBase*)this;
	m_pCur->_info = *pThis;
	m_pCur->_activeClosure = m_pActiveClosure;
	m_pActiveClosure = nullptr;
	if (doDead)
		DeadCoroutine(m_pCur);
	else
		m_pCur->_state = COROUTINE_STATE_SUSPENDED;

	if (m_sCoroutines.empty() == true)
	{
		return false;
	}
	else
	{
		auto it = m_sCoroutines.begin();
		m_pCur = (*it);
		m_sCoroutines.erase(it);
		if(m_pCur->_state != COROUTINE_STATE_NORMAL)
		{
			SetError(RTE_COROUTINE_STATE);
			return false;
		}
		m_pCur->_state = COROUTINE_STATE_RUNNING;
		SetVarStackBase(&m_pCur->m_sVarStack);
		m_pCallStack = &m_pCur->m_sCallStack;
		m_pClosureCallStack = &m_pCur->m_sClosureCallStack;
		m_pActiveClosure = m_pCur->_activeClosure;

		*pThis = m_pCur->_info;
		SetStackPointer(_iSP_Vars);
		RestoreFrameTables();
		// 이 컨텍스트가 coroutine.resume으로 넘겨 둔 호출 창을 이제 놓는다. 창이 잡은 코루틴
		// 참조 덕분에 임시 코루틴도 전환 동안 살아 있었다. 결과 슬롯은 호출자가 가져간다.
		if (_activePlan != nullptr)
		{
			ReleaseCallWindow(*_activePlan, GetStackFromBase(_iSP_VarsMax), false);
			_activePlan = nullptr;
		}
	}
	return true;
}

bool CNeoVMWorker::StartCoroutione(int argSP_Vars, int n3)
{
	if (m_pRegisterActive && m_pRegisterActive->_function.GetType() == VAR_CLOSURE &&
		(m_pRegisterActive->_function._closure == nullptr ||
		m_pRegisterActive->_function._closure->_ownerWorkerId != GetWorkerID()))
	{
		SetError(RTE_CALL_INVALID);
		return false;
	}
	CoroutineBase* pThis = (CoroutineBase*)this;
	if (m_pCur)
	{
		// Back up
		m_pCur->_info = *pThis;
		m_pCur->_activeClosure = m_pActiveClosure;
		//m_pCur->_info._iSP_Vars = sp;// _iSP_Vars;
		m_pCur->_state = COROUTINE_STATE_NORMAL;
		m_sCoroutines.push_front(m_pCur);
		CoroutineInfo* pPre = m_pCur;

		m_pCur = m_pRegisterActive;
		m_pCur->_program = _pProgram;
		m_pCur->_state = COROUTINE_STATE_RUNNING;
		m_pCur->_sub_state = COROUTINE_SUB_NORMAL;
		SetVarStackBase(&m_pCur->m_sVarStack);
		m_pCallStack = &m_pCur->m_sCallStack;
		m_pClosureCallStack = &m_pCur->m_sClosureCallStack;
		m_pActiveClosure = m_pCur->_activeClosure;

		if (m_pCur->_info._pCodeCurrent == nullptr) // first run
		{
            // 중단된 코루틴이 해제된 뒤 풀 주소가 재사용되어도 이전 기록은 이어받지 않는다.
            if (!m_sDebugLineSkips.empty()) PruneDebugLineSkips(m_pCur, -1);
			int iResumeParamCount = n3 - 1;
			const SFunctionTable& fun = Functions()[m_pCur->_fun_index];
			if (!EnsureStackRange(0, fun._stackSlotCount - 1))
				return false;
			// 새 컨텍스트의 프레임 구간을 비우고 배치를 고른다. 인자는 부모의 호출 창에서 복사한다.
			// 부모 창은 부모가 다시 실행될 때 놓는다(StopCoroutine).
			memset(&m_pCur->m_sVarStack[0], 0, sizeof(VarInfo) * fun._localAddCount);
			EnterFrame(m_pCur->_fun_index, 0);
			_activePlan = nullptr;
			const CallStoragePlan* resumePlan = pPre->_info._activePlan;
			for (int i = 0; i < fun._argsCount && i < iResumeParamCount; i++)
			{
				const VAR_TYPE given = resumePlan != nullptr && 2 + i < (int)resumePlan->types.size()
					? resumePlan->types[2 + i] : VAR_NONE;
				Move(ValueRef(&m_pCur->m_sVarStack[i + 1], fun._slotTypes[1 + i]),
					ValueRef(&pPre->m_sVarStack[i + argSP_Vars + 2], given));
			}
			SetCodePtr(fun._codePtr);
			_iSP_Vars = 0;// iSP_VarsMax;
			_iSP_VarsMax = _iSP_Vars + fun._localAddCount;
			_iSP_Vars_Max2 = _iSP_Vars + fun._stackSlotCount;
			if (m_pCur->_function.GetType() == VAR_CLOSURE)
			{
				ClosureInfo* closure = m_pCur->_function._closure;
				if (closure == nullptr || closure->_funIndex != m_pCur->_fun_index ||
					closure->_captures.size() != fun._captures.size())
					return false;
				for (size_t i = 0; i < fun._captures.size(); ++i)
					Move(GetVarPtr_L(fun._captures[i].second), closure->Capture(i));
				++closure->_refCount;
				m_pActiveClosure = closure;
				m_pCur->_activeClosure = closure;
			}
			m_pCur->_info._pCodeCurrent = _pCodeCurrent;
		}
		else
		{
			*pThis = m_pCur->_info;
			RestoreFrameTables();
		}
		SetStackPointer(_iSP_Vars);
	}
	m_pRegisterActive = nullptr;
	return true;
}


// 네이티브 안에서 스크립트 함수를 동기 호출한다. 자식 프레임은 네이티브 창 위(_iSP_Vars_Max2)에 두어
// 네이티브가 아직 읽는 창 인자를 덮지 않는다. 결과는 워커 보관함으로 옮겨 돌려준다.
ValueRef CNeoVMWorker::testCall(int iFID, ValueRef args, int argc)
{
	if (_isInitialized == false || !CheckNestedCallDepth())
		return nullptr;

	const SFunctionTable& fun = Functions()[iFID];
	if (argc != fun._argsCount)
		return nullptr;
	const int base = _iSP_Vars_Max2;
	if (!EnsureStackRange(base, fun._stackSlotCount - 1))
		return nullptr;
	ActiveClosureIsolation activeClosureIsolation(m_pActiveClosure);
	STestCallParent parent;
	bool ok = BeginTestCall(fun, iFID, base, args, argc, parent);
	if (ok)
	{
		++m_iDebugSuppressCount;
		ok = Run();
		--m_iDebugSuppressCount;
	}
	return FinishTestCall(fun, base, ok, parent);
}

ValueRef CNeoVMWorker::testCall(ValueRef function, ValueRef args, int argc)
{
	if (function == nullptr || !CheckNestedCallDepth())
		return nullptr;
	if (function.GetType() == VAR_FUN)
		return testCall(function.FunctionIndex(), args, argc);
	if (function.GetType() != VAR_CLOSURE || function->_closure == nullptr ||
		_isInitialized == false)
		return nullptr;

	ClosureInfo* closure = function->_closure;
	if (closure->_ownerWorkerId != GetWorkerID())
	{
		SetError(RTE_CALL_INVALID);
		return nullptr;
	}
	if (closure->_funIndex < 0 || closure->_funIndex >= (int)Functions().size())
		return nullptr;
	const SFunctionTable& fun = Functions()[closure->_funIndex];
	const int base = _iSP_Vars_Max2;
	if (argc != fun._argsCount || closure->_captures.size() != fun._captures.size() ||
		!EnsureStackRange(base, fun._stackSlotCount - 1))
		return nullptr;
	ActiveClosureIsolation activeClosureIsolation(m_pActiveClosure);
	STestCallParent parent;
	bool ok = BeginTestCall(fun, closure->_funIndex, base, args, argc, parent);
	if (ok)
	{
		for (size_t i = 0; i < fun._captures.size(); ++i)
			Move(GetVarPtr_L(fun._captures[i].second), closure->Capture(i));
		++closure->_refCount; // RET/ERROR가 Sync 후 반납한다.
		m_pActiveClosure = closure;
		++m_iDebugSuppressCount;
		ok = Run();
		--m_iDebugSuppressCount;
	}
	return FinishTestCall(fun, base, ok, parent);
}

// 부모 레지스터는 호출별 보관함에 둔다. 자식의 중첩 testCall이 바깥 호출의 복원 상태를 덮지 않는다.
bool CNeoVMWorker::BeginTestCall(const SFunctionTable& fun, int functionIndex, int base, ValueRef args, int argc, STestCallParent& parent)
{
	parent._code = GetCodeptr();
	parent._iSP_Vars = _iSP_Vars;
	parent._iSP_VarsMax = _iSP_VarsMax;
	parent._iSP_Vars_Max2 = _iSP_Vars_Max2;
	parent._functionIndex = _functionIndex;
	parent._plan = _activePlan;

	SetCodePtr(fun._codePtr);
	_iSP_Vars = base;
	SetStackPointer(_iSP_Vars);
	memset(&(*m_pVarStack_Base)[base], 0, sizeof(VarInfo) * fun._localAddCount);
	EnterFrame(functionIndex, base);
	_activePlan = nullptr;
	_iSP_VarsMax = base + fun._localAddCount;
	if (_iSP_Vars_Max2 < base + fun._stackSlotCount)
		_iSP_Vars_Max2 = base + fun._stackSlotCount;
	for (int i = 0; i < argc; ++i)
		if (!StoreArgument(fun, base, i, args[i]))
		{
			SetError(RTE_CALL_ARG_TYPE);
			return false;
		}
	return true;
}

ValueRef CNeoVMWorker::FinishTestCall(const SFunctionTable& fun, int base, bool ok, const STestCallParent& parent)
{
	// 예외로 남은 자식 클로저를 부모 프레임 복원 전에 동기화하고 실행 참조를 반납한다.
	if (!ok) UnwindActiveClosures();
	// 결과 슬롯은 자식 프레임의 것이다. 소유권을 보관함으로 옮기고 슬롯을 비운다.
	Var_Release(&m_sTestCallResult);
	if (ok)
	{
		VarInfo* slot = &(*m_pVarStack_Base)[base];
		m_sTestCallResult = BoundaryFromSlot(slot, fun._resultType);
		slot->_int = 0;
	}
	// 실패한 자식 호출의 오류 트랩은 호스트가 실패를 삼켜도 유지한다.
	SetCodePtr(ok ? parent._code : 0);
	_iSP_Vars = parent._iSP_Vars;
	_iSP_VarsMax = parent._iSP_VarsMax;
	_iSP_Vars_Max2 = parent._iSP_Vars_Max2;
	SetStackPointer(_iSP_Vars);
	_functionIndex = parent._functionIndex;
	RestoreFrameTables();
	_activePlan = parent._plan;
	_isInitialized = true;
	return ok ? ValueRef(&m_sTestCallResult) : ValueRef(nullptr);
}

void CNeoVMWorker::CallFunctionValue(int functionIndex, int argumentCount, int returnValueIndex)
{
	if (functionIndex < 0 || functionIndex >= (int)Functions().size())
	{
		SetError(RTE_CALL_INVALID);
		return;
	}
	if (argumentCount != Functions()[functionIndex]._argsCount)
	{
		SetError(RTE_CALL_ARG_COUNT);
		return;
	}
	Call(functionIndex, argumentCount, returnValueIndex);
}

void CNeoVMWorker::Call(ClosureInfo* closure, int n2, int returnValueIndex)
{
	if (closure == nullptr || closure->_ownerWorkerId != GetWorkerID() || closure->_funIndex < 0 ||
		closure->_funIndex >= (int)Functions().size())
	{
		SetError(RTE_CALL_INVALID);
		return;
	}
	const SFunctionTable& fun = Functions()[closure->_funIndex];
	if (n2 != fun._argsCount)
	{
		SetError(RTE_CALL_ARG_COUNT);
		return;
	}
	if (closure->_captures.size() != fun._captures.size() ||
		!EnsureStackRange(_iSP_VarsMax, fun._stackSlotCount - 1))
	{
		SetError(RTE_CALL_INVALID);
		return;
	}

	// 저장되는 프레임은 호출자다. 오류 정리와 디버거가 _functionIndex로 배치를 찾는다.
#if _DEBUG
	SCallStack callStack;
	callStack._ReturnIP = (u32)GetCodeptr();
	callStack._iSP_Vars = _iSP_Vars;
	callStack._iSP_VarsMax = _iSP_VarsMax;
	callStack._iReturnValueIndex = returnValueIndex;
	callStack._functionIndex = _functionIndex;
	if (m_pActiveClosure != nullptr)
		callStack._ReturnIP |= FLAG_CLOSURE;
	m_pCallStack->push_back(callStack);
#else
	SCallStack& callStack = m_pCallStack->push_back();
	callStack._ReturnIP = (u32)GetCodeptr();
	callStack._iSP_Vars = _iSP_Vars;
	callStack._iSP_VarsMax = _iSP_VarsMax;
	callStack._iReturnValueIndex = returnValueIndex;
	callStack._functionIndex = _functionIndex;
	if (m_pActiveClosure != nullptr)
		callStack._ReturnIP |= FLAG_CLOSURE;
#endif
	// 캡처 함수 안에서 다른 캡처 함수를 호출한 경우에도 부모 프레임은
	// 일반 Call과 같은 보조 스택으로 복원한다.
	if (callStack._ReturnIP & FLAG_CLOSURE)
	{
		SClosureCallState& state = m_pClosureCallStack->push_back();
		state._closure = m_pActiveClosure;
		state._iSP_Vars = callStack._iSP_Vars;
		m_pActiveClosure = nullptr;
	}

	SetCodePtr(fun._codePtr);
	_iSP_Vars = _iSP_VarsMax;
	SetStackPointer(_iSP_Vars);
	// 인자는 호출자가 창에 넣었고 이제 피호출자 프레임의 것이다. 창 계획은 소비됐다.
	EnterFrame(closure->_funIndex, _iSP_Vars);
	_activePlan = nullptr;
	_iSP_VarsMax = _iSP_Vars + fun._localAddCount;
	if (_iSP_Vars_Max2 < _iSP_Vars + fun._stackSlotCount)
		_iSP_Vars_Max2 = _iSP_Vars + fun._stackSlotCount;

	for (size_t i = 0; i < fun._captures.size(); ++i)
		Move(GetVarPtr_L(fun._captures[i].second), closure->Capture(i));

	// 호출 중 콜백 값이 컨테이너에서 제거돼도 현재 실행 프레임은 살아 있어야 한다.
	++closure->_refCount;
	m_pActiveClosure = closure;
	(void)n2; // 호출 인자는 기존 Call과 동일하게 부모 프레임의 arg 슬롯에 이미 배치돼 있다.
}

void CNeoVMWorker::SyncActiveClosure(ValueRef keepOnStack)
{
	SyncClosureAtFrame(m_pActiveClosure, _iSP_Vars, keepOnStack);
}

NEOS_NOINLINE void CNeoVMWorker::FinishClosureReturn(VarInfo* keepOnStack)
{
	// closure 반환만 여기로 온다. 반환값 슬롯은 caller가 Move할 때까지 남겨 두고,
	// 나머지 캡처 슬롯은 보관함으로 되돌려 실행 보유 참조도 함께 반납한다.
	ClosureInfo* closure = m_pActiveClosure;
	if (closure == nullptr)
		return;
	// 뷰는 슬롯 주소 비교에만 쓰이므로 선언 타입이 필요 없다.
	SyncClosureAtFrame(closure, _iSP_Vars, ValueRef(keepOnStack, VAR_NONE));
	if (m_pCur && m_pCur->_activeClosure == closure)
		m_pCur->_activeClosure = nullptr;
	m_pActiveClosure = nullptr;
	BoundaryValue active(VAR_CLOSURE);
	active._closure = closure;
	Var_Release(&active);
}

void CNeoVMWorker::SyncClosureAtFrame(ClosureInfo* closure, int stackBase, ValueRef keepOnStack,
	ValueStack* varStack)
{
	if (closure == nullptr || closure->_funIndex < 0 ||
		closure->_funIndex >= (int)Functions().size())
		return;
	if (varStack == nullptr)
		varStack = m_pVarStack_Base;
	if (varStack == nullptr || stackBase < 0)
		return;
	const SFunctionTable& fun = Functions()[closure->_funIndex];
	if (closure->_captures.size() != fun._captures.size())
		return;
	for (size_t i = 0; i < fun._captures.size(); ++i)
	{
		const int sourceIndex = stackBase + fun._captures[i].second;
		if (sourceIndex < 0 || sourceIndex >= (int)varStack->size())
			return;
	}
	bool mayContainContainerChild = false;
	for (size_t i = 0; i < fun._captures.size(); ++i)
	{
		ValueRef source(&(*varStack)[stackBase + fun._captures[i].second], fun._slotTypes[fun._captures[i].second]);
		// 반환값으로 아직 사용될 슬롯만 복사로 남긴다. 나머지 캡처 슬롯은 호출
		// 프레임이 끝난 뒤 쓰이지 않으므로 보관함에 소유권을 넘긴다.
		if (source.IsContainerType())
			mayContainContainerChild = true;

		if (source == keepOnStack)
			Move(closure->Capture(i), source);
		else
			MoveTake(closure->Capture(i), source);
	}
	if (mayContainContainerChild)
		closure->_cycleState._mayContainContainerChild = true;
}

bool CNeoVMWorker::CallNative(FunctionPtrNative functionPtrNative, void* pUserData, StringInfo* pStr, int n3, VarInfo* pRet, bool keepResult)
{
	Neo_NativeFunction func = functionPtrNative._func;
	if (func == nullptr)
	{
		SetErrorFormat(RTE_CALL_NAMED, pStr ? pStr->_str.c_str() : "<unknown>");
		return false;
	}
	if (!EnsureStackRange(_iSP_VarsMax, n3))
		return false;
	NativeFrameScope frame(*this);
	const CallStoragePlan* plan = _activePlan;
	CoroutineInfo* context = m_pCur;
	_iSP_Vars = _iSP_VarsMax;
	SetStackPointer(_iSP_Vars);
	if (_iSP_Vars_Max2 < _iSP_VarsMax + 1 + n3)
		_iSP_Vars_Max2 = _iSP_VarsMax + 1 + n3;

	bool nativeResult = (func)(this, pUserData, pStr, n3);
	if (!frame.IsCurrent())
		return false;
	if (nativeResult == false)
	{
		frame.Restore();
		if (GetVM()->IsLastErrorMsg())
			SetCodePtr(0);
		else
			SetErrorFormat(RTE_CALL_NAMED, pStr ? pStr->_str.c_str() : "<unknown>");
		return false;
	}
	// 결과는 소유권째 목적지로 옮긴다. 결과를 쓰지 않는 호출만 창 0번도 함께 놓는다.
	VarInfo* window = m_pVarStack_Pointer;
	const int argSP_Vars = _iSP_Vars;
	const bool includeResult = pRet == nullptr && !keepResult;
	if (pRet != nullptr)
		TakeByPolicy(pRet, window, plan != nullptr ? static_cast<StoragePolicy>(plan->policies[0]) : StoragePolicy::Boxed);
	frame.Restore();
	if (m_pRegisterActive != nullptr)
	{
		switch(m_pRegisterActive->_sub_state)
		{
			case COROUTINE_SUB_START:
				// 부모 창은 부모가 다시 실행될 때 놓는다(StopCoroutine). 창이 잡은 코루틴 참조가
				// 전환 동안 살아 있어야 임시 코루틴도 안전하다.
				return StartCoroutione(argSP_Vars, n3);
			case COROUTINE_SUB_CLOSE:
				switch (m_pRegisterActive->_state)
				{
					case COROUTINE_STATE_SUSPENDED:
						DeadCoroutine(m_pRegisterActive);
						break;
					case COROUTINE_STATE_RUNNING:
						if (m_pCur != m_pRegisterActive)SetError(RTE_COROUTINE_INVALID);
						else							StopCoroutine(true);
						break;
					case COROUTINE_STATE_DEAD:
						break;
					case COROUTINE_STATE_NORMAL:
						DeadCoroutine(m_pRegisterActive);
						for(auto it = m_sCoroutines.begin(); it != m_sCoroutines.end(); it++)
						{
							if((*it) == m_pRegisterActive)
							{
								m_sCoroutines.erase(it);
								break;
							}
						}
						break;
				}
				m_pRegisterActive = nullptr;
				break;
			default:
				SetError(RTE_COROUTINE_INVALID);
				break;
		}
	}
	// 컨텍스트가 바뀌었으면 창은 저장된 계획으로 그 컨텍스트가 놓는다.
	if (m_pCur == context && plan != nullptr)
	{
		ReleaseCallWindow(*plan, window, includeResult);
		_activePlan = nullptr;
	}
	return true;
}
bool CNeoVMWorker::CallDefaultNativeByIndex(int nativeIndex, int n3, VarInfo* pRet)
{
	if (!EnsureStackRange(_iSP_VarsMax, n3))
		return false;
    // 스택 범위 검사는 유지한다. 등록된 숫자 함수의 인자 계약은 컴파일러가 보증한다.
    // 정수와 실수 모두 허용하는 내부 명령이므로 두 숫자 표현의 선택은 호출 계획이 준다.
    if (n3 == 1)
    {
        CNeoVM::ScalarNativeFunction scalar = CNeoVM::GetDefaultNativeScalar(nativeIndex);
        if (scalar)
        {
            const ValueRef argument(m_pVarStack_Base->data() + _iSP_VarsMax + 1, PlanType(1));
            assert(argument.IsNumber());
            const NS_FLOAT value = scalar(argument.GetFloatNumber());
            if (pRet) StoreNumberFloat(pRet, value);
            _activePlan = nullptr;
            return true;
        }
    }
	NativeFrameScope frame(*this);
	const CallStoragePlan* plan = _activePlan;
	CoroutineInfo* context = m_pCur;
	_iSP_Vars = _iSP_VarsMax;
	SetStackPointer(_iSP_Vars);
	if (_iSP_Vars_Max2 < _iSP_VarsMax + 1 + n3)
		_iSP_Vars_Max2 = _iSP_VarsMax + 1 + n3;

	bool nativeResult = CNeoVM::CallDefaultNativeByIndex(nativeIndex, this, (short)n3);
	if (!frame.IsCurrent())
		return false;
	if (nativeResult == false)
	{
		frame.Restore();
		if (GetVM()->IsLastErrorMsg())
			SetCodePtr(0);
		else
			SetErrorFormat(RTE_CALL_NAMED, CNeoVM::GetDefaultNativeName(nativeIndex));
		return false;
	}
	// 결과는 소유권째 목적지로 옮긴다. 결과를 쓰지 않는 호출은 창 0번도 함께 놓는다.
	VarInfo* window = m_pVarStack_Pointer;
	const int argSP_Vars = _iSP_Vars;
	if (pRet != nullptr)
		TakeByPolicy(pRet, window, plan != nullptr ? static_cast<StoragePolicy>(plan->policies[0]) : StoragePolicy::Boxed);
	frame.Restore();
	if (m_pRegisterActive != nullptr)
	{
		switch(m_pRegisterActive->_sub_state)
		{
			case COROUTINE_SUB_START:
				// 부모 창은 부모가 다시 실행될 때 놓는다(StopCoroutine).
				return StartCoroutione(argSP_Vars, n3);
			case COROUTINE_SUB_CLOSE:
				switch (m_pRegisterActive->_state)
				{
					case COROUTINE_STATE_SUSPENDED:
						DeadCoroutine(m_pRegisterActive);
						break;
					case COROUTINE_STATE_RUNNING:
						if (m_pCur != m_pRegisterActive)SetError(RTE_COROUTINE_INVALID);
						else							StopCoroutine(true);
						break;
					case COROUTINE_STATE_DEAD:
						break;
					case COROUTINE_STATE_NORMAL:
						DeadCoroutine(m_pRegisterActive);
						for(auto it = m_sCoroutines.begin(); it != m_sCoroutines.end(); it++)
						{
							if((*it) == m_pRegisterActive)
							{
								m_sCoroutines.erase(it);
								break;
							}
						}
						break;
				}
				m_pRegisterActive = nullptr;
				break;
			default:
				SetError(RTE_COROUTINE_INVALID);
				break;
		}
	}
	// 컨텍스트가 바뀌었으면 창은 저장된 계획으로 그 컨텍스트가 놓는다.
	if (m_pCur == context && plan != nullptr)
	{
		ReleaseCallWindow(*plan, window, pRet == nullptr);
		_activePlan = nullptr;
	}
	return true;
}
bool CNeoVMWorker::PropertyNative(FunctionPtrNative functionPtrNative, void* pUserData, StringInfo* pStr, ValueRef pRet, bool get, int* deferredTransferFrame)
{
	Neo_NativeProperty func = functionPtrNative._property;
	if (func == nullptr)
	{
		SetErrorFormat(RTE_PROPERTY_NAMED, get ? "read" : "write", pStr ? pStr->_str.c_str() : "<unknown>");
		return false;
	}
	// 호스트 getter/setter 의 프레임을 현재 프레임이 쓰는 모든 슬롯 위(_iSP_Vars_Max2)에 둔다.
	// 함수 진입 시 _iSP_Vars_Max2 는 _stackSlotCount(대기 중인 호출 인자 창 포함)까지 올라가 있으므로,
	// 여기서 시작하면 평가 중인 인자가 덮이지 않는다 → 컴파일러가 선택자 읽기를 인자 창 충돌로 보지 않아도 된다.
	const int hostBase = _iSP_Vars_Max2;
	if (!EnsureStackRange(hostBase, 0))
		return false;
	NativeFrameScope frame(*this);
	_iSP_Vars = hostBase;
	_iSP_VarsMax = hostBase;
	SetStackPointer(_iSP_Vars);
	if (_iSP_Vars_Max2 < hostBase + 1)
		_iSP_Vars_Max2 = hostBase + 1;

	bool nativeResult = (func)(this, pUserData, pStr, pRet, get);
	if (!frame.IsCurrent())
		return false;
	int argSP_Vars = _iSP_Vars;
	// 반환값은 호출자의 pRet에 있다. 호스트 프레임은 소유 슬롯을 남기지 않으므로 상한만 되돌린다.
	_iSP_Vars_Max2 = hostBase;
	frame.Restore();
	if (nativeResult == false)
	{
		if (GetVM()->IsLastErrorMsg())
			SetCodePtr(0);
		else
			SetErrorFormat(RTE_PROPERTY_NAMED, get ? "read" : "write", pStr ? pStr->_str.c_str() : "<unknown>");
		return false;
	}

    if (deferredTransferFrame != nullptr)
    {
        // READ_* 융합 명령은 산술·저장까지 끝낸 뒤에 getter 가 요청한 코루틴 전환을 처리한다.
        *deferredTransferFrame = argSP_Vars;
        return true;
    }
    return FinishPropertyTransfer(argSP_Vars);
}

bool CNeoVMWorker::FinishPropertyTransfer(int argSP_Vars)
{
	if (m_pRegisterActive != nullptr)
	{
		switch (m_pRegisterActive->_sub_state)
		{
		case COROUTINE_SUB_START:
			if (!StartCoroutione(argSP_Vars, 0))
				return false;
			break;
		case COROUTINE_SUB_CLOSE:
			switch (m_pRegisterActive->_state)
			{
			case COROUTINE_STATE_SUSPENDED:
				DeadCoroutine(m_pRegisterActive);
				break;
			case COROUTINE_STATE_RUNNING:
				if (m_pCur != m_pRegisterActive)SetError(RTE_COROUTINE_INVALID);
				else							StopCoroutine(true);
				break;
			case COROUTINE_STATE_DEAD:
				break;
			case COROUTINE_STATE_NORMAL:
				DeadCoroutine(m_pRegisterActive);
				for (auto it = m_sCoroutines.begin(); it != m_sCoroutines.end(); it++)
				{
					if ((*it) == m_pRegisterActive)
					{
						m_sCoroutines.erase(it);
						break;
					}
				}
				break;
			}
			m_pRegisterActive = nullptr;
			break;
		default:
			SetError(RTE_COROUTINE_INVALID);
			break;
		}
	}

	return true;
}

bool CNeoVMWorker::ChangeNumber(ValueRef p)
{
	switch(p.GetType())
	{
		case VAR_INT:
			return true;
		case VAR_FLOAT:
			p.SetType(VAR_INT);
			p->_int = NeoFloatToInt(p->_float);
			return true;
		default:
			break;
	}
	char ch[256];
#ifdef _WIN32
	snprintf(ch, _countof(ch), "ChangeNumber (%s != number)", GetDataType(p.GetType()).c_str());
#else
	sprintf(ch, "ChangeNumber (%s != number)", GetDataType(p.GetType()).c_str());
#endif

	SetError(ch);
	return false;
}


void CNeoVMWorker::SetArrayAssignError(ArrayInfo* array, ValueRef value)
{
	if (array->Int32OutOfRange(value)) SetError("int32 array value out of range");
	else SetErrorFormat(RTE_ARRAY_VALUE_TYPE, GetDataType(value.GetType()).c_str(), GetArrayElementTypeName(array->_elementType));
}

std::string GetDataType(VAR_TYPE t)
{
	switch (t)
	{
	case VAR_INT:
		return "int";
	case VAR_FLOAT:
		return "float";
	case VAR_BOOL:
		return "bool";
	case VAR_NONE:
		return "null";
	case VAR_FUN:
		return "fun";
	case VAR_ITERATOR:
		return "iterator";
	case VAR_FUN_NATIVE:
		return "fun";
	case VAR_VEC:
		// 진단에서는 저장 방식과 성분 수에 관계없이 벡터의 공통 이름을 쓴다.
		return "vector";
	case VAR_STRING:
		return "string";
	case VAR_VEC3:
	case VAR_VEC4:
		// 진단에서는 저장 방식과 성분 수에 관계없이 벡터의 공통 이름을 쓴다.
		return "vector";
	case VAR_FP_NATIVE:
		return "native_object";
	case VAR_ARRAY:
		return "array";
	case VAR_MAP:
		return "map";
	case VAR_LIST:
		return "list";
	case VAR_SET:
		return "set";
	case VAR_COROUTINE:
		return "coroutine";
	case VAR_MODULE:
		return "module";
	case VAR_ASYNC:
		return "async";
	case VAR_CLOSURE:
		return "fun";
	default:
		break;
	}
	return "unknown";
}

};
