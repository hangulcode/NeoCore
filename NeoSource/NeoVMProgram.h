#pragma once

#include <functional>
#include "NeoVMInternal.h"

namespace NeoScript
{

class CNArchive;

struct SNeoVMHeader
{
	u32		_dwFileType;
	u32		_dwNeoVersion;

	int		_iFunctionCount;
	int		_iStaticVarCount;
	int		_iGlobalVarCount;
	int		_iExportVarCount;

	int		_iMainFunctionOffset;
	int		_iCodeSize;
	int		m_iDebugCount;
	int		m_iDebugOffset;

	u32		_dwFlag;
};

#define NEO_HEADER_FLAG_DEBUG				0x00000001
#define NEO_HEADER_FLAG_SINGLE_PRECISION	0x00000002


enum FUNCTION_TYPE : u8
{
	FUNT_NORMAL = 0,
	FUNT_EXPORT,
	FUNT_ANONYMOUS,
	FUNT_BUILT_IN,
};

// 컴파일러가 정한 저장 계약. typeId는 정확한 언어 타입, kind는 실행 시 저장·정리 방식이다.
enum class SlotKind : u8
{
    Boxed, Int, Float, Bool, String, Vector2, Vector3, Vector4,
    Native, Array, Map, List, Set, Coroutine, Async, ObjectId, Function, Iterator
};
static_assert(static_cast<unsigned>(SlotKind::ObjectId) <= NEOS_OP_SUB_MASK, "Fixed copies must fit the sub-operation");
struct SlotType
{
    u32 typeId = 0;
    SlotKind kind = SlotKind::Boxed;
    bool IsNumber() const { return kind >= SlotKind::Int && kind <= SlotKind::Bool; }
};
struct SlotLayout
{
    u32 typeId = 0;
    short slot = 0;
    SlotKind kind = SlotKind::Boxed;
    bool IsNumber() const { return kind >= SlotKind::Int && kind <= SlotKind::Bool; }
};
static_assert(sizeof(SlotLayout) == 8, "Slot layouts must remain compact");
inline SlotKind FindSlotKind(const std::vector<SlotLayout>& layout, short slot)
{
    std::size_t first = 0, last = layout.size();
    while (first < last)
    {
        const std::size_t middle = first + (last - first) / 2;
        if (layout[middle].slot < slot) first = middle + 1;
        else last = middle;
    }
    return first < layout.size() && layout[first].slot == slot ? layout[first].kind : SlotKind::Boxed;
}
inline VAR_TYPE StorageValueType(SlotKind kind)
{
    static const VAR_TYPE types[] = { VAR_NONE, VAR_INT, VAR_FLOAT, VAR_BOOL, VAR_STRING,
        VAR_VEC, VAR_VEC3, VAR_VEC4, VAR_FP_NATIVE, VAR_ARRAY, VAR_MAP, VAR_LIST,
        VAR_SET, VAR_COROUTINE, VAR_ASYNC };
    return kind == SlotKind::ObjectId ? ValueRef::ObjectIdStorageType() :
        kind == SlotKind::Function ? ValueRef::CallableStorageType() :
        kind == SlotKind::Iterator ? VAR_ITERATOR : types[static_cast<unsigned>(kind)];
}

// 복사·해제 정책. 언어 타입이 아니라 VM이 슬롯 payload를 다루는 방식이다.
// argFlag 하위 4비트(NEOS_OP_SUB_MASK)에 실리므로 16개 안에 들어야 한다.
// Payload는 참조 카운트가 없는 8바이트 값, 나머지는 해제 경로가 다른 소유 참조다.
enum class StoragePolicy : u8
{
    Payload = 0, String, Vec, Native, Array, Map, List, Set, Coroutine, Async, Module, Callable,
    Boxed = 15 // 컴파일러가 종류를 정하지 못한 슬롯. 해제할 수 없으므로 디버그 빌드에서 검출한다.
};
static_assert(static_cast<unsigned>(StoragePolicy::Boxed) <= NEOS_OP_SUB_MASK, "Storage policies must fit the sub-operation");
inline StoragePolicy PolicyOf(SlotKind kind)
{
    switch (kind)
    {
    case SlotKind::Boxed: return StoragePolicy::Boxed;
    case SlotKind::Int: case SlotKind::Float: case SlotKind::Bool: case SlotKind::Vector2:
    case SlotKind::ObjectId: case SlotKind::Iterator: return StoragePolicy::Payload;
    case SlotKind::String: return StoragePolicy::String;
    case SlotKind::Vector3: case SlotKind::Vector4: return StoragePolicy::Vec;
    case SlotKind::Native: return StoragePolicy::Native;
    case SlotKind::Array: return StoragePolicy::Array;
    case SlotKind::Map: return StoragePolicy::Map;
    case SlotKind::List: return StoragePolicy::List;
    case SlotKind::Set: return StoragePolicy::Set;
    case SlotKind::Coroutine: return StoragePolicy::Coroutine;
    case SlotKind::Async: return StoragePolicy::Async;
    case SlotKind::Function: return StoragePolicy::Callable;
    }
    return StoragePolicy::Boxed;
}
inline StoragePolicy PolicyOfType(VAR_TYPE type)
{
    switch (type)
    {
    case VAR_STRING: return StoragePolicy::String;
    case VAR_VEC3: case VAR_VEC4: return StoragePolicy::Vec;
    case VAR_FP_NATIVE: return StoragePolicy::Native;
    case VAR_ARRAY: return StoragePolicy::Array;
    case VAR_MAP: return StoragePolicy::Map;
    case VAR_LIST: return StoragePolicy::List;
    case VAR_SET: return StoragePolicy::Set;
    case VAR_COROUTINE: return StoragePolicy::Coroutine;
    case VAR_ASYNC: return StoragePolicy::Async;
    case VAR_MODULE: return StoragePolicy::Module;
    case VAR_CLOSURE: case VAR_FUN: return StoragePolicy::Callable;
    default: break;
    }
    if (type == ValueRef::CallableStorageType()) return StoragePolicy::Callable;
    return StoragePolicy::Payload;
}
// 정책이 참조 카운트를 소유하는가. Payload와 Boxed는 해제 대상이 아니다.
inline bool PolicyOwns(StoragePolicy policy)
{ return policy != StoragePolicy::Payload && policy != StoragePolicy::Boxed; }
inline const char* StoragePolicyName(StoragePolicy policy)
{
    switch (policy)
    {
    case StoragePolicy::Payload: return "payload";
    case StoragePolicy::String: return "string";
    case StoragePolicy::Vec: return "vec";
    case StoragePolicy::Native: return "native";
    case StoragePolicy::Array: return "array";
    case StoragePolicy::Map: return "map";
    case StoragePolicy::List: return "list";
    case StoragePolicy::Set: return "set";
    case StoragePolicy::Coroutine: return "coroutine";
    case StoragePolicy::Async: return "async";
    case StoragePolicy::Module: return "module";
    case StoragePolicy::Callable: return "callable";
    case StoragePolicy::Boxed: return "boxed";
    }
    return "?";
}
// 함수 슬롯 정보 바이트: 하위 4비트 정책, 비트 4 임시 슬롯.
static const u8 SLOT_INFO_POLICY_MASK = 0x0f;
static const u8 SLOT_INFO_TEMPORARY = 0x10;
inline StoragePolicy SlotInfoPolicy(u8 info) { return static_cast<StoragePolicy>(info & SLOT_INFO_POLICY_MASK); }

struct CallStoragePlan
{
    std::vector<VAR_TYPE> types; // [0] 반환값, [1..] 순서가 확정된 인자
    std::vector<u8> policies;    // types와 같은 순서의 복사·해제 정책
};

struct SFunctionTable
{
	int					_codePtr;
	short				_argsCount;
	short				_localTempMax;
	int					_localVarCount;
	int					_localAddCount; // No Save (로드 시 계산)
	int					_stackSlotCount; // 로드 시 계산한 쓰기 범위. 다음 호출의 인자 슬롯까지 포함한다.
	FUNCTION_TYPE		_funType;
	// 익명 함수 생성 시 부모 프레임에서 복사할 슬롯과, 호출 프레임에서
	// 값을 받을 숨은 지역 슬롯. 캡처가 없으면 빈 벡터라 기존 함수 호출은
	// 추가 비용이 없다.
	std::vector<std::pair<short, short>> _captures;
    std::shared_ptr<const std::vector<VAR_TYPE>> _captureTypes;
    std::vector<SlotLayout> _storage;
    std::vector<CallStoragePlan> _calls;
    // 프레임 슬롯 [0, _stackSlotCount)의 선언 타입. 창 슬롯은 VAR_NONE이다.
    std::vector<VAR_TYPE> _slotTypes;
    // 프레임 슬롯 [0, _localAddCount)의 정책·임시 표시. 0번은 반환값 정책이다.
    std::vector<u8> _slotInfo;
    // 소유 슬롯은 인자 뒤에 연속으로 놓인다. 반환 정리는 [1, _releaseCount)만 돈다.
    int _ownedCount = 0;
    int _releaseCount = 1;
    VAR_TYPE _resultType = VAR_NONE;
    NEOS_FORCEINLINE StoragePolicy SlotPolicy(int slot) const { return SlotInfoPolicy(_slotInfo[slot]); }
    NEOS_FORCEINLINE bool IsTemporarySlot(int slot) const
    { return slot >= 0 && slot < static_cast<int>(_slotInfo.size()) && (_slotInfo[slot] & SLOT_INFO_TEMPORARY) != 0; }
};

// static(상수) 슬롯 1개. BoundaryValue 가 아니라 원시 값으로 들고 있는다.
// VAR_STRING 의 StringInfo 는 CNeoVM 별 풀에서 나오고 같은 풀로 반납되어야 하므로,
// VM 경계를 넘어 공유되는 Program 이 소유하면 안 된다. 워커가 자기 VM 할당자로 실체화한다.
struct SStaticConst
{
	VAR_TYPE	_type = VAR_NONE;
	NS_INT			_int = 0;
	NS_FLOAT	_float = 0;
	bool		_bl = false;
	std::string	_str;   // VAR_STRING 전용
};

// switch/case 테이블. SStaticConst 와 같은 이유로 Program 전용 원시 표현을 쓴다.
// (VM pool / StringInfo / Var_Release / 스크립트 Map API 에 의존하지 않음)
// key 는 컴파일 타임 상수이고 strict type 비교한다.
// float 는 정확 비교가 불안정해서 case key 로 허용하지 않는다(컴파일 에러).
struct ProgramSwitchKey
{
	VAR_TYPE	_type = VAR_NONE;   // VAR_INT / VAR_STRING
	NS_INT			_int = 0;
	bool		_bl = false;
	std::string	_str;               // VAR_STRING 전용 (UTF-8 바이트 그대로)
};

struct ProgramSwitchEntry
{
	ProgramSwitchKey	key;
	int					jumpOffset = 0;  // op 단위 relative (NOP_SWITCH 다음 op 기준)
	int					next = -1;       // 같은 버킷의 다음 엔트리. -1 = 끝
};

// 읽기 전용 해시 테이블. Program 과 수명이 같고 여러 VM/워커가 공유한다.
struct ProgramSwitchTable
{
	std::vector<int>				buckets;   // hash & mask → entry index (-1 = 빈 버킷)
	std::vector<ProgramSwitchEntry>	entries;
	int								defaultOffset = 0;

	void Build();                             // entries 로부터 buckets 구성
	int  Find(ValueRef pKey) const;           // 매칭 실패/지원 외 타입이면 defaultOffset
};

// NOP_JMP_RANGE_*의 lower/upper operand. Program은 여러 VM이 공유되므로
// BoundaryValue/포인터가 아니라 슬롯 번호와 descriptor flag만 보관한다.
struct ProgramRangeJump
{
	short	lower = 0;
	short	upper = 0;
	u8		flags = 0;
};

// 융합 계획은 명령·슬롯 번호만 보관한다. 워커의 값이나 포인터는 공유하지 않는다.
// 컴파일된 스크립트 이미지.
// Create() 에서 1회 파싱 + 1회 코드 패치되고 이후 완전 불변이다.
// 여러 CNeoVMWorker / 여러 CNeoVM / 여러 스레드가 refcount 로 공유한다.
class CNeoVMProgram
{
	std::atomic<int> _refCount{ 1 };

	CNeoVMProgram() {}
	~CNeoVMProgram() {}
	CNeoVMProgram(const CNeoVMProgram&) = delete;
	CNeoVMProgram& operator=(const CNeoVMProgram&) = delete;

	bool Load(CNArchive& ar, std::string* err);
	bool VerifyCode(const std::vector<int>& codeOwners, std::string* err);
	// PTRCALL2(native 이름) → NATIVECALL(native index) / intrinsic opcode 치환.
	// Create() 안에서 딱 한 번만 실행된다.
	void PatchNativeCalls();
	// n2가 static 문자열인 READ를 문자열 키 전용 opcode로 치환한다.
	// Program은 여러 VM이 공유하므로 StringInfo/MapInfo 포인터는 코드에 저장하지 않는다.
	void PatchStaticStringReads();
	// 오퍼랜드가 전부 로컬인 op 를 _L 변형으로 치환한다(런타임 argFlag 테스트 제거).
	// 컴파일러/이미지는 건드리지 않고 로드 후 메모리 코드만 바꾸므로 저장 바이트코드는 그대로다.
	// 이 순회에서 구성 의존 opcode도 검증한다. false면 Program 생성 실패다.
	bool PatchLocalOps(std::string* err);

public:
	SNeoVMHeader				header;
	std::vector<SVMOperation>	code;              // 패치 완료 상태
	int							localOpCount = 0;  // PatchLocalOps 가 _L 로 바꾼 op 수(진단용)
	std::vector<SFunctionTable>	functions;
    std::vector<SlotLayout> globalStorage;
    std::vector<VAR_TYPE> globalTypes; // 전체 인스턴스가 공유하는 고정 전역·상수 배치
	std::map<std::string, int>	exportFunctions;   // 함수명 → 함수 ID
	std::map<std::string, int>	exportVariables;   // 전역 변수명 → 전역 슬롯 인덱스
	std::vector<SStaticConst>	staticValues;
	std::vector<ProgramSwitchTable>	switchTables;      // NOP_SWITCH.n1 이 이 배열의 인덱스
	std::vector<ProgramRangeJump>	rangeJumps;          // NOP_JMP_RANGE_*.n3 이 이 배열의 인덱스

	// 디버그 정보 (IsDebugInfo() 일 때만 채워진다)
	std::vector<debug_info>		debugData;
	std::map<int, std::map<int, std::string>>	debugVarNames;   // 함수 ID → (스택 슬롯 → 이름)
	std::map<int, std::string>	debugGlobalNames;                // 전역 슬롯 → 이름
	std::map<int, std::string>	debugFunctionNames;              // 함수 ID → 이름

	int							byteSize = 0;      // 원본 이미지 바이트 수

	// 실패 시 nullptr. err 이 있으면 사유를 담는다. 반환된 프로그램의 refCount 는 1.
	static CNeoVMProgram* Create(const void* pBuffer, int iSize, std::string* err = nullptr);

	void AddRef()
	{
		_refCount.fetch_add(1, std::memory_order_relaxed);
	}
	void Release()
	{
		if (_refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
			delete this;
	}

	NEOS_FORCEINLINE bool IsDebugInfo() const { return (header._dwFlag & NEO_HEADER_FLAG_DEBUG) != 0; }
	NEOS_FORCEINLINE const u8* GetCodeBegin() const { return (const u8*)code.data(); }
	NEOS_FORCEINLINE int GetCodeSize() const { return (int)(code.size() * sizeof(SVMOperation)); }
	// The pointer between globals and constants is the origin for signed slots.
	NEOS_FORCEINLINE int ConstantBase() const { return header._iGlobalVarCount + 1; }
	NEOS_FORCEINLINE bool IsGlobalVariable(int slot) const
	{ return slot <= -2 && slot >= -header._iGlobalVarCount - 1; }
	NEOS_FORCEINLINE bool IsGlobalOperand(int slot) const
	{ return IsGlobalVariable(slot) || (slot >= 0 && slot < header._iStaticVarCount); }
	NEOS_FORCEINLINE int GetGlobalSlotCount() const { return header._iStaticVarCount + header._iGlobalVarCount + 1; }

	int FindFunction(const std::string& name) const
	{
		auto it = exportFunctions.find(name);
		if (it == exportFunctions.end())
			return -1;
		return (*it).second;
	}
	int FindGlobalVar(const std::string& name) const
	{
		auto it = exportVariables.find(name);
		if (it == exportVariables.end())
			return -1;
		return (*it).second;
	}
};

// 컨텍스트가 소유할 수 있는 슬롯의 열거. 호출자가 저장한 프레임(콜스택), 실행 중인 네이티브가
// 기록한 프레임과 창, 저장된 현재 프레임(_info), 대기 중인 호출 창 순서로 (슬롯 포인터, 선언 타입)을
// 넘긴다. 태그 없는 스택에서 소유 참조를 찾는 유일한 방법이며, 실행 중인 컨텍스트는 먼저 _info를
// 저장해야 한다. 프레임은 [base, base + _releaseCount)만 본다. 숫자 슬롯은 소유하지 않는다.
template<typename Visitor>
inline void ForEachContextFrameSlot(CoroutineInfo* ctx, Visitor visitor)
{
    const CNeoVMProgram* program = ctx->_program;
    if (program == nullptr) return;
    VarInfo* stack = ctx->m_sVarStack.data();
    const int stackSize = static_cast<int>(ctx->m_sVarStack.size());
    const std::vector<SFunctionTable>& functions = program->functions;
    auto frame = [&](int base, int functionIndex)
    {
        if (functionIndex < 0 || functionIndex >= static_cast<int>(functions.size()) || base < 0) return;
        const SFunctionTable& fun = functions[functionIndex];
        for (int i = 0; i < fun._releaseCount && base + i < stackSize; ++i)
        {
            const VAR_TYPE type = fun._slotTypes[i];
            if (type == VAR_NONE) continue;
            visitor(stack + base + i, type);
        }
    };
    auto window = [&](int base, const CallStoragePlan* plan)
    {
        if (plan == nullptr || base < 0) return;
        for (std::size_t i = 0; i < plan->types.size() && base + static_cast<int>(i) < stackSize; ++i)
            if (plan->types[i] != VAR_NONE) visitor(stack + base + i, plan->types[i]);
    };
    for (int i = 0; i < ctx->m_sCallStack.size(); ++i)
        frame(ctx->m_sCallStack[i]._iSP_Vars, ctx->m_sCallStack[i]._functionIndex);
    for (int i = 0; i < ctx->m_sNativeFrames.size(); ++i)
    {
        const SNativeFrame& native = ctx->m_sNativeFrames[i];
        frame(native._iSP_Vars, native._functionIndex);
        window(native._window, native._plan);
    }
    frame(ctx->_info._iSP_Vars, ctx->_info._functionIndex);
    window(ctx->_info._iSP_VarsMax, ctx->_info._activePlan);
}

// opcode별 어셈블리 표기는 이 공통 포맷터만 사용한다. 컴파일러와 런타임은
// 각각 자기 심볼 표를 operand/function resolver로 공급한다.
struct DebugInstructionFormatContext
{
	const SVMOperation* operation = nullptr;
	// 점프 대상 계산의 기준점. 컴파일 타임은 함수 블록 기준 상대 인덱스, 런타임은 프로그램
	// 절대 인덱스다. 포맷터는 opIndex + 1 + n1 만 계산하고 표기는 jumpLabel 에 맡기므로
	// 어느 쪽이든 그대로 동작한다.
	int opIndex = 0;
	// argIndex(1,2,3) -> "[S.3]" / "[G.7 'text']" 같은 오퍼랜드 표기.
	std::function<std::string(int)> operand;
	// 함수 ID -> 이름. 컴파일 타임이 두 가지 표기를 쓰고 있어(전체이름 vs 짧은 이름)
	// 출력을 그대로 보존하려고 둘로 나눠 둔다. 런타임은 같은 것을 넣어도 된다.
	std::function<std::string(int)> functionFull;    // NOP_CALL 용
	std::function<std::string(int)> functionShort;   // NOP_FMOV1/FMOV2 용
	// 점프 대상 op 인덱스 -> " 'go_3'" 같은 라벨. 라벨이 없으면 빈 문자열.
	std::function<std::string(int)> jumpLabel;
};

// 어셈블리 한 줄(개행 없음)을 만든다. outByteCount 는 바이트 덤프에 표시할
// opcode + arg flag + 유효 operand 바이트 수다.
// 반환값 false = 이 포맷터가 모르는 opcode(문자열에는 "?? op(N)" 이 들어간다).
// 에러로 볼지 말지는 호출자가 정한다 — 컴파일 타임은 컴파일 에러, 런타임은 표시만.
bool FormatDebugOperation(const DebugInstructionFormatContext& context, std::string& outAssembly,
	int* outByteCount = nullptr);

};
