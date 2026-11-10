#pragma once

#include "NeoArchive.h"
#include "NeoVMWorker.h"
#include "NeoTextLoader.h"
#include <cmath>
#include "NeoCoreIR.h"

namespace NeoScript
{

#define INVALID_ERROR_PARSEJOB			(-1)

#define COMPILE_LOCALTMP_VAR_BEGIN		(10000)
#define COMPILE_STATIC_VAR_BEGIN		(15000)
#define COMPILE_CALLARG_VAR_BEGIN		(30000) // Call argument window ends before STACK_POS_RETURN.
#define COMPILE_STATIC_VAR_CAPACITY (COMPILE_CALLARG_VAR_BEGIN - COMPILE_STATIC_VAR_BEGIN)
#define COMPILE_EXTENDED_STATIC_BEGIN (65536)
#define COMPILE_MAX_ARGUMENTS (STACK_POS_RETURN - COMPILE_CALLARG_VAR_BEGIN - 1)

inline int StaticCompileIndex(int index)
{
    CheckCompileRange("constant index", index, 0, INT_MAX - COMPILE_EXTENDED_STATIC_BEGIN);
    return index + (index < COMPILE_STATIC_VAR_CAPACITY ? COMPILE_STATIC_VAR_BEGIN : COMPILE_EXTENDED_STATIC_BEGIN);
}
inline int StaticPoolIndex(int index)
{
    return index - (index >= COMPILE_EXTENDED_STATIC_BEGIN ? COMPILE_EXTENDED_STATIC_BEGIN : COMPILE_STATIC_VAR_BEGIN);
}
inline bool IsCompileConstant(int index)
{
    return (index >= COMPILE_STATIC_VAR_BEGIN && index < COMPILE_CALLARG_VAR_BEGIN)
        || index >= COMPILE_EXTENDED_STATIC_BEGIN;
}
//#define COMPILE_VAR_NULL				(32766)
#define STACK_POS_RETURN				(32767)
#define COMPILE_VAR_MAX					(32768)

#define COMPILE_GLOBAL_VAR_BEGIN		(-2)	// -2 ~ -32768 (32767 globals)

#ifdef _WIN32
#else
#define _countof(a) (sizeof(a)/sizeof(*(a)))
#endif

bool IsTempVar(int iVar);
// 테스트·실험용 스위치. false 면 READ_* 융합 명령을 내지 않고 분리된 READ + 산술을 낸다.


struct SJumpValue
{
	int	_iCodePosOffset;
	int _iBaseJmpOffset;

	SJumpValue()
	{
		_iCodePosOffset = _iBaseJmpOffset = 0;
	}
	SJumpValue(int iCodePosOffset, int iBaseJmpOffset)
	{
		_iCodePosOffset = iCodePosOffset;
		_iBaseJmpOffset = iBaseJmpOffset;
	}
	void Set(int iCodePosOffset, int iBaseJmpOffset)
	{
		_iCodePosOffset = iCodePosOffset;
		_iBaseJmpOffset = iBaseJmpOffset;
	}
};


struct SLocalVar
{
	std::map<std::string, int> _localVars;

	int FindVar(const std::string& name)
	{
		auto it = _localVars.find(name);
		if (it == _localVars.end())
			return -1;
		return (*it).second;
	}
};

inline SlotType CoreStorageType(const NeoCore::TypeRegistry& types, NeoCore::TypeId id)
{
    using NeoCore::TypeKind;
    SlotType storage;
    storage.typeId = id;
    const NeoCore::TypeDesc& type = types.Get(id);
    switch (type.kind)
    {
    case TypeKind::Int: storage.kind = SlotKind::Int; break;
    case TypeKind::Float: storage.kind = SlotKind::Float; break;
    case TypeKind::Bool: storage.kind = SlotKind::Bool; break;
    case TypeKind::String: storage.kind = SlotKind::String; break;
    case TypeKind::Vector2: storage.kind = SlotKind::Vector2; break;
    case TypeKind::Vector3: storage.kind = SlotKind::Vector3; break;
    case TypeKind::Vector4: case TypeKind::Quaternion: storage.kind = SlotKind::Vector4; break;
    case TypeKind::Array: case TypeKind::Array32: storage.kind = SlotKind::Array; break;
    case TypeKind::List: case TypeKind::Struct: storage.kind = SlotKind::List; break;
    case TypeKind::Map: storage.kind = SlotKind::Map; break;
    case TypeKind::Set: storage.kind = SlotKind::Set; break;
    case TypeKind::Function: storage.kind = SlotKind::Function; break;
    case TypeKind::Native: storage.kind = SlotKind::ObjectId; break;
    case TypeKind::BuiltinObject: storage.kind = type.name == "AsyncRequest" ? SlotKind::Async : SlotKind::Native; break;
    case TypeKind::Coroutine: storage.kind = SlotKind::Coroutine; break;
    default: break;
    }
    return storage;
}

struct SFunctionInfo;

struct SLayerVar
{
	// 이 lexical scope를 소유한 함수. 다단계 캡처에서 각 중간 익명 함수에
	// 부모 프레임 기준의 숨은 슬롯을 만들 때 사용한다.
	SFunctionInfo*	_function = nullptr;
	std::vector<SLocalVar*>	_varsLayer;
	// 상위 함수의 지역 변수를 읽는 익명 함수는 이름을 자기 숨은 로컬 슬롯으로
	// 해석한다. 선언 로컬과 달리 이 맵은 현재 블록의 중복 선언 검사에는 넣지
	// 않는다. `var x`가 그 뒤에 나오면 정상적인 lexical shadowing이 가능하다.
	std::map<std::string, int>	_captures;

	~SLayerVar()
	{
		for (int i = (int)_varsLayer.size() - 1; i >= 0; i--)
			delete _varsLayer[i];
		_varsLayer.clear();
	}

	int FindVarOnlyCurrentBlock(const std::string& name)
	{
		int i = (int)_varsLayer.size() - 1;
		if(i >= 0)
		{
			int r = _varsLayer[i]->FindVar(name);
			if (r != -1)
				return r;
		}
		return -1;
	}

	int FindVar(const std::string& name)
	{
		for (int i = (int)_varsLayer.size() - 1; i >= 0; i--)
		{
			int r = _varsLayer[i]->FindVar(name);
			if(r != -1)
				return r;
		}
		auto it = _captures.find(name);
		if (it != _captures.end())
			return it->second;
		return -1;
	}

	int FindCapture(const std::string& name)
	{
		auto it = _captures.find(name);
		return it != _captures.end() ? it->second : -1;
	}

	void AddCapture(const std::string& name, int offset)
	{
		_captures[name] = offset;
	}

	void	AddLocalVar(const std::string& name, int offset)
	{
		int sz = (int)_varsLayer.size();
		if (sz <= 0)
			return; // Error

		_varsLayer[sz - 1]->_localVars[name] = offset;
	}
};

struct SFunctionLayer;
struct SVars
{
	std::vector<SLayerVar*>	_varsFunction;
	std::vector<std::string> _varsExport;
	// 모듈 변수의 이름만 분리한다. 실행 시에는 기존 전역 슬롯을 그대로 쓴다.
	// 현재 import의 입력 아카이브가 소유한다. 루트 파일은 nullptr이다.
	const std::string* _moduleName = nullptr;
	std::set<std::string> _hostGlobals;
	std::map<std::string, int> _exportSlots;
	std::set<std::string> _publicVars;   // public var 의 한정 이름("모듈.이름"). 별칭으로 읽고 쓸 수 있는 변수.
	std::string GlobalName(const std::string& name) const
	{
		return _moduleName == nullptr ? name : *_moduleName + "." + name;
	}
	std::string DeclarationName(const std::string& name) const
	{
		return _varsFunction.size() == 1 ? GlobalName(name) : name;
	}
	std::map<std::string, SFunctionLayer*> m_sImports; // Only Once Load
	// 지금 파싱 중인 import 체인. m_sImports 는 파싱을 마친 뒤에야 채워지므로,
	// A->B->A 같은 순환은 캐시로 막히지 않고 A 를 재진입 파싱해 이름 충돌로 죽는다
	// (게다가 사유가 안 남아 "메시지 없는 컴파일 실패"가 된다). 진입 시 여기에 넣어 막는다.
	std::set<std::string> m_sImporting;

	~SVars()
	{
		for (int i = (int)_varsFunction.size() - 1; i >= 0; i--)
			delete _varsFunction[i];
		_varsFunction.clear();
	}

	int FindVar(const std::string& name)
	{
		return FindVar(name, nullptr);
	}
	// 반환값이 어느 함수 lexical scope에서 왔는지도 함께 알려 준다. 익명 함수
	// 본문에서 상위 함수의 지역만 캡처하고, 전역은 기존 전역 경로로 남긴다.
	int FindVar(const std::string& name, SLayerVar** outLayer)
	{
		if (outLayer) *outLayer = nullptr;
		for (int i = (int)_varsFunction.size() - 1; i >= 0; i--)
		{
			int r = _varsFunction[i]->FindVar(i == 0 ? GlobalName(name) : name);
			if (r == -1 && i == 0 && _hostGlobals.count(name) != 0)
				r = _varsFunction[i]->FindVar(name);
			if (r != -1)
			{
				if (outLayer) *outLayer = _varsFunction[i];
				return r;
			}
		}
		return -1;
	}
	SLayerVar* GetCurrentLayer()
	{
		return _varsFunction[_varsFunction.size() - 1];
	}
	int _iTempVarNameIndex = 0; // 이름 없는 임시 변수 생성 인덱스
	int							_globalVarCount = 0;
    std::map<int, SlotType> _globalStorage;
};

struct CompileCallStorage
{
    std::vector<int> sources;
    std::vector<SlotType> types;
    int result = -1;
    int function = -1;
    int receiver = -1, member = -1;
    SlotType returnType;
    // 계획이 활성화되는 함수 내 코드 위치(바이트). PREPARE_CALL을 낸 경우 그 명령의 위치이고,
    // 내지 않은 직접 호출이면 첫 창 쓰기의 위치다. 내보내기와 저장소 추론이 이 위치로 활성 계획을 찾는다.
    int prepareOffset = 0;
};

struct SFunctionTableForWriter
{
	FUNCTION_TYPE				_funType;
	std::string					_name;
	int							_codePtr;
	short						_argsCount;
	short						_localTempMax;
	int							_localVarCount;
	std::vector<std::pair<short, short>> _captures;
    std::vector<SlotLayout> _storage;
    // 프레임 슬롯 [0, localAddCount)의 정책·임시 표시. 소유 슬롯은 인자 뒤 연속 구간이다.
    std::vector<u8> _slotInfo;
    int _ownedCount = 0;
    std::vector<CompileCallStorage> _calls;
    SlotType _resultType;
};

// 익명 함수 한 개가 생성될 때 부모 프레임에서 복사할 지역 슬롯과, 그 값을
// 받는 익명 함수 프레임의 숨은 지역 슬롯 쌍이다.
struct SClosureCapture
{
	short _sourceSlot = 0;
	short _localSlot = 0;
};


struct SFunctionInfo
{
	int							_funID; // Function Index
//	int							_staticIndex;
	std::string					_name;
	std::set<std::string>		_args;
	int							_built_in_arg_c;
	FUNCTION_TYPE				_funType = FUNT_NORMAL;
	bool _blPublic = false;   // public: 다른 모듈이 별칭으로 부를 수 있다. export(호스트)와 별개다.
	bool _bodyParsed = false;
	std::string					_moduleName;
	std::map<int, std::string>	_debugVarNames;
	std::vector<SClosureCapture>	_captures;
    std::map<int, SlotType> _storage;
    SlotType _returnStorage;
    std::vector<SlotType> _parameterStorage;
    std::vector<CompileCallStorage> _calls;
    // 루프 조건·증감식처럼 컴파일한 뒤 다른 위치로 옮겨 쓰는 코드 안의 호출 계획은 활성 위치도 함께 옮긴다.
    // PREPARE_CALL을 내지 않은 직접 호출은 이 위치로만 계획이 활성화된다.
    void ShiftCallPlans(std::size_t first, std::size_t last, int delta)
    {
        for (std::size_t i = first; i < last && i < _calls.size(); ++i) _calls[i].prepareOffset += delta;
    }
    // 옮긴 코드 때문에 계획 벡터 순서가 활성 위치 순서와 다를 수 있다. 활성화는 이 순서로 훑는다.
    std::vector<std::size_t> CallPlanOrder() const
    {
        std::vector<std::size_t> order(_calls.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b)
            { return _calls[a].prepareOffset < _calls[b].prepareOffset; });
        return order;
    }
    // 루프 상수 hoisting 이 만든 (숨은 지역 슬롯, static 상수 오퍼랜드) 쌍. 저장 종류 추론은 이 표로
    // 숨은 슬롯을 원래 상수로 되짚어 구조체 필드 키를 알아낸다.
    std::vector<std::pair<short, short> > _hoisted;
    // 몸통이 전역 하나를 돌려주는 RETURN뿐인 함수의 그 전역 슬롯. 0이면 그런 함수가 아니다.
    // 뒤에 오는 인자 없는 호출은 호출 창 없이 이 전역을 복사한다.
    int _globalGetterSlot = 0;


	int							_localVarCount;

	int							_localTempMax;
	int							_localTempCount;
	std::vector<SlotType> _tempStorage;
	std::vector<unsigned char> _tempBusy;
	std::vector<int> _tempOrder;

	CNArchive*					_code = NULL;
	int							_iCode_Begin = 0;
	int							_iCode_Size = 0;
	std::vector<debug_info>*	_pDebugData = nullptr;
	// Byte offsets in _code that can be entered by a previously emitted jump.
	// Used only while compiling this function to preserve control-flow joins.
	std::set<int>					_jumpTargetOffsets;

	void Clear()
	{
		_args.clear();
		_debugVarNames.clear();
		_captures.clear();
        _storage.clear(); _calls.clear();
        _globalGetterSlot = 0;
		_localVarCount = 0;
		_localTempMax = _localTempCount = 0;
		_tempStorage.clear(); _tempBusy.clear(); _tempOrder.clear();

		_iCode_Begin = 0;
		_iCode_Size = 0;
		_jumpTargetOffsets.clear();
	}

	int AddCapture(const std::string& name, int sourceSlot, SLayerVar* layer)
	{
		int existing = layer->FindCapture(name);
		if (existing != -1)
			return existing;

		const int slot = AllocLocalVar();
		CheckCompileRange("capture source slot", sourceSlot, 1, COMPILE_LOCALTMP_VAR_BEGIN - 1);
		layer->AddCapture(name, slot);
		_debugVarNames[slot] = name;
		SClosureCapture capture;
		capture._sourceSlot = (short)sourceSlot;
		capture._localSlot = (short)slot;
		_captures.push_back(capture);
		return slot;
	}

	std::string	GetFullName() { if(_moduleName.empty() == true) return _name; return _moduleName + "." + _name; }


	int AllocLocalVar()
	{
		const int slot = 1 + (int)_args.size() + _localVarCount;
		CheckCompileRange("local variable slot", slot, 1, COMPILE_LOCALTMP_VAR_BEGIN - 1);
		++_localVarCount;
		return slot;
	}

    void SetStorage(int slot, const SlotType& type)
    {
        if (type.kind == SlotKind::Boxed || slot < 0 || slot >= COMPILE_STATIC_VAR_BEGIN) return;
        const auto previous = _storage.find(slot);
        if (previous != _storage.end() && previous->second.kind != SlotKind::Boxed)
        {
            if (previous->second.kind != type.kind)
                throw std::logic_error("conflicting compiler slot types");
            return;
        }
        _storage[slot] = type;
        if (slot >= COMPILE_LOCALTMP_VAR_BEGIN && slot < COMPILE_CALLARG_VAR_BEGIN)
        {
            const int index = slot - COMPILE_LOCALTMP_VAR_BEGIN - 1 - static_cast<int>(_args.size());
            if (index >= 0 && index < static_cast<int>(_tempStorage.size())) _tempStorage[index] = type;
        }
    }
	int AllocLocalTempVar(const SlotType& type = SlotType())
	{
		std::size_t index = 0;
		for (; index < _tempStorage.size(); ++index)
			if (type.kind != SlotKind::Boxed && !_tempBusy[index] && _tempStorage[index].kind == type.kind && _tempStorage[index].typeId == type.typeId) break;
		if (index == _tempStorage.size()) { _tempStorage.push_back(type); _tempBusy.push_back(0); }
		return UseTempSlot(index);
	}
	// 벡터 생성·저장 인자는 연속 구간을 요구한다. 개별 타입별 재사용과 섞어도 순서를 보존한다.
	int AllocTempWindow(int count)
	{
		const std::size_t begin = _tempStorage.size();
		while (_tempStorage.size() < begin + count) { _tempStorage.push_back(SlotType()); _tempBusy.push_back(0); }
		const int first = COMPILE_LOCALTMP_VAR_BEGIN + 1 + static_cast<int>(_args.size()) + static_cast<int>(begin);
		for (int i = 0; i < count; ++i) UseTempSlot(begin + i);
		return first;
	}
	int UseTempSlot(std::size_t index)
	{
		const int slot = COMPILE_LOCALTMP_VAR_BEGIN + 1 + static_cast<int>(_args.size()) + static_cast<int>(index);
		CheckCompileRange("temporary variable slot", slot, COMPILE_LOCALTMP_VAR_BEGIN, COMPILE_STATIC_VAR_BEGIN - 1);
		_tempBusy[index] = 1; _tempOrder.push_back(static_cast<int>(index));
		_localTempCount = static_cast<int>(_tempOrder.size());
		_localTempMax = static_cast<int>(_tempStorage.size());
		if (_tempStorage[index].kind != SlotKind::Boxed) _storage[slot] = _tempStorage[index];
		return slot;
	}
	void RestoreTemps(int count)
	{
		while (static_cast<int>(_tempOrder.size()) > count) { _tempBusy[_tempOrder.back()] = 0; _tempOrder.pop_back(); }
		_localTempCount = count;
	}
	void	FreeLocalTempVar()
	{
		RestoreTemps(0);
	}


	struct OperandTemps
	{
		SFunctionInfo& fun;
		int saved;
		explicit OperandTemps(SFunctionInfo& f) : fun(f), saved(f._localTempCount) {}
		~OperandTemps() { fun.RestoreTemps(saved); }
	};
	void Push_LOADK(CArchiveRdWC& ar, int dest, int index, int line = -1)
	{
		AddDebugData(ar, line);
		_iLastOPOffset = _code->GetBufferOffset();
		OpType op = NOP_LOADK;
		_code->Write(&op, sizeof(op));
		Push_NoFlag(dest, index);
	}
	int ReadOperand(CArchiveRdWC& ar, int value)
	{
		if (value < COMPILE_EXTENDED_STATIC_BEGIN)
			return CompileShort("operand", value);
		const int temp = AllocLocalTempVar();
		Push_LOADK(ar, temp, StaticPoolIndex(value));
		return temp;
	}

	int _iLastOPOffset = -1;
	int _callOpEmitCount = 0; // CALL/PTRCALL/PTRCALL2 emit 횟수 (인자 융합 안전성 판정용)
	void ClearLastOP()
	{
		_iLastOPOffset = -1;
	}
	// n1(dest) 을 다른 슬롯으로 바꿔도 의미가 보존되는 단일-결과 op 인가.
	// Push_OP2 의 인접 peephole 과 함수 호출 인자 융합(지연 복사 패치)이 공유한다.
	static bool IsRetargetableProducerOP(OperationCode op)
	{
		const TypedOpInfo* typed = GetTypedOpInfo(op);
		if (typed != nullptr) return !typed->jump;
		// 저장 명령은 n1이 수신 컨테이너이므로 목적지 바꾸기를 적용하지 않는다.
		const AggregateOpInfo* aggregate = GetAggregateOpInfo(op);
		if (aggregate != nullptr) return aggregate->shape != AggregateOpShape::Store;
		switch (op)
		{
		case NOP_MOV:
		case OP_MOV_IMM_FLOAT:
		case NOP_LOADK:
		case NOP_LOG_NOT:
		case NOP_VAR_CLEAR:
		case OP_GREAT:		// >
		case OP_GREAT_EQ:	// >=
		case OP_LESS:		// <
		case OP_LESS_EQ:	// <=
		case OP_EQUAL2:	// ==
		case OP_NEQUAL:	// !=
		case NOP_STR_ADD:
		case NOP_TOSTRING:
		case NOP_TOINT:
		case NOP_TOFLOAT:
		case NOP_TOSIZE:
		case NOP_TABLE_ALLOC:
			return true;
		default:
			return false;
		}
	}
	OperationCode GetLastOP()
	{
		if (_iLastOPOffset < 0)
			return NOP_NONE;

		return GetOPPointer(_iLastOPOffset)->Code();
	}
	OperationCode GetOP(int iOffsetOP)
	{
		return GetOPPointer(iOffsetOP)->Code();
	}
	SVMOperation* GetOPPointer(int iOffsetOP)
	{
		return (SVMOperation*)((u8*)_code->GetData() + iOffsetOP);
	}
	s16 GetN(int iOffsetOP, int n)
	{
		s16* pN = (s16*)((u8*)_code->GetData() + iOffsetOP + sizeof(OpType) + sizeof(ArgFlag));
		return pN[n];
	}
	void SetN(int iOffsetOP, int n, s16 v)
	{
		s16* pN = (s16*)((u8*)_code->GetData() + iOffsetOP + sizeof(OpType) + sizeof(ArgFlag));
		pN[n] = v;
	}
	void    Push_Flag(ArgFlag arg, int a1, int a2, int a3)
	{
		_code->Write(&arg, sizeof(arg));
		const short n1 = CompileShort("operand", a1);
		_code->Write(&n1, sizeof(n1));
		const short n2 = CompileShort("operand", a2);
		_code->Write(&n2, sizeof(n2));
		const short n3 = CompileShort("operand", a3);
		_code->Write(&n3, sizeof(n3));
	}
	void    Push_NoFlag(int a1, int a2, int a3)
	{
		ArgFlag arg = 0;//
		_code->Write(&arg, sizeof(arg));
		const short n1 = CompileShort("operand", a1);
		_code->Write(&n1, sizeof(n1));
		const short n2 = CompileShort("operand", a2);
		_code->Write(&n2, sizeof(n2));
		const short n3 = CompileShort("operand", a3);
		_code->Write(&n3, sizeof(n3));
	}
	void    Push_NoFlag(int a1, int a23)
	{
		ArgFlag arg = 0;//
		_code->Write(&arg, sizeof(arg));
		const short n1 = CompileShort("operand", a1);
		_code->Write(&n1, sizeof(n1));
		_code->Write(&a23, sizeof(a23));
	}
	void	AddDebugData(CArchiveRdWC& ar, int iDebugLoopLine = -1)
	{
		if (ar._debug == false) return;
		CheckCompileRange("debug line", iDebugLoopLine != -1 ? iDebugLoopLine : ar.CurLine(), 0, USHRT_MAX);
		int iOff = _code->GetBufferOffset() / 8;
		if ((int)_pDebugData->size() < iOff + 1)
			_pDebugData->resize(iOff + 1);

		(*_pDebugData)[iOff] = debug_info(ar.CurFile(), iDebugLoopLine != -1 ? iDebugLoopLine : ar.CurLine());
	}
	void	Push_OP(CArchiveRdWC& ar, OperationCode op, int r, int a1, int a2, int debugLine = -1)
	{
		OperandTemps temps(*this);
		a1 = ReadOperand(ar, a1); a2 = ReadOperand(ar, a2);
		AddDebugData(ar, debugLine);
		_iLastOPOffset = _code->GetBufferOffset();

		OpType optype = PrimaryOperation(op);
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&r, sizeof(r));
		Push_Flag(OperationSubCode(op), r, a1, a2);
	}
	void	Push_OP(CArchiveRdWC& ar, OperationCode op, int r, int a1, int a2, bool b2, bool b3)
	{
		OperandTemps temps(*this);
		a1 = ReadOperand(ar, a1); a2 = ReadOperand(ar, a2);
		AddDebugData(ar);
		_iLastOPOffset = _code->GetBufferOffset();

		OpType optype = PrimaryOperation(op);
		_code->Write(&optype, sizeof(optype));

		Push_Flag(OperationSubCode(op), r, a1, a2);
	}
    // 숫자 payload만 오가는 직접 스크립트 호출은 창을 비울 필요도, 실행 중에 계획을 고를 필요도 없다.
    // 피호출자가 내장·익명 함수이거나 캡처가 있거나 인자·반환 종류가 payload가 아니면 PREPARE_CALL을 낸다.
    static bool CallWindowNeedsPrepare(const SFunctionInfo* callee, std::size_t argumentCount)
    {
        if (callee == nullptr || callee->_funType == FUNT_BUILT_IN || callee->_funType == FUNT_ANONYMOUS) return true;
        if (!callee->_captures.empty() || callee->_parameterStorage.size() != argumentCount) return true;
        for (const SlotType& parameter : callee->_parameterStorage)
            if (parameter.kind == SlotKind::Boxed || PolicyOf(parameter.kind) != StoragePolicy::Payload) return true;
        const SlotType& result = callee->_returnStorage;
        if (result.kind == SlotKind::Boxed) return result.typeId != static_cast<u32>(NeoCore::TypeKind::Void);
        return PolicyOf(result.kind) != StoragePolicy::Payload;
    }
    // emit이 거짓이면 계획만 기록한다. 활성 계획은 prepareOffset으로 찾으므로 명령이 없어도 창 쓰기의 종류가 정해진다.
    void PrepareCall(CArchiveRdWC& ar, const std::vector<int>& sources, int line = -1, bool emit = true)
    {
        CompileCallStorage plan; plan.sources = sources; plan.types.resize(sources.size());
        plan.prepareOffset = _code->GetBufferOffset() - _iCode_Begin;
        _calls.push_back(plan);
        if (emit) Push_OP(ar, NOP_PREPARE_CALL, CompileShort("call storage plan", static_cast<int>(_calls.size()) - 1), 0, 0, line);
    }
	void	Push_Call(CArchiveRdWC& ar, OperationCode op, int fun, int args, int res, int iDebugLine = -1)
	{
        assert(!_calls.empty());
        _calls.back().result = res; _calls.back().function = fun;
		AddDebugData(ar, iDebugLine);
		_iLastOPOffset = _code->GetBufferOffset();
		++_callOpEmitCount;

		OpType optype = PrimaryOperation(op);
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&fun, sizeof(fun));
		//_code->Write(&args, sizeof(args));
		Push_Flag(OperationSubCode(op), fun, args, res);
	}
	void	Push_CallPtr(CArchiveRdWC& ar, int table, int index, int args, bool isRet = false, int iDebugLine = -1)
	{
		OperandTemps temps(*this);
		table = ReadOperand(ar, table); index = ReadOperand(ar, index);
        assert(!_calls.empty()); _calls.back().result = STACK_POS_RETURN;
        _calls.back().receiver = table; _calls.back().member = index;
		AddDebugData(ar, iDebugLine);
		_iLastOPOffset = _code->GetBufferOffset();
		++_callOpEmitCount;

		OpType optype = PrimaryOperation(NOP_PTRCALL);
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&table, sizeof(table));
		//_code->Write(&index, sizeof(index));
		//_code->Write(&args, sizeof(args));
		Push_Flag(isRet ? NEOS_OP_CALL_NORESULT : 0,table, index, args);
	}
	void	Push_CallPtr2(CArchiveRdWC& ar, int index, int args, int res, int iDebugLine = -1)
	{
		OperandTemps temps(*this);
		index = ReadOperand(ar, index);
        assert(!_calls.empty()); _calls.back().result = res;
		AddDebugData(ar, iDebugLine);
		_iLastOPOffset = _code->GetBufferOffset();
		++_callOpEmitCount;

		OpType optype = PrimaryOperation(NOP_PTRCALL2);
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&table, sizeof(table));
		//_code->Write(&index, sizeof(index));
		//_code->Write(&args, sizeof(args));
		Push_NoFlag(index, args, res);
	}
	void	Push_OP2(CArchiveRdWC& ar, OperationCode op, int r, int s, bool b2, int iDebugLine = -1)
	{
		OperandTemps temps(*this);
		if (op == NOP_MOV && s >= COMPILE_EXTENDED_STATIC_BEGIN)
		{
			Push_LOADK(ar, r, StaticPoolIndex(s), iDebugLine);
			return;
		}
		if (op != NOP_FMOV1) s = ReadOperand(ar, s);
		if (op == NOP_MOV)
		{
			if (b2 == false && IsTempVar(s))
			{
				if (IsRetargetableProducerOP(GetLastOP()))
				{
					u8 *pre = (u8*)_code->GetData() + sizeof(OpType) + sizeof(ArgFlag) + _iLastOPOffset;
					short* preDest = (short*)pre;
					// 호출 창 슬롯으로는 고정 숫자 명령만 바로 쓴다(PREPARE_CALL을 내지 않은 호출의 마지막 인자).
					// 복사·읽기 명령은 원본 종류가 이전 계획에 묶여 있어 창에 직접 쓰지 않는다.
					const bool window = r >= COMPILE_CALLARG_VAR_BEGIN;
					if (*preDest == s && (!window || (GetTypedOpInfo(GetLastOP()) != nullptr && !_calls.empty())))
					{
                        // 생략한 임시의 고정 타입도 실제 목적지에 전달한다.
                        const auto sourceStorage = _storage.find(s);
                        if (sourceStorage != _storage.end() && r >= 0 && !window)
                            SetStorage(r, sourceStorage->second);
                        // 창에 직접 쓰는 명령부터 이 호출의 계획이 창 슬롯의 종류를 정한다.
                        if (window && _iLastOPOffset - _iCode_Begin < _calls.back().prepareOffset)
                            _calls.back().prepareOffset = _iLastOPOffset - _iCode_Begin;
                        *preDest = CompileShort("destination operand", r);
						return;
					}
				}
			}
		}

		AddDebugData(ar, iDebugLine);
		_iLastOPOffset = _code->GetBufferOffset();

		OpType optype = PrimaryOperation(op);
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&r, sizeof(r));
		//_code->Write(&s, sizeof(s));

		Push_Flag(OperationSubCode(op), r, s, 0);
	}
	// 즉값 저장은 NOP_MOV의 세부 코드로 낸다. n23이 즉값이다.
	void    Push_Immediate(unsigned sub, int a1, int a23)
	{
		ArgFlag arg = static_cast<ArgFlag>(sub);
		_code->Write(&arg, sizeof(arg));
		const short n1 = CompileShort("operand", a1);
		_code->Write(&n1, sizeof(n1));
		_code->Write(&a23, sizeof(a23));
	}
	void	Push_MOVI(CArchiveRdWC& ar, int r, int v, int iDebugLine = -1)
	{
		AddDebugData(ar, iDebugLine);
		_iLastOPOffset = _code->GetBufferOffset();

		OpType optype = PrimaryOperation(NOP_MOV);
		_code->Write(&optype, sizeof(optype));
		Push_Immediate(MOV_SUB_IMMEDIATE_INT, r, v);
	}
	bool	Push_MOVF(CArchiveRdWC& ar, int r, NS_FLOAT v, int iDebugLine = -1)
	{
		// 범위 밖 캐스트를 피하고, 부호 있는 0까지 원래 double 값과 같을 때만 즉값을 쓴다.
		const NS_FLOAT limit = (std::numeric_limits<float>::max)();
		if (!std::isfinite(v) || v < -limit || v > limit) return false;
		const float immediate = static_cast<float>(v);
		if (static_cast<NS_FLOAT>(immediate) != v || std::signbit(immediate) != std::signbit(v)) return false;
		AddDebugData(ar, iDebugLine);
		_iLastOPOffset = _code->GetBufferOffset();
		OpType optype = PrimaryOperation(NOP_MOV);
		_code->Write(&optype, sizeof(optype));
		int bits = 0;
		std::memcpy(&bits, &immediate, sizeof(bits));
		Push_Immediate(MOV_SUB_IMMEDIATE_FLOAT, r, bits);
		return true;
	}
	void	Push_OP1(CArchiveRdWC& ar, OperationCode op, int r)
	{
		AddDebugData(ar);
		_iLastOPOffset = _code->GetBufferOffset();

		OpType optype = PrimaryOperation(op);
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&r, sizeof(r));
		Push_Flag(OperationSubCode(op), r, 0, 0);
	}
	void	Push_OP0(CArchiveRdWC& ar, OperationCode op)
	{
		AddDebugData(ar);
		_iLastOPOffset = _code->GetBufferOffset();

		OpType optype = PrimaryOperation(op);
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&r, sizeof(r));
		Push_Flag(OperationSubCode(op), 0, 0, 0);
	}
	void	Push_RETURN(CArchiveRdWC& ar, int r, bool b1, int debugLine = -1)
	{
		OperandTemps temps(*this);
		r = ReadOperand(ar, r);
		OpType optype = PrimaryOperation(NOP_RETURN);
		if(b1 == false)
		{
			OperationCode preOP = GetLastOP();
			switch (preOP)
			{
			case NOP_MOV:
			{
				ArgFlag* preArgFlag = (ArgFlag*)((u8*)_code->GetData() + sizeof(OpType) + _iLastOPOffset);
				short* preDest = (short*)((u8*)_code->GetData() + sizeof(OpType) + sizeof(ArgFlag) + _iLastOPOffset);
				short* preSrc = preDest + 1;
				// 점프 도착 지점의 MOV를 RET로 바꾸면 반환을 건너뛴다.
				// 관찰 가능한 변수 대입도 남겨 두고 임시 슬롯만 합친다.
				const bool isControlFlowJoin =
					_jumpTargetOffsets.find(_code->GetBufferOffset()) != _jumpTargetOffsets.end();
				if (*preDest == r && IsTempVar(r) && isControlFlowJoin == false)
				{
					*(OpType*)((u8*)_code->GetData() + _iLastOPOffset) = optype;
					*preDest = *preSrc;
					*preSrc = 0;
					*preArgFlag = 0;
                    if (ar._debug && debugLine != -1)
                    {
                        CheckCompileRange("debug line", debugLine, 0, USHRT_MAX);
                        (*_pDebugData)[_iLastOPOffset / 8] = debug_info(ar.CurFile(), debugLine);
                    }
					return;
				}
				break;
			}
			default:
				break;
			}
		}
		AddDebugData(ar, debugLine);
		_iLastOPOffset = _code->GetBufferOffset();

		_code->Write(&optype, sizeof(optype));
		//_code->Write(&r, sizeof(r));

		Push_NoFlag(r, 0, 0);
	}
	//void	Push_FUNEND(CArchiveRdWC& ar)
	//{
	//	AddDebugData(ar);
	//	_iLastOPOffset = _code->GetBufferOffset();

	//	OpType optype = PrimaryOperation(NOP_FUNEND);
	//	_code->Write(&optype, sizeof(optype));
	//	Push_Arg(0, 0, 0);
	//}


	void	Push_JMP(CArchiveRdWC& ar, int destOffset)
	{
		AddDebugData(ar);
		_iLastOPOffset = _code->GetBufferOffset();

		OperationCode op = NOP_JMP;
		OpType optype = PrimaryOperation(op);
		short add = destOffset == 0 ? 0 : CompileShort("jump distance", (destOffset - (_code->GetBufferOffset() + sizeof(SVMOperation))) / (int)sizeof(SVMOperation));
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&add, sizeof(add));
		Push_NoFlag(add, 0, 0);
	}
	void	Push_JMPFalse(CArchiveRdWC& ar, int var, int destOffset)
	{
		OperandTemps temps(*this);
		var = ReadOperand(ar, var);
		AddDebugData(ar);
		_iLastOPOffset = _code->GetBufferOffset();

		OperationCode op = NOP_JMP_FALSE;
		OpType optype = PrimaryOperation(op);
		short add = destOffset == 0 ? 0 : CompileShort("jump distance", (destOffset - (_code->GetBufferOffset() + sizeof(SVMOperation))) / (int)sizeof(SVMOperation));
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&var, sizeof(var));
		//_code->Write(&add, sizeof(add));
		Push_NoFlag(add, var, 0);
	}
	void	Push_JMPTrue(CArchiveRdWC& ar, int var, int destOffset, int iDebugLoopLine)
	{
		OperandTemps temps(*this);
		var = ReadOperand(ar, var);
		AddDebugData(ar, iDebugLoopLine);
		_iLastOPOffset = _code->GetBufferOffset();

		OperationCode op = NOP_JMP_TRUE;
		OpType optype = PrimaryOperation(op);
		short add = destOffset == 0 ? 0 : CompileShort("jump distance", (destOffset - (_code->GetBufferOffset() + sizeof(SVMOperation))) / (int)sizeof(SVMOperation));
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&var, sizeof(var));
		//_code->Write(&add, sizeof(add));
		Push_NoFlag(add, var, 0);
	}
	void	Push_JMPFor(CArchiveRdWC& ar, int destOffset, int table, int key, int iDebugLoopLine)
	{
		AddDebugData(ar, iDebugLoopLine);
		_iLastOPOffset = _code->GetBufferOffset();

		OperationCode op = NOP_JMP_FOR;
		OpType optype = PrimaryOperation(op);
		short add = destOffset == 0 ? 0 : CompileShort("jump distance", (destOffset - (_code->GetBufferOffset() + sizeof(SVMOperation))) / (int)sizeof(SVMOperation));
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&add, sizeof(add));
		//_code->Write(&table, sizeof(table));
		//_code->Write(&key, sizeof(key));
		Push_NoFlag(add, table, key);
	}
	// Always Value is Key Next Alloc ID
	// bTwoVar: foreach(var k, v in ...) 2변수 형태. 대상 타입은 런타임에만 알 수 있어
	// 플래그만 실어 보내고, list + 2변수 미지원 판정은 ForEach 런타임이 한다.
	void	Push_JMPForEach(CArchiveRdWC& ar, int destOffset, int table, int key, int iDebugLoopLine, bool bTwoVar)
	{
		AddDebugData(ar, iDebugLoopLine);
		_iLastOPOffset = _code->GetBufferOffset();

		OperationCode op = NOP_JMP_FOREACH;
		OpType optype = PrimaryOperation(op);
		short add = destOffset == 0 ? 0 : CompileShort("jump distance", (destOffset - (_code->GetBufferOffset() + sizeof(SVMOperation))) / (int)sizeof(SVMOperation));
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&add, sizeof(add));
		//_code->Write(&table, sizeof(table));
		//_code->Write(&key, sizeof(key));
		Push_Flag(bTwoVar ? NEOS_OP_FOREACH_TWOVAR : 0, add, table, key);
	}
	// n1 = switch table index (부호 없이 해석, 0~65535), n2 = 조건식 결과 위치.
	// 점프는 런타임이 table 에서 찾는다.
	void	Push_Switch(CArchiveRdWC& ar, u16 tableIndex, int keyVar)
	{
		OperandTemps temps(*this);
		keyVar = ReadOperand(ar, keyVar);
		AddDebugData(ar);
		_iLastOPOffset = _code->GetBufferOffset();

		OpType optype = PrimaryOperation(NOP_SWITCH);
		_code->Write(&optype, sizeof(optype));
		Push_NoFlag((short)tableIndex, keyVar, 0);   // 런타임이 u16 으로 되읽는다
	}
	void	Set_JumpOffet(SJumpValue sJmp, int destOffset)
	{
		// offset 은 op(SVMOperation) 단위. dest/base 는 바이트 버퍼 위치라 차이를 op 크기로 나눈다.
		*((short*)((u8*)_code->GetData() + sJmp._iCodePosOffset)) = CompileShort("jump distance", (destOffset - sJmp._iBaseJmpOffset) / (int)sizeof(SVMOperation));
		_jumpTargetOffsets.insert(destOffset);
	}
	void	Push_ListAlloc(CArchiveRdWC& ar, int r)
	{
		AddDebugData(ar);
		_iLastOPOffset = _code->GetBufferOffset();

		OpType optype = PrimaryOperation(NOP_LIST_ALLOC);
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&r, sizeof(r));
		Push_NoFlag(r, 0, 0);
	}
	void	Push_TableAlloc(CArchiveRdWC& ar, int r)
	{
		AddDebugData(ar);
		_iLastOPOffset = _code->GetBufferOffset();

		OpType optype = PrimaryOperation(NOP_TABLE_ALLOC);
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&r, sizeof(r));
		Push_NoFlag(r, 0, 0);
	}
	void	Push_Table_MASMDP(CArchiveRdWC& ar, OperationCode op, int nTable, int nArray, int nValue, bool b1, bool b2, bool b3)
	{
		OperandTemps temps(*this);
		nTable = ReadOperand(ar, nTable); nArray = ReadOperand(ar, nArray);
		if (op != NOP_FMOV2) nValue = ReadOperand(ar, nValue);
		// null 대입을 REMOVE로 바꾸지 않는다. 대상은 런타임에 목록일 수도 있다.
		// CltInsert가 목록의 길이/인덱스를 보존하고, 표에 대해서만 키를 삭제한다.
		AddDebugData(ar);
		_iLastOPOffset = _code->GetBufferOffset();

		OpType optype = PrimaryOperation(op == NOP_MOV ? NOP_CLT_MOV : op);
		_code->Write(&optype, sizeof(optype));
		Push_Flag(OperationSubCode(op), nTable, nArray, nValue);
	}
	void Push_TableWriteRef(CArchiveRdWC& ar, int owner, int key,
        int temp, int component, int value, bool loaded)
    {
        // LOADK가 쓰기 참조 쌍을 나누지 않도록 피연산자를 먼저 확정한다.
        OperandTemps temps(*this);
        owner = ReadOperand(ar, owner); key = ReadOperand(ar, key);
        component = ReadOperand(ar, component); value = ReadOperand(ar, value);
        AddDebugData(ar);
        _iLastOPOffset = _code->GetBufferOffset();
        OpType type = PrimaryOperation(NOP_CLT_WRITE_REF);
        _code->Write(&type, sizeof(type));
        Push_Flag(loaded ? NEOS_OP_REF_LOADED : 0, owner, key, temp);
        AddDebugData(ar);
        _iLastOPOffset = _code->GetBufferOffset();
        type = PrimaryOperation(NOP_CLT_MOV);
        _code->Write(&type, sizeof(type));
        Push_Flag(NEOS_OP_REF_WRITE, temp, component, value);
    }
	void	Push_TableRead(CArchiveRdWC& ar, int nTable, int nArray, int nValue, bool b2, OperationCode op = NOP_CLT_READ) // value = table[nArray]
	{
		OperandTemps temps(*this);
		nTable = ReadOperand(ar, nTable); nArray = ReadOperand(ar, nArray);
		AddDebugData(ar);
		_iLastOPOffset = _code->GetBufferOffset();

		OpType optype = PrimaryOperation(op);
		_code->Write(&optype, sizeof(optype));

		Push_Flag(OperationSubCode(op), nTable, nArray, nValue);
	}
	void	Push_ToType(CArchiveRdWC& ar, OperationCode op, int r, int s)
	{
		OperandTemps temps(*this);
		s = ReadOperand(ar, s);
		AddDebugData(ar);
		_iLastOPOffset = _code->GetBufferOffset();

		OpType optype = PrimaryOperation(op);
		_code->Write(&optype, sizeof(optype));
		//_code->Write(&r, sizeof(r));
		//_code->Write(&s, sizeof(s));
		Push_Flag(OperationSubCode(op), r, s, 0);
	}
};


struct SFunctionLayer
{
	bool _blBuiltInModule = false;
	// import 한 파일 이름. 이 모듈의 최상위 변수는 전역 층에 "_moduleName.이름" 으로 있어
	// 별칭으로 public var 를 찾을 때 한정 이름을 만드는 데 쓴다. 루트와 내장 모듈은 비어 있다.
	std::string _moduleName;
	std::map<std::string, SFunctionInfo*>	_funs;
	std::map< std::string, SFunctionLayer*> _defModules;
	NeoCompileDefines _exportedConsts; // public const. Compile-time values only; never runtime globals.
};

// 컴파일 타임 switch case 1개. Program 의 ProgramSwitchKey 와 같은 값 모델.
struct SSwitchCaseCompile
{
	VAR_TYPE	_type = VAR_NONE;   // VAR_INT / VAR_STRING (bool/float 는 허용 안 함)
	NS_INT		_int = 0;
	std::string	_str;
	int			_jumpOffset = 0;    // NOP_SWITCH 다음 op 기준 상대 op offset
};

struct SSwitchTableCompile
{
	std::vector<SSwitchCaseCompile>	_cases;
	int								_defaultOffset = 0;
};

// 컴파일 중에는 lower/upper가 compiler slot 표현이다. Export가 owning function의
// stack/global slot으로 바꾼 뒤 'RNGE' chunk로 저장한다.
struct SRangeJumpCompile
{
	short	_lower = 0;
	short	_upper = 0;
	u8		_flags = 0;
	int		_ownerFunction = -1;
};

struct SFunctions
{
    const NeoCore::TypedIR* _typedIR = nullptr;
	std::set<std::string> _exportNames;
    std::unordered_map<std::string, SlotType> _declaredStorage;
    std::map<std::string, std::map<std::string, SlotType>> _helperStorage;
    SlotType FindDeclaredStorage(const std::string& name) const
    {
        const auto function = _helperStorage.find(_cur->GetFullName());
        if (function != _helperStorage.end())
        {
            const auto local = function->second.find(name);
            if (local != function->second.end()) return local->second;
        }
        const auto value = _declaredStorage.find(name);
        return value != _declaredStorage.end() ? value->second : SlotType();
    }
	std::vector<SFunctionLayer*>			_funLayers;
	std::map<int, SFunctionInfo*>			_funIDs;
	std::list<SFunctionInfo*>				_funSequence;

	SFunctionInfo*						_cur = nullptr;
	std::vector<BoundaryValue>				_staticVars;
	// 숫자 상수의 중복 제거 색인: NS_FLOAT 비트 / int 값 → 풀 인덱스. 선형 탐색은 리터럴 수의 제곱으로 느려졌다.
	std::unordered_map<uint64_t, int>	_floatConstants;
	std::unordered_map<NS_INT, int>		_intConstants;
	// String slots are never reclaimed by numeric immediate folding. Keep their
	// lookup separate so large text pools do not require a quadratic scan.
	std::unordered_map<std::string, int> _stringConstants;
	// 마지막 상수 슬롯을 '어느 함수의 어느 코드 오프셋에서' 만들었는지.
	// 리터럴을 즉값 명령으로 대체할 때, 그 뒤로 코드가 방출되지 않았다면
	// 그 슬롯을 참조하는 명령도 있을 수 없다 — 이 비교 하나로 회수 가능 여부가 정해진다.
	SFunctionInfo*						_lastStaticFun = nullptr;
	// 바깥 파서가 이미 훑어 둔 식 부작용 비트. 다음 ParseJob 이 같은 소스 오프셋에서 시작하면 재스캔 없이 쓴다.
	// (호출 인자·괄호 부분식은 바깥 범위의 부분집합이라 바깥 비트가 보수적 상한이다.)
	int									_presetExprEffects = -1;
	int									_presetExprOffset = -1;
	int									_lastStaticCodeOffset = -1;
	// 그 오프셋에서 시작된 상수 '묶음'의 첫 인덱스. 접기로 여러 개를 되돌릴 때 하한이 된다.
	int									_staticRunBegin = 0;
	// switch 테이블은 _staticVars 처럼 프로그램 전역으로 누적된다(함수별이 아님).
	// NOP_SWITCH.n1 = 이 배열의 인덱스이고, Export 가 'SWIT' 청크로 써서
	// 런타임에 CNeoVMProgram::switchTables 가 된다.
	std::vector<SSwitchTableCompile>	_switchTables;
	// NOP_JMP_RANGE_*.n3 = 이 배열의 인덱스. 인덱스는 unsigned short로 저장한다.
	std::vector<SRangeJumpCompile>	_rangeJumps;

	CNArchive		_codeFinal;
	CNArchive		_codeTemp;

	SFunctionLayer*			_curModule = nullptr;
	int				_lastModuleIndex = -1;

	std::vector<debug_info> m_sDebugFinal;
	std::vector<debug_info> m_sDebugTemp;

	~SFunctions()
	{
		for(int i = (int)_funLayers.size() - 1; i >= 0; i--)
		{
			SFunctionLayer* pFLayer = _funLayers[i];
			for(auto it = pFLayer->_funs.begin(); it != pFLayer->_funs.end(); it++)
			{
				SFunctionInfo* f = (*it).second;
				delete f;
			}
			pFLayer->_funs.clear();
			delete pFLayer;
		}
		_funLayers.clear();
		for (int i = (int)_staticVars.size() - 1; i >= 0; i--)
		{
			BoundaryValue& v2 = _staticVars[i];
			if (VAR_STRING == v2.GetType())
			{
				delete v2._str;
			}
		}
		_staticVars.clear();
	}

	inline const std::string& GetCurFunName() { return _cur->_name; }

	int GetFunCountAll()
	{
		int cnt = 0;
		for (int i = (int)_funLayers.size() - 1; i >= 0; i--)
		{
			SFunctionLayer* pFLayer = _funLayers[i];
			if(pFLayer->_blBuiltInModule)
				continue;
			cnt += (int)pFLayer->_funs.size();
		}
		return cnt;
	}

	SFunctionLayer* NewLayer()
	{
		SFunctionLayer* pFLayer = new SFunctionLayer();
		++_lastModuleIndex;
		_funLayers.push_back(pFLayer);
		return pFLayer;
	}

	SFunctionInfo* NewFun(const std::string& name, CNArchive* code, std::vector<debug_info>* pDebugData)
	{
		SFunctionInfo* p = FindFun(name);
		if(p != nullptr)
			return nullptr;
		p = new SFunctionInfo();
		p->Clear();
		p->_name = name;
        if (_typedIR)
            for (const NeoCore::IRFunction& signature : _typedIR->functions)
                if (signature.backendName == name)
                {
                    p->_returnStorage = CoreStorageType(_typedIR->types, signature.returnType);
                    for (NeoCore::TypeId type : signature.parameterTypes)
                        p->_parameterStorage.push_back(CoreStorageType(_typedIR->types, type));
                    break;
                }

		p->_code = code;
		p->_pDebugData = pDebugData;
		_curModule->_funs[name] = p;
		return p;
	}

	SFunctionInfo*	FindFun(const std::string& name)
	{
		auto it = _curModule->_funs.find(name);
		if (it == _curModule->_funs.end())
			return NULL;
		return (*it).second;
	}
	SFunctionInfo* FindFun(const std::string& name, SFunctionLayer* pLayer)
	{
		auto it = pLayer->_funs.find(name);
		if (it == pLayer->_funs.end())
			return NULL;
		return (*it).second;
	}
	SFunctionInfo*	FindFun(int ifun)
	{
		auto it = _funIDs.find(ifun);
		if (it == _funIDs.end())
			return NULL;
		return (*it).second;
		//return FindFun((*it).second);
	}

	// 방금 만든 상수 슬롯의 출처를 기록한다(회수 판정용).
	// 마지막 상수를 두 곳 이상에서 받아 갔는가. 같은 리터럴이 아직 방출되지 않은 바깥 피연산자와 지금의 즉값
	// 인자에 함께 쓰이면(`7 + F(7, 1)`) 되돌릴 수 없다: 바깥 피연산자의 색인이 다음 상수를 가리키게 된다.
	bool _lastStaticShared = false;
	void MarkLastStaticOrigin()
	{
		_lastStaticShared = false;
		const int off = (_cur != nullptr && _cur->_code != nullptr)
			? _cur->_code->GetBufferOffset() : -1;
		if (_lastStaticFun != _cur || _lastStaticCodeOffset != off)
		{
			// 새 지점에서 시작하는 묶음이다(이전 묶음 뒤로 코드가 방출됐다는 뜻).
			_lastStaticFun = _cur;
			_lastStaticCodeOffset = off;
			_staticRunBegin = (int)_staticVars.size() - 1;
		}
	}
	// 리터럴이 만든 상수 슬롯을 되돌릴 수 있는가.
	//   - 아직 마지막 슬롯이고(뒤에 다른 상수가 생기지 않았고)
	//   - 그 슬롯을 만든 함수에서, 만든 뒤로 명령이 하나도 방출되지 않았다.
	// 두 조건이 서면 그 슬롯을 참조하는 코드는 존재할 수 없다.
	bool CanPopLastStatic(int compileIndex) const
	{
		const int idx = StaticPoolIndex(compileIndex);
		if (idx < 0 || idx != (int)_staticVars.size() - 1)
			return false;
		if (idx < _staticRunBegin)
			return false;   // 이 묶음보다 앞선 상수는 참조하는 코드가 있을 수 있다
		if (_lastStaticShared)
			return false;   // 아직 방출되지 않은 다른 피연산자가 같은 상수를 들고 있다
		if (_cur == nullptr || _cur->_code == nullptr || _lastStaticFun != _cur)
			return false;
		return _lastStaticCodeOffset == _cur->_code->GetBufferOffset();
	}
	// 마지막 상수를 되돌린다. 색인도 함께 지워야 같은 값이 다시 올 때 새 슬롯을 만든다.
	void PopLastStatic()
	{
		// 되돌린 뒤 마지막이 되는 상수를 누가 들고 있는지는 알 수 없다. 새 상수가 추가될 때까지 되돌리지 않는다.
		_lastStaticShared = true;
		const BoundaryValue& v = _staticVars.back();
		if (v.GetType() == VAR_FLOAT)
		{
			uint64_t bits = 0;
			std::memcpy(&bits, &v._float, sizeof(bits));
			auto it = _floatConstants.find(bits);
			if (it != _floatConstants.end() && it->second == (int)_staticVars.size() - 1) _floatConstants.erase(it);
		}
		else if (v.GetType() == VAR_INT)
		{
			auto it = _intConstants.find(v._int);
			if (it != _intConstants.end() && it->second == (int)_staticVars.size() - 1) _intConstants.erase(it);
		}
		_staticVars.pop_back();
	}
	bool TryPopStatic(int compileIndex)
	{
		if (CanPopLastStatic(compileIndex) == false)
			return false;
		PopLastStatic();
		return true;
	}
	int NextStaticIndex() const
	{
		CheckCompileRange("constant count", (int64_t)_staticVars.size() + 1, 0,
			(int64_t)INT_MAX - COMPILE_EXTENDED_STATIC_BEGIN + 1);
		return StaticCompileIndex((int)_staticVars.size());
	}
	int	AddStaticInt(NS_INT num)
	{
		auto found = _intConstants.find(num);
		if (found != _intConstants.end())
		{
			if (found->second == (int)_staticVars.size() - 1) _lastStaticShared = true;
			return StaticCompileIndex(found->second);
		}
		const int idx = NextStaticIndex();
		BoundaryValue v;
		v.SetType(VAR_INT);
		v._int = num;

		_staticVars.push_back(v);
		_intConstants.emplace(num, (int)_staticVars.size() - 1);
		MarkLastStaticOrigin();
		return idx;
	}
	bool GetStaticNum(int var, NS_INT* value)
	{
		for (int i = (int)_staticVars.size() - 1; i >= 0; i--)
		{
			if (StaticCompileIndex(i) == var)
			{
				BoundaryValue& v2 = _staticVars[i];
				if (VAR_INT == v2.GetType())
				{
					*value = v2._int;
					return true;
				}
				if (VAR_FLOAT == v2.GetType())
				{
					*value = NeoFloatToInt(v2._float);
					return true;
				}
			}
		}
		return false;
	}

	int	AddStaticNum(double num)
	{
		// NS_FLOAT 로 바꾼 뒤 비트로 찾는다. double 과 float 을 그대로 비교하면 0.001 처럼 정확히
		// 표현되지 않는 값은 절대 같지 않아 리터럴마다 새 상수가 생겼다. 비트 비교라 +0.0/-0.0 은 구분된다.
		static_assert(sizeof(NS_FLOAT) == sizeof(uint64_t), "NS_FLOAT must be 64-bit");
		const NS_FLOAT f = (NS_FLOAT)num;
		uint64_t bits = 0;
		std::memcpy(&bits, &f, sizeof(bits));
		auto found = _floatConstants.find(bits);
		if (found != _floatConstants.end())
		{
			if (found->second == (int)_staticVars.size() - 1) _lastStaticShared = true;
			return StaticCompileIndex(found->second);
		}
		const int idx = NextStaticIndex();
		BoundaryValue v;
		v.SetType(VAR_FLOAT);
		v._float = f;

		_staticVars.push_back(v);
		_floatConstants.emplace(bits, (int)_staticVars.size() - 1);
		MarkLastStaticOrigin();
		return idx;
	}
	int	AddStaticString(const std::string& str)
	{
		CheckCompileRange("string constant bytes", (int64_t)str.size(), 0, SHRT_MAX);
		auto existing = _stringConstants.find(str);
		if (existing != _stringConstants.end()) return existing->second;

		const int idx = NextStaticIndex();
		std::unique_ptr<StringInfo> text(new StringInfo());
		text->_str = str;
		BoundaryValue v;
		v.SetType(VAR_STRING);
		v._str = text.get();

		_staticVars.push_back(v);
		text.release();
		_stringConstants.emplace(str, idx);
		return idx;
	}
	int	AddStaticBool(bool b)
	{
		for (int i = (int)_staticVars.size() - 1; i >= 0; i--)
		{
			BoundaryValue& v2 = _staticVars[i];
			if (VAR_BOOL == v2.GetType())
			{
				if (b == v2._bl)
					return StaticCompileIndex(i);
			}
		}
		const int idx = NextStaticIndex();
		BoundaryValue v;
		v.SetType(VAR_BOOL);
		v._bl = b;

		_staticVars.push_back(v);
		return idx;
	}
	int	AddStaticFunction(int iIndex)
	{
		for (int i = (int)_staticVars.size() - 1; i >= 0; i--)
		{
			BoundaryValue& v2 = _staticVars[i];
			if (VAR_FUN == v2.GetType())
			{
				//if (b == v2._bl)
				//	return StaticCompileIndex(i);
			}
		}
		const int idx = NextStaticIndex();
		BoundaryValue v;
		v.SetType(VAR_FUN);
		v._fun_index = iIndex;

		_staticVars.push_back(v);
		return idx;
	}
};

#define ANSI_RESET_ALL          "\x1b[0m"

#define ANSI_COLOR_BLACK        "\x1b[30m"
#define ANSI_COLOR_RED          "\x1b[31m"
#define ANSI_COLOR_GREEN        "\x1b[32m"
#define ANSI_COLOR_YELLOW       "\x1b[33m"
#define ANSI_COLOR_BLUE         "\x1b[34m"
#define ANSI_COLOR_MAGENTA      "\x1b[35m"
#define ANSI_COLOR_CYAN         "\x1b[36m"
#define ANSI_COLOR_WHITE        "\x1b[37m"

};
