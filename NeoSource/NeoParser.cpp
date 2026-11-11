#include "NeoParser.h"
#include "NeoVMInternal.h"
#include "NeoExport.h"
#include "UTFString.h"
#include "NeoCoreIR.h"
#include "NeoCoreApi.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <limits.h>

namespace NeoScript
{

//#define CSTYLE_FOR

void	SetCompileError(CArchiveRdWC& ar, const char*	lpszString, ...);

#define PARSER_COMPILE_ERROR_LIST(X) \
	X(PCE_INVALID_LOCAL_NAME, "Error (%d, %d): invalid local variable name in function '%s': '%s'") \
	X(PCE_DUPLICATE_LOCAL_NAME, "Error (%d, %d): local variable '%s' is already defined in function '%s'") \
	X(PCE_NOT_FILE_SCOPE, "Error (%d, %d): '%s' is only allowed at file scope") \
	X(PCE_EXPORT_CONST, "Error (%d, %d): const cannot be exported to the host; use 'public const'") \
	X(PCE_DUPLICATE_VISIBILITY, "Error (%d, %d): duplicate '%s'") \
	X(PCE_EXPECTED_DECLARATION, "Error (%d, %d): expected 'var', 'const' or 'fun' after '%s', but found '%s'") \
	X(PCE_IMPORT_FAILED, "Error (%d, %d): failed to import module '%s'") \
	X(PCE_IMPORT_CYCLE, "Error (%d, %d): circular import of module '%s'") \
	X(PCE_MODULE_MEMBER, "Error (%d, %d): module '%s' has no public member '%s' (only public functions, public consts and public vars are accessible)") \
	X(PCE_MODULE_CONST, "Error (%d, %d): module '%s' has no public const '%s'") \
	X(PCE_DUPLICATE_FUNCTION_ARGUMENT, "Error (%d, %d): duplicate function argument '%s'") \
	X(PCE_DUPLICATE_FUNCTION, "Error (%d, %d): duplicate function definition '%s'") \
	X(PCE_FUNCTION_PROTOTYPE, "Error (%d, %d): function forward declarations are no longer supported; remove this declaration and keep the function body") \
	X(PCE_EXPECTED_FUNCTION_ARGUMENT, "Error (%d, %d): expected a function argument name, but found '%s'") \
	X(PCE_EXPECTED_COMMA_BETWEEN_ARGUMENTS, "Error (%d, %d): expected ',' between function arguments") \
	X(PCE_INVALID_CALL_ARGUMENT, "Error (%d, %d): invalid function call argument") \
	X(PCE_ARGUMENT_COUNT_MISMATCH, "Error (%d, %d): function argument count mismatch. Expected %d, got %d") \
	X(PCE_EXPECTED_LEFT_PAREN, "Error (%d, %d): expected '('") \
	X(PCE_EXPECTED_RIGHT_PAREN, "Error (%d, %d): expected ')'") \
	X(PCE_EXPECTED_RIGHT_BRACKET, "Error (%d, %d): expected ']'") \
	X(PCE_EXPECTED_EXPRESSION, "Error (%d, %d): expected an expression") \
	X(PCE_INVALID_NUMBER_LITERAL, "Error (%d, %d): invalid numeric literal") \
	X(PCE_UNKNOWN_IDENTIFIER, "Error (%d, %d): unknown identifier '%s'") \
	X(PCE_UNTERMINATED_STRING, "Error (%d, %d): unterminated string literal") \
	X(PCE_EXPECTED_MEMBER_NAME, "Error (%d, %d): expected a member name after '.'") \
	X(PCE_GLOBAL_CALL_NOT_ALLOWED, "Error (%d, %d): function calls are not allowed in global variable initializers") \
	X(PCE_INVALID_FUNCTION_CALL, "Error (%d, %d): invalid function call") \
	X(PCE_EXPECTED_LOGIC_NOT_OPERAND, "Error (%d, %d): expected an expression after '!'") \
	X(PCE_EXPECTED_TYPE_LEFT_PAREN, "Error (%d, %d): expected '(' after type conversion") \
	X(PCE_EXPECTED_TYPE_RIGHT_PAREN, "Error (%d, %d): expected ')' after type conversion") \
	X(PCE_UNEXPECTED_FUNCTION_END, "Error (%d, %d): unexpected end of function '%s'") \
	X(PCE_UNEXPECTED_GLOBAL_BLOCK_END, "Error (%d, %d): unexpected '}' in global initialization") \
	X(PCE_INVALID_RETURN_STATEMENT, "Error (%d, %d): invalid return statement") \
	X(PCE_INVALID_ASSIGNMENT, "Error (%d, %d): expected an assignment value after '='") \
	X(PCE_VAR_DECLARATION_NOT_ALLOWED, "Error (%d, %d): variable declarations are not allowed here") \
	X(PCE_BREAK_OUTSIDE_LOOP, "Error (%d, %d): 'break' can only be used inside a loop") \
	X(PCE_CONTINUE_OUTSIDE_LOOP, "Error (%d, %d): 'continue' can only be used inside a loop") \
	X(PCE_EXPECTED_BREAK_SEMICOLON, "Error (%d, %d): expected ';' after 'break'") \
	X(PCE_EXPECTED_CONTINUE_SEMICOLON, "Error (%d, %d): expected ';' after 'continue'") \
	X(PCE_TEMP_VAR_UNSUPPORTED, "Error (%d, %d): temporary variables do not support '%s'") \
	X(PCE_TABLE_VAR_UNSUPPORTED, "Error (%d, %d): table values do not support '%s'") \
	X(PCE_INVALID_INCREMENT_TARGET, "Error (%d, %d): invalid target for '++' or '--': '%s'") \
	X(PCE_INVALID_OPERATOR, "Error (%d, %d): invalid operator near '%s'") \
	X(PCE_SYNTAX_ERROR, "Error (%d, %d): syntax error near '%s'") \
	X(PCE_EXPECTED_LVALUE, "Error (%d, %d): assignment target must be writable") \
	X(PCE_LOGIC_NOT_ALLOWED_GLOBAL, "Error (%d, %d): '%s' is not allowed in global initialization") \
	X(PCE_EXPECTED_TOKEN, "Error (%d, %d): expected %s, but found '%s'") \
	X(PCE_CODE_BLOCK_TOO_LARGE, "Error (%d, %d): generated %s code is too large (%d bytes)") \
	X(PCE_INVALID_FOR_INIT, "Error (%d, %d): invalid for-loop initializer") \
	X(PCE_INVALID_FOR_CONDITION, "Error (%d, %d): invalid for-loop condition") \
	X(PCE_INVALID_FOR_INCREMENT, "Error (%d, %d): invalid for-loop increment expression") \
	X(PCE_INVALID_LOOP_VARIABLE_LAYOUT, "Error (%d, %d): internal loop variable layout error") \
	X(PCE_INVALID_STATEMENT_END, "Error (%d, %d): expected ';' at the end of the statement") \
	X(PCE_INVALID_FUNCTION_NAME, "Error (%d, %d): invalid function name '%s'") \
	X(PCE_UNEXPECTED_TOKEN, "Error (%d, %d): unexpected token '%s' in function '%s'") \
	X(PCE_CONST_DUPLICATE, "Error (%d, %d): const name '%s' is already defined (const, define, variable or function)") \
	X(PCE_CONST_INVALID_VALUE, "Error (%d, %d): const expression must be a compile-time constant, near '%s'") \
	X(PCE_CONST_INVALID_OP, "Error (%d, %d): invalid operation in const expression near '%s'") \
	X(PCE_CONST_DIV_ZERO, "Error (%d, %d): division by zero in const expression") \
	X(PCE_CONST_DIV_OVERFLOW, "Error (%d, %d): integer division overflow in const expression") \
	X(PCE_CONST_SHIFT_RANGE, "Error (%d, %d): shift count must be between 0 and 63 in const expression") \
	X(PCE_CASE_OUTSIDE_SWITCH, "Error (%d, %d): 'case' / 'default' can only be used inside a switch") \
	X(PCE_SWITCH_DUPLICATE_CASE, "Error (%d, %d): duplicate case value in switch") \
	X(PCE_SWITCH_DUPLICATE_DEFAULT, "Error (%d, %d): 'default' appears more than once in switch") \
	X(PCE_SWITCH_INVALID_CASE_VALUE, "Error (%d, %d): case value must be a compile-time constant of type bool, int or string") \
	X(PCE_SWITCH_FLOAT_CASE, "Error (%d, %d): float is not allowed as a case value (exact comparison is unreliable). use int or string") \
	X(PCE_SWITCH_TOO_MANY, "Error (%d, %d): switch table count limit exceeded (maximum 65536)") \
	X(PCE_VM_NOT_INITIALIZED, "Please call NeoScript::NeoVMSystem::Initialize() before compiling scripts")

enum EParserCompileError
{
#define X(name, message) name,
	PARSER_COMPILE_ERROR_LIST(X)
#undef X
	PCE_COUNT
};

static const char* g_sParserCompileErrors[PCE_COUNT] =
{
#define X(name, message) message,
	PARSER_COMPILE_ERROR_LIST(X)
#undef X
};

static_assert(sizeof(g_sParserCompileErrors) / sizeof(g_sParserCompileErrors[0]) == PCE_COUNT, "Parser compile error table mismatch");

template<typename... Args>
static void SetParserCompileError(CArchiveRdWC& ar, EParserCompileError error, Args... args)
{
	SetCompileError(ar, g_sParserCompileErrors[error], ar.CurLine(), ar.CurCol(), args...);
}

static void SetParserCompileError(CArchiveRdWC& ar, EParserCompileError error)
{
	SetCompileError(ar, g_sParserCompileErrors[error], ar.CurLine(), ar.CurCol());
}

#if 0
inline bool	IsShort(int v) { return (-32768 <= v && v <= 32767) ? true : false; }
#endif


template<class T>
static void RemoveAt(std::vector<T>& array, int index)
{
	int cur = 0;
	for (auto it = array.begin(); it != array.end(); it++, cur++)
	{
		if (cur == index)
		{
			array.erase(it);
			break;
		}
	}
}

enum OperandType
{
	Data_None,
	Data_Fun,
	Data_NR,	// None + [ref]
	Data_NS,	// None + Short
	Data_TR,	// Table + [ref]
	Data_TS,	// Table + Short
	Increment_Prefix,
	Increment_Postfix,
};

// 리터럴이 어떤 즉값 명령으로 나갈 수 있는지. Literal_None 이면 상수 풀 경유(기존 MOV).
enum LiteralKind
{
	Literal_None = 0,
	Literal_Int,
	Literal_Float,
};



struct SOperand
{
	SlotType _storage;
	int _iVar;
	int _iArrayIndex;
	OperandType _operandType;
	// 숫자 리터럴이면 그 값을 그대로 들고 있는다. 대입/인자 전달처럼 값만 옮기는
	// 자리에서는 상수 풀을 거치지 않고 MOVI/MOVF 로 바로 실을 수 있다.
	LiteralKind _literalKind;
	NS_FLOAT _floatLiteral;
	NS_INT _intLiteral;
	bool _readOnly = false;
	// 이 피연산자를 보존한 MOV만 추적한다. 다른 값을 만드는 임시는 되돌리지 않는다.
	int _snapshotOffset = -1;
    // A completed indexed assignment is still an lvalue, but its next value
    // consumer uses the captured component rather than rereading the owner.
    bool _assignedResult = false;
    // A selector path is resolved by its consuming read/write. Plain variable
    // roots and keys retain their slots; computed expressions retain their values.
    int _pathRoot = INVALID_ERROR_PARSEJOB;
    std::vector<int> _pathKeys;
    std::vector<int> _pathOwners;
    bool _pathResolved = false;

	// Compile-time address of the immediate container, e.g. (list, i) for
	// list[i][component]. Keep the address until a consumer chooses read/write.
	// Parentheses never decide that choice, and no future tokens are inspected.
	struct ContainerAccess
	{
		int owner = INVALID_ERROR_PARSEJOB;
		int key = INVALID_ERROR_PARSEJOB;
		bool loaded = false;
	} _container;

	SOperand()
	{
		Reset();
	}
	SOperand(int iVar, int iArrayIndex = INVALID_ERROR_PARSEJOB, OperandType operandType = Data_NR)
	{
		_iVar = iVar;
		_iArrayIndex = iArrayIndex;
		_literalKind = Literal_None;
		_floatLiteral = 0;
		_intLiteral = 0;
		if(iVar == INVALID_ERROR_PARSEJOB && iArrayIndex == INVALID_ERROR_PARSEJOB)
			_operandType = Data_None;
		else
			_operandType = operandType;
	}
	SOperand(int iVar, const SlotType& storage) : SOperand(iVar) { _storage = storage; }
	void Reset()
	{
		_storage = SlotType();
		_readOnly = false;
		_snapshotOffset = -1;
        _assignedResult = false;
        _pathRoot = INVALID_ERROR_PARSEJOB;
        _pathKeys.clear(); _pathOwners.clear(); _pathResolved = false;
		_container = ContainerAccess();
		_iVar = _iArrayIndex = INVALID_ERROR_PARSEJOB;
		_operandType = Data_NR;
		_literalKind = Literal_None;
		_floatLiteral = 0;
		_intLiteral = 0;
	}
	void SetIntLiteral(int var, NS_INT value)
	{
		_storage.typeId = static_cast<u32>(NeoCore::TypeKind::Int);
		_storage.kind = SlotKind::Int;
		_iVar = var;
		_iArrayIndex = INVALID_ERROR_PARSEJOB;
		_operandType = Data_NR;
		_literalKind = Literal_Int;
		_intLiteral = value;
		_floatLiteral = 0;
	}
	void SetFloatLiteral(int var, NS_FLOAT value)
	{
		_storage.typeId = static_cast<u32>(NeoCore::TypeKind::Float);
		_storage.kind = SlotKind::Float;
		_iVar = var;
		_iArrayIndex = INVALID_ERROR_PARSEJOB;
		_operandType = Data_NR;
		_literalKind = Literal_Float;
		_floatLiteral = value;
		_intLiteral = 0;
	}

	inline bool IsInvalidValue() { return (false == IsHaveShort()) && (_iVar == INVALID_ERROR_PARSEJOB); }

	inline bool IsArray() const { return _iArrayIndex != INVALID_ERROR_PARSEJOB; }
	inline bool IsShort() { return _operandType == Data_NS; }
	inline bool IsConst() { return _operandType == Data_NS; }
	inline bool IsNone() { return _operandType == Data_None; }
	inline bool IsHaveShort() { return (_operandType == Data_NS || _operandType == Data_TS); }
	inline bool IsFun() { return _operandType == Data_Fun; }
	inline bool IsFloatLiteral() const { return _literalKind == Literal_Float; }
	inline bool IsIntLiteral() const { return _literalKind == Literal_Int; }
};

// Resolve the container at the consuming operation. Assignment calls this
// after its RHS, just as a single-level indexed assignment does. R-value
// boundaries resolve it before later arguments/operands can have side effects.
static void PrepareContainerPath(SOperand& operand, CArchiveRdWC& ar, SFunctions& funs)
{
    if (operand._assignedResult || operand._pathResolved || operand._pathKeys.empty()) return;
    int owner = operand._pathRoot;
    for (size_t i = 0; i + 2 < operand._pathKeys.size(); ++i)
    {
        const int next = operand._pathOwners[i];
        funs._cur->Push_TableRead(ar, owner, operand._pathKeys[i], next, false);
        owner = next;
    }
    operand._container = SOperand::ContainerAccess();
    if (operand._pathKeys.size() > 1)
    {
        operand._container.owner = owner;
        operand._container.key = operand._pathKeys[operand._pathKeys.size() - 2];
        operand._iVar = operand._pathOwners.back();
    }
    else operand._iVar = owner;
    operand._iArrayIndex = operand._pathKeys.back();
    operand._pathResolved = true;
}

static void ResolveContainer(SOperand& operand, CArchiveRdWC& ar, SFunctions& funs)
{
    if (operand._assignedResult) return;
    PrepareContainerPath(operand, ar, funs);
	auto& access = operand._container;
	if (access.owner != INVALID_ERROR_PARSEJOB && !access.loaded)
	{
		funs._cur->Push_TableRead(ar, access.owner, access.key, operand._iVar, false);
		access.loaded = true;
	}
}

static void ReadElement(SOperand& operand, int dest, CArchiveRdWC& ar, SFunctions& funs)
{
    if (!operand.IsArray())
    {
        funs._cur->Push_OP2(ar, NOP_MOV, dest, operand._iVar, false);
        return;
    }
    ResolveContainer(operand, ar, funs);
    funs._cur->Push_TableRead(ar, operand._iVar, operand._iArrayIndex, dest,
        operand.IsHaveShort(), NOP_CLT_READ);
}


static OperationCode RejectLegacyArithmetic(CArchiveRdWC& ar)
{
    SetCompileError(ar, "numeric expressions require typed IR");
    return NOP_ERROR;
}



static void WriteElement(SOperand& operand, OperationCode op, int value, bool immediate,
	CArchiveRdWC& ar, SFunctions& funs)
{
    PrepareContainerPath(operand, ar, funs);
	const auto access = operand._container;
    if (access.owner != INVALID_ERROR_PARSEJOB && op == NOP_MOV)
    {
        const int temp = operand._iVar;
        funs._cur->Push_TableWriteRef(ar, access.owner, access.key, temp,
            operand._iArrayIndex, value, access.loaded);
        operand._assignedResult = true;
        operand._container.loaded = false;
        return;
    }
    if (operand._assignedResult) {
        operand._assignedResult = false;
        operand._container.loaded = false;
    }
	ResolveContainer(operand, ar, funs);
	funs._cur->Push_Table_MASMDP(ar, op, operand._iVar, operand._iArrayIndex,
		value, false, operand.IsHaveShort(), immediate);
}

// 컴파일 전용 함수 ID와 컨테이너 선택자를 런타임 값 슬롯으로 확정한다.
// 함수 ID를 일반 op에 그대로 넘기면 같은 번호의 변수 슬롯을 읽게 된다.
// 값을 읽는 경계에서만 사용한다. 직접 CALL과 대입/인자의 FMOV 목적 슬롯은 유지한다.
static void MaterializeValue(SOperand& operand, CArchiveRdWC& ar, SFunctions& funs)
{
	if (operand.IsFun())
	{
		const int value = funs._cur->AllocLocalTempVar(operand._storage);
		funs._cur->Push_OP2(ar, NOP_FMOV1, value, operand._iVar, false);
		operand = SOperand(value, operand._storage);
	}
	else if (operand.IsArray())
	{
		const int value = funs._cur->AllocLocalTempVar(operand._storage);
		ReadElement(operand, value, ar, funs);
		operand = SOperand(value, operand._storage);
	}
}

// 숫자 리터럴을 값만 옮기는 자리(대입/인자 전달)에서는 상수 풀을 거치지 않고
// 즉값 명령으로 낸다. int32 범위는 NOP_MOVI, float32로 정확히 표현되는 실수는 NOP_MOVF.
//
// ParseNum 이 만들어 둔 상수 풀 항목은, 아무도 참조하지 않는 것이 확실할 때만 되돌린다.
// 판정은 CanPopLastStatic 이 O(1) 로 한다 — 명령을 내면 코드 오프셋이 달라지므로
// 반드시 방출 '전에' 물어봐야 한다.
static void SnapshotValue(SOperand& operand, CArchiveRdWC& ar, SFunctions& funs)
{
    MaterializeValue(operand, ar, funs);
    if (!operand.IsInvalidValue() && !IsTempVar(operand._iVar) && !IsCompileConstant(operand._iVar))
    {
        const int value = funs._cur->AllocLocalTempVar(operand._storage);
        funs._cur->Push_OP2(ar, NOP_MOV, value, operand._iVar, operand.IsShort());
        operand = SOperand(value, operand._storage);
		operand._snapshotOffset = funs._cur->_iLastOPOffset;
    }
}

// 보존 이후 실행할 명령이 하나도 없다면 원본을 지금 읽어도 같은 값이다.
// 모듈의 public 변수 선택자는 호스트 getter와 달리 코드를 내지 않으므로 이 경우에 해당한다.
// 분기 합류점이나 디버그 빌드의 중단 위치는 그대로 둔다.
static void ElideImmediateSnapshot(SOperand& operand, CArchiveRdWC& ar, SFunctions& funs)
{
	SFunctionInfo& fun = *funs._cur;
	const int offset = operand._snapshotOffset;
	const int end = fun._code->GetBufferOffset();
	if (ar._debug || offset < 0 || offset != fun._iLastOPOffset ||
		offset + (int)sizeof(SVMOperation) != end || fun.GetLastOP() != NOP_MOV ||
		fun._jumpTargetOffsets.count(end) != 0)
		return;
	const SVMOperation* move = fun.GetOPPointer(offset);
	if (move->n1 != operand._iVar || move->argFlag != 0)
		return;
	const int source = move->n2;
	fun._code->SetBufferOffset(offset);
	fun.ClearLastOP();
	operand = SOperand(source);
}

// 연결 명령이 바로 소비하는 문자열 변환은 연결 안에서 수행한다. 중간에 명령이나 분기 합류점이
// 없을 때만 적용하므로 인자의 값·변환 시점이 같고, 디버그의 중단 위치도 그대로 남긴다.
static void ElideConcatConversion(SOperand& left, SOperand& right, CArchiveRdWC& ar, SFunctions& funs)
{
    SFunctionInfo& fun = *funs._cur;
    const int offset = fun._iLastOPOffset;
    const int end = fun._code->GetBufferOffset();
    if (ar._debug || offset < 0 || offset + static_cast<int>(sizeof(SVMOperation)) != end ||
        fun.GetLastOP() != NOP_TOSTRING || fun._jumpTargetOffsets.count(end) != 0)
        return;
    const SVMOperation conversion = *fun.GetOPPointer(offset);
    if (conversion.argFlag != 0 || !IsTempVar(conversion.n1)) return;
    const bool replaceLeft = !left.IsArray() && !left.IsFun() && left._iVar == conversion.n1;
    const bool replaceRight = !right.IsArray() && !right.IsFun() && right._iVar == conversion.n1;
    if (!replaceLeft && !replaceRight) return;
    fun._code->SetBufferOffset(offset);
    fun.ClearLastOP();
    if (replaceLeft) left = SOperand(conversion.n2);
    if (replaceRight) right = SOperand(conversion.n2);
}

// 바로 앞의 스크립트 호출 결과를 단순 변수에 직접 쓴다. 인자 평가는 이미 끝났으며,
// 임시 결과를 공유하는 MOV와 불필요한 참조 수 변경을 없앤다.
// 호출 인자 슬롯과 컨테이너/속성 대입은 제외하고, 분기 합류점도 넘지 않는다.
static bool TryStoreCallResult(CArchiveRdWC& ar, SFunctions& funs, int dest, SOperand& source)
{
    if (source.IsArray() || source.IsFun() || !IsTempVar(source._iVar) ||
        !((dest > 0 && dest < COMPILE_LOCALTMP_VAR_BEGIN) || dest <= COMPILE_GLOBAL_VAR_BEGIN))
        return false;
    SFunctionInfo& fun = *funs._cur;
    const int end = fun._code->GetBufferOffset();
    const OperationCode kind = fun.GetLastOP();
    if (kind != NOP_CALL || ar._debug ||
        fun._iLastOPOffset + (int)sizeof(SVMOperation) != end ||
        fun._jumpTargetOffsets.count(end) != 0)
        return false;
    SVMOperation* call = fun.GetOPPointer(fun._iLastOPOffset);
    if (call->n3 != source._iVar || (call->argFlag & NEOS_OP_CALL_NORESULT))
        return false;
    // RET가 실제로 실행될 때만 대입한다. 오류·일시 정지 중에는 목적지 값을 바꾸지 않는다.
    call->n3 = CompileShort("call destination", dest);
    return true;
}

static bool TryPushImmediate(CArchiveRdWC& ar, SFunctions& funs, int dest, const SOperand& source, int iDebugLine = -1)
{
	if (source.IsArray())
		return false;
	if (source.IsIntLiteral() == false && source.IsFloatLiteral() == false)
		return false;

	const bool canReclaim = funs.CanPopLastStatic(source._iVar);

	if (source.IsIntLiteral())
	{
		if (source._intLiteral < INT32_MIN || source._intLiteral > INT32_MAX) return false;
		funs._cur->Push_MOVI(ar, dest, (int)source._intLiteral, iDebugLine);
	}
	else if (funs._cur->Push_MOVF(ar, dest, source._floatLiteral, iDebugLine) == false)
		return false;   // float32로 정확히 표현할 수 없는 double은 상수 풀에 남긴다.

	if (canReclaim)
	{
		funs.PopLastStatic();
		// 되돌린 상수를 호출 계획이 인자 원본으로 가리키면 그 색인은 다음 상수를 가리키게 된다.
		// 원본을 지우면 저장소 추론은 피호출자 서명으로 종류를 정한다. PREPARE_CALL을 내지 않는 호출은
		// 상수 뒤에 명령이 없어 되돌리기가 실제로 일어난다.
		if (dest >= COMPILE_CALLARG_VAR_BEGIN && !funs._cur->_calls.empty())
			for (int& slot : funs._cur->_calls.back().sources)
				if (slot == source._iVar) slot = -1;
	}
	return true;
}

// 상수 계산은 런타임 명령 번호에 의존하지 않는다.
enum class ScalarBinaryOp
{
    Invalid, Add, Subtract, Multiply, Divide, Remainder,
    ShiftLeft, ShiftRight, BitXor, BitAnd, BitOr
};

static ScalarBinaryOp TokenBinaryOp(TK_TYPE token)
{
    if (token == TK_PLUS) return ScalarBinaryOp::Add;
    if (token == TK_MINUS) return ScalarBinaryOp::Subtract;
    if (token == TK_MUL) return ScalarBinaryOp::Multiply;
    if (token == TK_DIV) return ScalarBinaryOp::Divide;
    if (token == TK_PERCENT) return ScalarBinaryOp::Remainder;
    if (token == TK_LSHIFT) return ScalarBinaryOp::ShiftLeft;
    if (token == TK_RSHIFT) return ScalarBinaryOp::ShiftRight;
    if (token == TK_XOR) return ScalarBinaryOp::BitXor;
    if (token == TK_AND) return ScalarBinaryOp::BitAnd;
    if (token == TK_OR) return ScalarBinaryOp::BitOr;
    return ScalarBinaryOp::Invalid;
}

static ScalarBinaryOp TypedBinaryOp(OperationCode op)
{
    switch (op)
    {
    case OP_I64_ADD: return ScalarBinaryOp::Add;
    case OP_F64_ADD: return ScalarBinaryOp::Add;
    case OP_I64_SUB: return ScalarBinaryOp::Subtract;
    case OP_F64_SUB: return ScalarBinaryOp::Subtract;
    case OP_I64_MUL: return ScalarBinaryOp::Multiply;
    case OP_F64_MUL: return ScalarBinaryOp::Multiply;
    case OP_I64_DIV: return ScalarBinaryOp::Divide;
    case OP_F64_DIV: return ScalarBinaryOp::Divide;
    case OP_I64_MOD: return ScalarBinaryOp::Remainder;
    case OP_I64_AND: return ScalarBinaryOp::BitAnd;
    case OP_I64_OR: return ScalarBinaryOp::BitOr;
    case OP_I64_XOR: return ScalarBinaryOp::BitXor;
    case OP_I64_SHL: return ScalarBinaryOp::ShiftLeft;
    case OP_I64_SHR: return ScalarBinaryOp::ShiftRight;
    default: return ScalarBinaryOp::Invalid;
    }
}

// 양쪽 숫자 리터럴을 VM과 같은 정수/NS_FLOAT 규칙으로 접는다.
// 0 나누기와 오버플로는 접지 않아 원래 위치의 런타임 오류를 유지한다.
static bool TryFoldLiteralBinOp(SOperand& a, ScalarBinaryOp op, const SOperand& b, SFunctions& funs)
{
	if (a.IsArray() || b.IsArray())
		return false;

	const bool aInt = a.IsIntLiteral(), aFloat = a.IsFloatLiteral();
	const bool bInt = b.IsIntLiteral(), bFloat = b.IsFloatLiteral();
	if ((aInt == false && aFloat == false) || (bInt == false && bFloat == false))
		return false;

	const bool bothInt = (aInt && bInt);
	bool resultIsInt = bothInt;
	NS_INT   ri = 0;
	NS_FLOAT rf = 0;

	// int op float 도 VM 과 같이 float 산술로 맞춘다.
	const NS_FLOAT fx = aInt ? (NS_FLOAT)a._intLiteral : a._floatLiteral;
	const NS_FLOAT fy = bInt ? (NS_FLOAT)b._intLiteral : b._floatLiteral;

	switch (op)
	{
	case ScalarBinaryOp::Add:
		if (bothInt) ri = NeoIntAdd(a._intLiteral, b._intLiteral); else rf = fx + fy;
		break;
	case ScalarBinaryOp::Subtract:
		if (bothInt) ri = NeoIntSub(a._intLiteral, b._intLiteral); else rf = fx - fy;
		break;
	case ScalarBinaryOp::Multiply:
		if (bothInt) ri = NeoIntMul(a._intLiteral, b._intLiteral); else rf = fx * fy;
		break;
	case ScalarBinaryOp::Divide:
		if (bothInt)
		{
			if (b._intLiteral == 0 || (a._intLiteral == NS_INT_MIN && b._intLiteral == -1))
				return false;   // 런타임 예외로 남긴다
			ri = a._intLiteral / b._intLiteral;
		}
		else rf = fx / fy;
		break;
	// 아래는 VM 도 int/int 조합에서만 계산한다. 나머지 조합은 접지 않고 넘겨서
	// 기존과 같은 타입 에러가 런타임에 나게 둔다.
	case ScalarBinaryOp::Remainder:
		if (bothInt == false || b._intLiteral == 0 ||
			(a._intLiteral == NS_INT_MIN && b._intLiteral == -1))
			return false;
		ri = a._intLiteral % b._intLiteral;
		break;
	case ScalarBinaryOp::ShiftLeft:
		if (bothInt == false || static_cast<NS_UINT>(b._intLiteral) >= 64) return false;
		ri = NeoShiftLeft(a._intLiteral, b._intLiteral);
		break;
	case ScalarBinaryOp::ShiftRight:
		if (bothInt == false || static_cast<NS_UINT>(b._intLiteral) >= 64) return false;
		ri = NeoShiftRight(a._intLiteral, b._intLiteral);
		break;
	case ScalarBinaryOp::BitXor:
		if (bothInt == false) return false;
		ri = a._intLiteral ^ b._intLiteral;
		break;
	case ScalarBinaryOp::BitAnd:
		if (bothInt == false) return false;
		ri = a._intLiteral & b._intLiteral;
		break;
	case ScalarBinaryOp::BitOr:
		if (bothInt == false) return false;
		ri = a._intLiteral | b._intLiteral;
		break;
	default:
		return false;
	}

	// [주의] 여기서 피연산자의 상수 슬롯을 회수하면 안 된다.
	// 같은 리터럴 값이 식 안에 여러 번 나오면 AddStaticInt/AddStaticNum 의 중복 제거로
	// 여러 SOperand 가 같은 슬롯을 가리킨다. 접기가 그 슬롯을 pop 하고 결과값을 같은
	// 자리에 새로 만들면, 아직 처리되지 않은 다른 SOperand 의 _iVar 가 엉뚱한 값을
	// (또는 인덱스가 밀려 전역변수를) 가리키게 된다.
	//   예) 7 == 7 + 1  ->  EQ [8] == [8]  (7 이 8 로 뒤바뀜)
	// 접히지 않는 연산자(비교/논리/문자열연결)는 _iVar 를 그대로 쓰므로 실제로 터진다.
	// 미참조 슬롯이 남는 비용은 값 종류당 최대 하나라 감수한다.

	if (resultIsInt)
		a.SetIntLiteral(funs.AddStaticInt(ri), ri);
	else
		a.SetFloatLiteral(funs.AddStaticNum(rf), rf);
	return true;
}

struct STokenValue
{
	std::string _str;
	char		_iPriority; // lower value is higher priority
	OperationCode	_op;

	STokenValue() {}
	STokenValue(const char* p, char iPriority, OperationCode	op)
	{
		_str = p;
		_iPriority = iPriority;
		_op = op;
	}
};

std::vector<STokenValue> g_sTokenToString;
std::map<std::string, TK_TYPE> g_sStringToToken;

#define TOKEN_STR1(key, str) g_sTokenToString[key] = STokenValue(str, 20, NOP_NONE)
#define TOKEN_STR2(key, str) g_sTokenToString[key] = STokenValue(str, 20, NOP_NONE); g_sStringToToken[str] = key

#define TOKEN_STR3(key, str, pri, op) g_sTokenToString[key] = STokenValue(str, pri, op); g_sStringToToken[str] = key

TK_TYPE ParseJob(bool bReqReturn, SOperand& sResultStack, std::vector<SJumpValue>* pJumps, CArchiveRdWC& ar, SFunctions& funs, SVars& vars, bool bAllowVarDef = false, TK_TYPE tkEnd1 = TK_SEMICOLON, TK_TYPE tkEnd2 = TK_COMMA, TK_TYPE tkEnd3 = TK_R_SMALL, TK_TYPE tkEnd4 = TK_R_ARRAY, std::vector<SJumpValue>* pContinueJumps = NULL, TK_TYPE tkEnd5 = TK_UNUSED, TK_TYPE tkEnd6 = TK_UNUSED);
// ParseJob 과 동일한 계약(종결 토큰 반환, 에러 시 TK_NONE)의 단락 평가 래퍼.
// 값을 소비하는 모든 식 진입점은 ParseJob 대신 이 함수를 거쳐야 한다.
// pOutFalseJumps != NULL 이면 "분기 컨텍스트" 모드로 동작한다(if 등 조건식 전용).
// 이때 &&/|| 의 결과 불린을 만들지 않고(구: MOV true/false + JMP + 재검사 JF),
// 조건이 거짓일 때 뛸 점프 목록만 돌려준다 → 호출자가 자기 위치로 패치한다.
// 논리 연산자가 없으면 목록은 비어 있고 기존 경로(값 반환)와 동일하다.
TK_TYPE ParseShortCircuitLogic(bool bReqReturn, SOperand& sResultStack, CArchiveRdWC& ar, SFunctions& funs, SVars& vars, TK_TYPE tkEnd1 = TK_SEMICOLON, TK_TYPE tkEnd2 = TK_COMMA, TK_TYPE tkEnd3 = TK_R_SMALL, TK_TYPE tkEnd4 = TK_R_ARRAY, std::vector<SJumpValue>* pOutFalseJumps = NULL);
bool ParseVarDef(CArchiveRdWC& ar, SFunctions& funs, SVars& vars, bool blExport, bool blPublic);
// bStopAtCase: switch 본문 파싱용. case/default/'}' 를 만나면 소비하지 않고(PushToken) 종료한다.
static bool ParseSingleStatement(std::vector<SJumpValue>* breaks, CArchiveRdWC& ar,
    SFunctions& funs, SVars& vars, bool* returns, std::vector<SJumpValue>* continues);
bool ParseMiddleArea(std::vector<SJumpValue>* pJumps, CArchiveRdWC& ar, SFunctions& funs, SVars& vars, bool* alwaysReturns = NULL, std::vector<SJumpValue>* pContinueJumps = NULL, bool bStopAtCase = false, bool allowEof = false, bool singleStatement = false);
bool ParseSwitch(CArchiveRdWC& ar, SFunctions& funs, SVars& vars, bool* alwaysReturns, std::vector<SJumpValue>* pContinueJumps);
bool ParseFunctionBody(CArchiveRdWC& ar, SFunctions& funs, SVars& vars, bool addOPFunEnd = true);

void InitDefaultData()
{
	g_sTokenToString.clear();
	g_sStringToToken.clear();
}
int InitDefaultTokenString()
{
	InitDefaultData();

	g_sTokenToString.resize(TK_MAX);

	TOKEN_STR1(TK_UNUSED, "unused token");

	TOKEN_STR1(TK_NONE, "none token");
	TOKEN_STR1(TK_STRING, "string token");

	TOKEN_STR2(TK_VAR, "var");
	TOKEN_STR2(TK_CONST, "const");
	TOKEN_STR2(TK_FUN, "fun");
	TOKEN_STR2(TK_IMPORT, "import");
	TOKEN_STR2(TK_EXPORT, "export");
	TOKEN_STR2(TK_PUBLIC, "public");

	TOKEN_STR3(TK_TOSTRING, "tostring", 20, NOP_TOSTRING);
	TOKEN_STR3(TK_TOINT, "toint", 20, NOP_TOINT);
	TOKEN_STR3(TK_TOFLOAT, "tofloat", 20, NOP_TOFLOAT);
	TOKEN_STR3(TK_SLEEP, "sleep", 20, NOP_SLEEP);

	TOKEN_STR2(TK_RETURN, "return");
	TOKEN_STR2(TK_BREAK, "break");
	TOKEN_STR2(TK_CONTINUE, "continue");
	TOKEN_STR2(TK_IF, "if");
	TOKEN_STR2(TK_ELSE, "else");
	TOKEN_STR2(TK_FOR, "for");
	TOKEN_STR2(TK_FOREACH, "foreach");
	TOKEN_STR2(TK_WHILE, "while");
	TOKEN_STR2(TK_SWITCH, "switch");
	TOKEN_STR2(TK_CASE, "case");
	TOKEN_STR2(TK_DEFAULT, "default");
	TOKEN_STR2(TK_TRUE, "true");
	TOKEN_STR2(TK_FALSE, "false");
	TOKEN_STR2(TK_NULL, "null");

	TOKEN_STR3(TK_PLUS2, "++", 1, NOP_NONE);
	TOKEN_STR3(TK_MINUS2, "--", 1, NOP_NONE);

	TOKEN_STR3(TK_PLUS, "+", 5, NOP_NONE);
	TOKEN_STR3(TK_PLUS_EQ, "+=", 15, NOP_NONE);
	TOKEN_STR3(TK_MINUS, "-", 5, NOP_NONE);
	TOKEN_STR3(TK_MINUS_EQ, "-=", 15, NOP_NONE);
	TOKEN_STR3(TK_MUL, "*", 4, NOP_NONE);
	TOKEN_STR3(TK_MUL_EQ, "*=", 15, NOP_NONE);
	TOKEN_STR3(TK_DIV, "/", 4, NOP_NONE);
	TOKEN_STR3(TK_DIV_EQ, "/=", 15, NOP_NONE);
	TOKEN_STR3(TK_PERCENT, "%", 4, NOP_NONE);
	TOKEN_STR3(TK_PERCENT_EQ, "%=", 15, NOP_NONE);

	TOKEN_STR3(TK_LSHIFT, "<<", 6, NOP_NONE);
	TOKEN_STR3(TK_LSHIFT_EQ, "<<=", 15, NOP_NONE);
	TOKEN_STR3(TK_RSHIFT, ">>", 6, NOP_NONE);
	TOKEN_STR3(TK_RSHIFT_EQ, ">>=", 15, NOP_NONE);

	TOKEN_STR2(TK_NOT, "~");
	TOKEN_STR3(TK_XOR, "^", 10, NOP_NONE); // C 우선순위: & (9) > ^ (10) > | (11)
	TOKEN_STR3(TK_XOR_EQ, "^=", 15, NOP_NONE);
	TOKEN_STR3(TK_EQUAL, "=", 15, NOP_MOV);
	TOKEN_STR3(TK_EQUAL_EQ, "==", 8, OP_EQUAL2);
	TOKEN_STR3(TK_EQUAL_NOT, "!=", 8, OP_NEQUAL);

	TOKEN_STR3(TK_AND, "&", 9, NOP_NONE);
	TOKEN_STR3(TK_AND_EQ, "&=", 15, NOP_NONE);
	TOKEN_STR3(TK_AND2, "&&", 12, NOP_NONE);
	TOKEN_STR3(TK_OR, "|", 11, NOP_NONE);
	TOKEN_STR3(TK_OR_EQ, "|=", 15, NOP_NONE);
	TOKEN_STR3(TK_OR2, "||", 13, NOP_NONE);

	TOKEN_STR2(TK_L_SMALL, "(");
	TOKEN_STR2(TK_R_SMALL, ")");
	TOKEN_STR2(TK_L_MIDDLE, "{");
	TOKEN_STR2(TK_R_MIDDLE, "}");
	TOKEN_STR2(TK_L_ARRAY, "[");
	TOKEN_STR2(TK_R_ARRAY, "]");

	TOKEN_STR3(TK_GREAT, ">", 7, OP_GREAT);
	TOKEN_STR3(TK_GREAT_EQ, ">=", 7, OP_GREAT_EQ);
	TOKEN_STR3(TK_LESS, "<", 7, OP_LESS);
	TOKEN_STR3(TK_LESS_EQ, "<=", 7, OP_LESS_EQ);

	TOKEN_STR2(TK_COLON, ":");
	TOKEN_STR2(TK_SEMICOLON, ";");
	TOKEN_STR2(TK_COMMA, ",");
	TOKEN_STR2(TK_DOT, ".");
	TOKEN_STR3(TK_DOT2, "..", 6, NOP_STR_ADD);
	TOKEN_STR2(TK_QUOTE2, "\"");
	TOKEN_STR2(TK_QUOTE1, "'");
	TOKEN_STR2(TK_LOGIC_NOT, "!");

	TOKEN_STR2(TK_YIELD, "yield");

	/////////////////////////////////////////////////////

	return 1;
}
static bool g_bInitVM = false;
// (구 전역 g_NeoLoader 제거) import loader 는 이제 NeoCompilerParam.loader → CArchiveRdWC::m_pLoader 로 전달.

std::string GetTokenString(TK_TYPE tk)
{
	if(tk < 0 || tk >= TK_MAX)
		return "";
	return g_sTokenToString[tk]._str;
}

#define GLOBAL_INIT_FUN_NAME	"##_global"
int ParseFunctionBase(CArchiveRdWC& ar, SFunctions& funs, SVars& vars, std::string fname, FUNCTION_TYPE funType); // -1 : error
static bool CollectFunctionDeclarations(CArchiveRdWC& ar, SFunctions& funs);


void SkipCurrentLine(CArchiveRdWC& ar)
{
	while (true)
	{
		u16 c = ar.GetData(true);
		switch (c)
		{
		case '\n':
			ar.AddLine();
		case 0:
			return;
		default:
			break;
		}
	}
}

bool SkipMultiLine(CArchiveRdWC& ar)
{
	while (true)
	{
		switch (ar.GetData(true))
		{
		case 0:
			SetCompileError(ar, "Error (%d, %d): unterminated block comment", ar.CurLine(), ar.CurCol());
			return false;
		case '\n':
			ar.AddLine();
			break;
		case '*':
			if (ar.GetData(false) == '/')
			{
				ar.GetData(true);
				return true;
			}
			break;
		default:
			break;
		}
	}
}

TK_TYPE CalcStringToken(std::string& tk)
{
	if (true == tk.empty())
		return TK_NONE;

	auto it = g_sStringToToken.find(tk);
	if (it != g_sStringToToken.end())
	{
		return (*it).second;
	}

	return TK_STRING;
}

TK_TYPE GetToken(CArchiveRdWC& ar, std::string& tk);

static TK_TYPE GetRawToken(CArchiveRdWC& ar, std::string& tk)
{
	const bool suppressed = ar.m_bSuppressDefines;
	ar.m_bSuppressDefines = true;
	const TK_TYPE type = GetToken(ar, tk);
	ar.m_bSuppressDefines = suppressed;
	return type;
}

static TK_TYPE CompileDefineTokenToToken(const NeoCompileDefineToken& defineToken, std::string& tk)
{
	tk = defineToken.text;
	switch (defineToken.type)
	{
	case NEO_DEFINE_TOKEN_IDENTIFIER:
	case NEO_DEFINE_TOKEN_INT:
	case NEO_DEFINE_TOKEN_FLOAT:
		return TK_STRING;
	case NEO_DEFINE_TOKEN_STRING:
		return TK_STRING_LITERAL; // 완성된 문자열 리터럴 (따옴표 재파싱 없이 tk 가 곧 내용)
	case NEO_DEFINE_TOKEN_TRUE:
//		if (tk.empty()) tk = "true"; // 이렇게 하는것 보다 Define 을 전달하는 곳에서 잘하면 됨.
		return TK_TRUE;
	case NEO_DEFINE_TOKEN_FALSE:
//		if (tk.empty()) tk = "false";
		return TK_FALSE;
	case NEO_DEFINE_TOKEN_NULL:
//		if (tk.empty()) tk = "null";
		return TK_NULL;
	default:
		return TK_STRING;
	}
}

static TK_TYPE ApplyCompileDefine(CArchiveRdWC& ar, TK_TYPE tkType, std::string& tk)
{
	if (tkType != TK_STRING || ar.m_bSuppressDefines)
		return tkType;

	// 스크립트 const (모듈-로컬) 우선 조회 — 호스트 define 과의 충돌은 선언 시점에 에러 처리됨
	const NeoCompileDefineToken* scriptDefine = ar.FindScriptDefine(tk);
	if (scriptDefine)
		return CompileDefineTokenToToken(*scriptDefine, tk);

	if (ar.m_pDefines == nullptr)
		return tkType;

	auto it = ar.m_pDefines->values.find(tk);
	if (it == ar.m_pDefines->values.end())
		return tkType;

	const NeoCompileDefineToken& f = (*it).second;
	return CompileDefineTokenToToken(f, tk);
}


TK_TYPE CalcToken(TK_TYPE tkTypeOnySingleChar, CArchiveRdWC& ar, std::string& tk)
{
	if (false == tk.empty())
		return ApplyCompileDefine(ar, CalcStringToken(tk), tk);

	u16 c1 = ar.GetData(true);
	u16 c2 = ar.GetData(false);
	if (c2 > 255)
	{
		tk = GetTokenString(tkTypeOnySingleChar);
		return tkTypeOnySingleChar;
	}

	std::string s1;
	s1 += (u8)c1;
	s1 += (u8)c2;

	TK_TYPE tkTemp = CalcStringToken(s1);
	if (tkTemp != TK_STRING && tkTemp != TK_NONE)
	{
		ar.GetData(true);
		u16 c3 = ar.GetData(false);

		// check 3 char token
		s1 += (u8)c3;
		TK_TYPE tkTemp2 = CalcStringToken(s1);
		if (tkTemp2 != TK_STRING && tkTemp2 != TK_NONE)
		{
			ar.GetData(true);
			tk = GetTokenString(tkTemp2);
			return tkTemp2;
		}

		tk = GetTokenString(tkTemp);
		return tkTemp;
	}

	tk = GetTokenString(tkTypeOnySingleChar);
	return tkTypeOnySingleChar;
}

OperationCode TokenToOP(TK_TYPE tk, int& iPriority)
{
	if (tk < 0 || tk >= TK_MAX)
	{
		iPriority = 20;
		return NOP_NONE;
	}
	
	STokenValue& tv = g_sTokenToString[tk];

	iPriority = tv._iPriority;
	return tv._op;
}

static bool ReadUnicodeEscape(CArchiveRdWC& ar, u16& value)
{
	value = 0;
	for (int i = 0; i < 4; ++i)
	{
		const u16 c = ar.GetData(true);
		int digit;
		if (c >= '0' && c <= '9') digit = c - '0';
		else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
		else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
		else
		{
			SetCompileError(ar, "Error (%d, %d): invalid Unicode escape (expected four hex digits)", ar.CurLine(), ar.CurCol());
			return false;
		}
		value = static_cast<u16>((value << 4) | digit);
	}
	return true;
}

bool GetQuotationString(CArchiveRdWC& ar, std::string& str, u16 quote)
{
	str.clear();
	bool blEnd = false;
	while (blEnd == false)
	{
		u16 c1 = ar.GetData(true);
		if (c1 == 0) // null
			return false;
		if (c1 == '\n')
			return false;
		if (c1 == quote)
			break;
		if (c1 == '\\')
		{
			u16 c2 = ar.GetData(false);
			switch (c2)
			{
			case '"':
			case '\'':
			case '\\':
				c1 = c2;
				ar.GetData(true);
				break;
			case 'n':
				c1 = '\n';
				ar.GetData(true);
				break;
			case 'r':
				c1 = '\r';
				ar.GetData(true);
				break;
			case 't':
				c1 = '\t';
				ar.GetData(true);
				break;
			case 'u':
			{
				ar.GetData(true);
				u16 first;
				if (!ReadUnicodeEscape(ar, first)) return false;
				if (first >= 0xD800 && first <= 0xDBFF)
				{
					u16 second;
					if (ar.GetData(true) != '\\' || ar.GetData(true) != 'u')
					{
						SetCompileError(ar, "Error (%d, %d): invalid Unicode escape (missing low surrogate)", ar.CurLine(), ar.CurCol());
						return false;
					}
					if (!ReadUnicodeEscape(ar, second)) return false;
					if (second < 0xDC00 || second > 0xDFFF)
					{
						SetCompileError(ar, "Error (%d, %d): invalid Unicode escape (expected low surrogate)", ar.CurLine(), ar.CurCol());
						return false;
					}
					utf_string::UNICODE_UTF8_PAIR(first, second, str);
				}
				else
				{
					// Runtime string APIs use NUL-terminated text. Reject U+0000
					// rather than compile a string that silently truncates later.
					if (first == 0 || (first >= 0xDC00 && first <= 0xDFFF))
					{
						SetCompileError(ar, "Error (%d, %d): invalid Unicode escape (NUL or unpaired low surrogate)", ar.CurLine(), ar.CurCol());
						return false;
					}
					utf_string::UNICODE_UTF8_ONE(first, str);
				}
				continue;
			}
			}
		}

		if (c1 >= 0xD800 && c1 <= 0xDBFF)
		{
			const u16 c2 = ar.GetData(false);
			if (c2 < 0xDC00 || c2 > 0xDFFF)
				return false;
			ar.GetData(true);
			if (utf_string::UNICODE_UTF8_PAIR(c1, c2, str) < 0)
				return false;
		}
		else if (utf_string::UNICODE_UTF8_ONE(c1, str) < 0)
		{
			return false;
		}
	}
	return true;
}
static inline bool IsAlphabet(char c)
{
	if ('a' <= c && c <= 'z')
		return true;
	if ('A' <= c && c <= 'Z')
		return true;
	return false;
}
static inline bool IsDotChar(u16 c)
{
	if ('a' <= c && c <= 'z')
		return true;
	if ('A' <= c && c <= 'Z')
		return true;
	if ('0' <= c && c <= '9')
		return true;
	if(c == '_')
		return true;

	return false;
}
void SkipSpace(CArchiveRdWC& ar)
{
	while (true)
	{
		u16 c1 = ar.GetData(false);
		if (c1 == ' ' || c1 == '\t')
		{
			ar.GetData(true);
			continue;
		}
		return;
	}
}
bool GetDotString(CArchiveRdWC& ar, std::string& str)
{
	SkipSpace(ar);

	str.clear();
	bool blEnd = false;
	while (blEnd == false)
	{
		u16 c1 = ar.GetData(false);
		if (false == IsDotChar(c1))
			break;

		ar.GetData(true);
		str.push_back((char)c1);
	}
	return true;
}
bool AbleName(const std::string& str)
{
	if (str.empty())
		return false;

	const char* p = str.c_str();
	for (int i = 0; i < (int)str.length(); i++)
	{
		char c = p[i];
		if (i == 0)
		{	// first is Alphabet or '_'
			if (IsAlphabet(c) == false && c != '_')
				return false;
		}
		else
		{
			if (IsDotChar(c) == false)
				return false;
		}
	}
	return true;
}

static bool AbleBackendName(const CArchiveRdWC& ar, const std::string& name)
{
	if (AbleName(name)) return true;
	// 검사된 NeoCore 골격에서만 내부 식별자를 허용한다. import에는 이 권한을 넘기지 않는다.
	if (!ar.m_coreTypedIR) return false;
	const char* prefix = NeoCore::InternalSymbolPrefix();
	const std::size_t length = std::strlen(prefix);
	return name.size() > length && name.compare(0, length, prefix) == 0 && AbleName(name.substr(1));
}

// 숫자 리터럴을 통째로 한 토큰으로 읽는다(예전엔 '.'/지수부호에서 쪼개져 1.2e3, 5.6E-4 가 깨짐).
// 정수부·소수부·지수부(e/E[+/-]digits)와 헥사(0x…, 0x1.8p4)를 지원. '.' 은 뒤가 숫자일 때만
// 소비해 멤버 접근(5.foo)과 구분한다. 실제 값 파싱/검증은 StringToNumber 가 담당.
static void ScanNumberLiteral(CArchiveRdWC& ar, std::string& tk)
{
	auto isDig = [](u16 c) { return c >= '0' && c <= '9'; };
	auto isHex = [](u16 c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); };
	auto take  = [&]() { tk.push_back((char)ar.GetData(true)); };

	if (ar.GetData(false) == '0' && (ar.PeekData(1) == 'x' || ar.PeekData(1) == 'X'))
	{
		take(); take(); // "0x"
		while (isHex(ar.GetData(false))) take();
		if (ar.GetData(false) == '.' && isHex(ar.PeekData(1)))
		{
			take();
			while (isHex(ar.GetData(false))) take();
		}
		if (ar.GetData(false) == 'p' || ar.GetData(false) == 'P')
		{
			u16 n1 = ar.PeekData(1);
			if (isDig(n1) || ((n1 == '+' || n1 == '-') && isDig(ar.PeekData(2))))
			{
				take(); // p/P
				u16 s = ar.GetData(false);
				if (s == '+' || s == '-') take();
				while (isDig(ar.GetData(false))) take();
			}
		}
		return;
	}

	while (isDig(ar.GetData(false))) take();            // 정수부
	if (ar.GetData(false) == '.' && isDig(ar.PeekData(1))) // 소수부('.' 뒤 숫자일 때만)
	{
		take();
		while (isDig(ar.GetData(false))) take();
	}
	if (ar.GetData(false) == 'e' || ar.GetData(false) == 'E') // 지수부
	{
		u16 n1 = ar.PeekData(1);
		if (isDig(n1) || ((n1 == '+' || n1 == '-') && isDig(ar.PeekData(2))))
		{
			take(); // e/E
			u16 s = ar.GetData(false);
			if (s == '+' || s == '-') take();
			while (isDig(ar.GetData(false))) take();
		}
	}
}

TK_TYPE GetToken(CArchiveRdWC& ar, std::string& tk)
{
	if (false == ar.m_sTokenQueue.empty())
	{
		SToken d = *ar.m_sTokenQueue.begin();
		ar.m_sTokenQueue.pop_front();
		tk = d._tk;
		return d._type;
	}

	if (ar.IsEOF())
		return TK_NONE;

	tk.clear();

	bool blEnd = false;
	while (blEnd == false)
	{
		u16 c1 = ar.GetData(false);
		switch (c1)
		{
		case 0:
			return CalcToken(TK_NONE, ar, tk);

		case '\n':
			if (false == tk.empty())
				return ApplyCompileDefine(ar, CalcStringToken(tk), tk);
			ar.GetData(true);
			ar.AddLine();
			break;
		case ' ':
		case '\r':
		case '\t':
			if (false == tk.empty())
				return ApplyCompileDefine(ar, CalcStringToken(tk), tk);
			ar.GetData(true);
			break;

		case '+':
			return CalcToken(TK_PLUS, ar, tk);
		case '-':
			return CalcToken(TK_MINUS, ar, tk);
		case '*':
			return CalcToken(TK_MUL, ar, tk);
		case '%':
			return CalcToken(TK_PERCENT, ar, tk);
		case '~':
			return CalcToken(TK_NOT, ar, tk);
		case '^':
			return CalcToken(TK_XOR, ar, tk);
		case '=':
			return CalcToken(TK_EQUAL, ar, tk);
		case '&':
			return CalcToken(TK_AND, ar, tk);
		case '|':
			return CalcToken(TK_OR, ar, tk);

		case '(':
			return CalcToken(TK_L_SMALL, ar, tk);
		case ')':
			return CalcToken(TK_R_SMALL, ar, tk);
		case '{':
			return CalcToken(TK_L_MIDDLE, ar, tk);
		case '}':
			return CalcToken(TK_R_MIDDLE, ar, tk);
		case '[':
			return CalcToken(TK_L_ARRAY, ar, tk);
		case ']':
			return CalcToken(TK_R_ARRAY, ar, tk);
		case '<':
			return CalcToken(TK_LESS, ar, tk);
		case '>':
			return CalcToken(TK_GREAT, ar, tk);

		case ':':
			return CalcToken(TK_COLON, ar, tk);
		case ';':
			return CalcToken(TK_SEMICOLON, ar, tk);
		case ',':
			return CalcToken(TK_COMMA, ar, tk);
		case '.':
			return CalcToken(TK_DOT, ar, tk);
		case '"':
			return CalcToken(TK_QUOTE2, ar, tk);
		case '\'':
			return CalcToken(TK_QUOTE1, ar, tk);
		case '!':
			return CalcToken(TK_LOGIC_NOT, ar, tk);

		case '/':
		{
			if (!tk.empty())
				return ApplyCompileDefine(ar, CalcStringToken(tk), tk);
			ar.GetData(true);
			u16 c2 = ar.GetData(false);
			if (c2 == '/')	// '//'
				SkipCurrentLine(ar);
			else if (c2 == '*')	// '/*'
			{
				ar.GetData(true);
				if (!SkipMultiLine(ar)) return TK_NONE;
			}
			else if (c2 == '=')
			{
				ar.GetData(true);
				tk = GetTokenString(TK_DIV_EQ);
				return TK_DIV_EQ;
			}
			else
			{
				tk = GetTokenString(TK_DIV);
				return TK_DIV;
			}
			break;
		}
		default:
			if (tk.empty() && c1 >= '0' && c1 <= '9') // 토큰 시작이 숫자 → 리터럴 통째로(1.2e3, 5.6E-4, 0xff)
			{
				ScanNumberLiteral(ar, tk);
				return ApplyCompileDefine(ar, CalcStringToken(tk), tk);
			}
			tk.push_back((char)c1);
			ar.GetData(true);
			break;
		}
	}
	return (tk.size() != 0) ? TK_STRING : TK_NONE;
}

struct ParserDepthGuard
{
    CArchiveRdWC& archive;
    explicit ParserDepthGuard(CArchiveRdWC& value) : archive(value)
    {
        CheckCompileRange("parser nesting depth", archive._parserDepth + 1, 0, 128);
        ++archive._parserDepth;
    }
    ~ParserDepthGuard() { --archive._parserDepth; }
};

bool UseableName(CArchiveRdWC& ar, SFunctions& funs, SVars& vars, const std::string& name, bool checkName)
{
	if (checkName && false == AbleBackendName(ar, name))
	{
		SetParserCompileError(ar, PCE_INVALID_LOCAL_NAME, funs.GetCurFunName().c_str(), name.c_str());
		return false;
	}
	SLayerVar* pCurLayer = vars.GetCurrentLayer();
	if (vars._varsFunction.size() == 1 && funs.FindFun(name) != nullptr)
	{
		SetCompileError(ar, "variable '%s' conflicts with a function in this module", name.c_str());
		return false;
	}

	if (pCurLayer->FindVarOnlyCurrentBlock(vars.DeclarationName(name)) != -1 ||
		(vars._varsFunction.size() == 1 && pCurLayer->_varsLayer.size() == 1 &&
			vars._hostGlobals.count(name) != 0))
	{
		SetParserCompileError(ar, PCE_DUPLICATE_LOCAL_NAME, name.c_str(), funs.GetCurFunName().c_str());
		return false;
	}
	return true;
}

int  AddLocalVarName(CArchiveRdWC& ar, SFunctions& funs, SVars& vars, bool blExport, bool blPublic, const std::string& name, bool checkName = true)
{
	if(false == UseableName(ar, funs, vars, name, checkName))
		return -1;
	// export var 는 호스트가 이름으로 찾는 변수(모듈에는 안 보임), public var 는 다른 모듈이
	// 별칭으로 읽고 쓰는 변수다. 호스트 이름이 충돌하면 모호한 이미지를 만들지 않는다.
	if (blExport && (vars._exportSlots.count(name) != 0 || funs._exportNames.count(name) != 0))
	{
		SetCompileError(ar, "duplicate exported variable '%s'", name.c_str());
		return -1;
	}
	int iLocalVar;
	if (funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME)
	{
		CheckCompileRange("global variable count", (int64_t)vars._globalVarCount + 1, 1, 32767);
		iLocalVar = COMPILE_GLOBAL_VAR_BEGIN - vars._globalVarCount++;
	}
	else
		iLocalVar = funs._cur->AllocLocalVar(); // 0 번은 리턴 저장용
	SLayerVar* pCurLayer = vars.GetCurrentLayer();
	pCurLayer->AddLocalVar(vars.DeclarationName(name), iLocalVar);
	const SlotType storage = funs.FindDeclaredStorage(name);
	if (storage.kind != SlotKind::Boxed)
	{
		if (iLocalVar < 0) vars._globalStorage[iLocalVar] = storage;
		else funs._cur->_storage[iLocalVar] = storage;
	}
	if (funs.GetCurFunName() != GLOBAL_INIT_FUN_NAME && iLocalVar > 0)
		funs._cur->_debugVarNames[iLocalVar] = name;
	if (blExport)
	{
		if(vars._varsFunction.size() == 1)
		{
			vars._varsExport.push_back(name);
			vars._exportSlots[name] = iLocalVar;
			funs._exportNames.insert(name);
		}
		else
		{
			SetParserCompileError(ar, PCE_NOT_FILE_SCOPE, "export");
			return -1;
		}
	}
	if (blPublic)
	{
		// 파일 최상위 블록만. 블록 안의 선언은 블록이 끝나면 사라지는데 공개 이름은 남아,
		// 뒤에 오는 같은 이름의 비공개 변수가 공개되어 버린다.
		if (vars._varsFunction.size() != 1 || pCurLayer->_varsLayer.size() != 1)
		{
			SetParserCompileError(ar, PCE_NOT_FILE_SCOPE, "public");
			return -1;
		}
		vars._publicVars.insert(vars.DeclarationName(name));
	}
	return iLocalVar;
}

// 이름을 읽는 함수가 익명 함수이고 이름이 상위 함수의 지역이라면, 부모의
// 물리 스택 슬롯을 본문에 박지 않는다. 람다 생성 시 그 슬롯의 VarInfo를
// ClosureRecord에 복사하고, 호출 시 이 함수의 숨은 지역 슬롯으로 주입한다.
// 전역(음수 compile 슬롯)은 기존처럼 런타임 전역 조회로 둔다.
// 다른 모듈의 public var 를 전역 슬롯으로 푼다. 모듈 변수는 전역 층에 "모듈.이름" 으로 있으므로
// 컴파일 때 슬롯이 정해지고, 같은 모듈 안의 전역 접근과 같은 명령이 나와 실행 비용이 없다.
// public 이 아닌 변수(export var 포함)와 호스트 전역은 돌려주지 않는다.
static int FindPublicModuleVar(SVars& vars, const SFunctionLayer* module, const std::string& name)
{
	if (module->_moduleName.empty() || vars._varsFunction.empty())
		return -1;
	const std::string qualified = module->_moduleName + "." + name;
	if (vars._publicVars.count(qualified) == 0)
		return -1;
	return vars._varsFunction[0]->FindVar(qualified);
}

// 파일 최상위(전역 초기화 함수의 바깥 블록)인가. import · const · public · export 는 여기서만 허용한다.
// 블록 안에 두면 블록이 끝나며 선언은 사라지는데 공개 이름 · 모듈 등록은 남아 뒤 선언에 번진다.
static bool AtFileTopLevel(SFunctions& funs, SVars& vars)
{
	SLayerVar* pLayer = vars.GetCurrentLayer();
	return funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME && vars._varsFunction.size() == 1 &&
		pLayer != nullptr && pLayer->_varsLayer.size() == 1;
}

static int FindReadableVar(SFunctions& funs, SVars& vars, const std::string& name)
{
	SLayerVar* owner = nullptr;
	int slot = vars.FindVar(name, &owner);
	if (slot == -1)
		return -1;
	if (owner == nullptr || owner == vars.GetCurrentLayer() || slot <= 0)
		return slot;
	if (funs._cur == nullptr || funs._cur->_funType != FUNT_ANONYMOUS)
		return -1;

	int ownerIndex = -1;
	for (int i = 0; i < (int)vars._varsFunction.size(); ++i)
	{
		if (vars._varsFunction[i] == owner)
		{
			ownerIndex = i;
			break;
		}
	}
	if (ownerIndex == -1)
		return -1;

	// FMOV는 언제나 직속 부모 프레임에서 sourceSlot을 읽는다. 따라서
	// Outer -> mid -> inner가 Outer의 v를 참조하면 mid에도 v를 먼저 캡처하고,
	// inner는 mid의 숨은 슬롯을 캡처해야 한다.
	for (int i = ownerIndex + 1; i < (int)vars._varsFunction.size(); ++i)
	{
		SLayerVar* layer = vars._varsFunction[i];
		SFunctionInfo* function = layer->_function;
		if (function == nullptr || function->_funType != FUNT_ANONYMOUS)
			return -1; // 일반 함수 프레임은 바깥 지역을 보관하지 않는다.
		const auto source = vars._varsFunction[i - 1]->_function->_storage.find(slot);
		const SlotType storage = source == vars._varsFunction[i - 1]->_function->_storage.end() ? SlotType() : source->second;
		slot = function->AddCapture(name, slot, layer);
		if (storage.kind != SlotKind::Boxed) function->_storage[slot] = storage;
	}
	return slot;
}

int  AddLocalVar(CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
	SLayerVar* pCurLayer = vars.GetCurrentLayer();

	char name[256];
	snprintf(name, _countof(name), "^_#@_temp_%d", vars._iTempVarNameIndex++);

	if (pCurLayer->FindVarOnlyCurrentBlock(vars.DeclarationName(name)) != -1)
	{
		SetParserCompileError(ar, PCE_DUPLICATE_LOCAL_NAME, name, funs.GetCurFunName().c_str());
		return -1;
	}
	int iLocalVar;
	if (funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME)
	{
		CheckCompileRange("global variable count", (int64_t)vars._globalVarCount + 1, 1, 32767);
		iLocalVar = COMPILE_GLOBAL_VAR_BEGIN - vars._globalVarCount++;
	}
	else
		iLocalVar = funs._cur->AllocLocalVar(); // 0 번은 리턴 저장용
	pCurLayer->AddLocalVar(vars.DeclarationName(name), iLocalVar);
	return iLocalVar;
}
void ClearTempVars(SFunctions& funs)
{
	funs._cur->FreeLocalTempVar();
	OperationCode opLast = funs._cur->GetLastOP();
	if (opLast == NOP_MOV || opLast == OP_I64_NEG || opLast == OP_F64_NEG
    )
	{
		int iVar = funs._cur->GetN(funs._cur->_iLastOPOffset, 0);
		if (IsTempVar(iVar))
		{
			//funs._cur->_code->SetPointer(funs._cur->_iLastOPOffset - (ar._debug ? sizeof(debug_info) : 0), SEEK_SET);
			funs._cur->_code->SetPointer(funs._cur->_iLastOPOffset, SEEK_SET);
			funs._cur->ClearLastOP();
		}
	}
	else if(opLast == NOP_CALL || opLast == NOP_PTRCALL2)
	{
		SVMOperation* op = funs._cur->GetOPPointer(funs._cur->_iLastOPOffset); // 3rd
		if (IsTempVar(op->n3))
		{
			op->argFlag |= NEOS_OP_CALL_NORESULT;
		}
	}
}
// 프런트엔드가 서명으로 검사한 내장 API 호출의 표기를 연결 이름으로 바꾼다.
//   메서드: recv.__api<번호>(인자...)      함수: $neocore_api_<번호>(인자...) 또는 전역 API 이름(print)
// 번호는 이번 컴파일의 등록부 색인이고, 이미지에는 연결 이름(소유자.이름) 상수만 남는다.
static bool CoreApiKeyByIndex(const std::string& text, std::size_t prefix, std::string& key)
{
    if (text.size() <= prefix) return false;
    std::size_t index = 0;
    for (std::size_t i = prefix; i < text.size(); ++i)
    {
        if (text[i] < '0' || text[i] > '9' || index > 100000) return false;
        index = index * 10 + static_cast<std::size_t>(text[i] - '0');
    }
    const std::vector<NeoCore::ApiDeclaration>& declarations = NeoCore::CoreApi();
    if (index >= declarations.size()) return false;
    key = declarations[index].key;
    return true;
}
static bool CoreApiMethodKey(const std::string& member, std::string& key)
{
    return member.compare(0, 5, "__api") == 0 && CoreApiKeyByIndex(member, 5, key) && key.find('.') != std::string::npos;
}
static bool CoreApiFunctionKey(const std::string& name, std::string& key)
{
    // 숫자 리터럴도 이 자리를 지나간다. 이름이 될 수 없는 토큰은 등록부를 찾지 않는다.
    if (name.empty()) return false;
    const char first = name[0];
    if (first == '$')
    {
        static const std::string prefix = std::string(NeoCore::InternalSymbolPrefix()) + "api_";
        return name.compare(0, prefix.size(), prefix) == 0 && CoreApiKeyByIndex(name, prefix.size(), key);
    }
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') || first == '_')) return false;
    const NeoCore::ApiDeclaration* declaration = NeoCore::FindCoreApi(std::string(), name);
    if (declaration == nullptr) return false;
    key = declaration->key;
    return true;
}
bool ParseImport(CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
	std::string fileName, tk2;
	TK_TYPE tkType1, tkType2;

	tkType1 = GetToken(ar, fileName);
	if (tkType1 != TK_STRING)
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "module name after 'import'", fileName.c_str());
		return false;
	}
	std::string defName = fileName; // default equal
	std::transform(fileName.begin(), fileName.end(), fileName.begin(), ::tolower);
	tkType2 = GetToken(ar, tk2);
	if (tkType2 == TK_STRING && tk2 == "as")
	{
		tkType2 = GetToken(ar, tk2);
		if (tkType2 == TK_STRING)
		{
			defName = tk2;
			tkType2 = GetToken(ar, tk2);
		}
	}

	if (tkType2 != TK_SEMICOLON)
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "';' after import statement", tk2.c_str());
		return false;
	}

	if(AbleName(defName) == false)
	{
		SetParserCompileError(ar, PCE_INVALID_LOCAL_NAME, funs.GetCurFunName().c_str(), defName.c_str());
		return false;
	}



	auto it = vars.m_sImports.find(fileName);
	if (it != vars.m_sImports.end())
	{
		funs._curModule->_defModules[defName] = (*it).second;
		return true;
	}

	// 순환 import 차단. m_sImports 는 파싱을 끝낸 뒤에 채워지므로 위 캐시로는 못 막는다.
	// 막지 않으면 같은 모듈을 재진입 파싱해 이름 충돌로 죽는데, 그 실패가 사유 없이
	// 사라져 "메시지 없는 컴파일 실패"가 된다(자기 자신 import 도 마찬가지).
	if (vars.m_sImporting.count(fileName) != 0)
	{
		SetParserCompileError(ar, PCE_IMPORT_CYCLE, fileName.c_str());
		return false;
	}

	std::string fullFileName;
	void* pFileBuffer = NULL;
	int iFileLen = 0;
	if (ar.m_pLoader)
	{
		const char* libPath = ar.m_pLoader->GetLibPath();
		if(libPath != nullptr)
		{
			fullFileName = libPath;
			//fullFileName = "../../Lib/";
			fullFileName += fileName + ".ns";

			if(false == ar.m_pLoader->Load(fullFileName.c_str(), pFileBuffer, iFileLen))
			{
				//SetCompileError(ar, "Error (%d, %d): Import Error (%s)", ar.CurLine(), ar.CurCol(), tk2.c_str());
				//return false;
			}
		}
	}
	if(pFileBuffer == nullptr)
	{
		// 내장 모듈의 함수는 프런트엔드가 서명으로 검사해 API 호출 표기로 바꿔 넘긴다. 여기서는 이름만 받는다.
		if (NeoCore::IsCoreApiModule(fileName))
			return true;
		SetParserCompileError(ar, PCE_IMPORT_FAILED, fileName.c_str());
		return false;
	}

	CArchiveRdWC ar2;
	ar2.m_sSourceName = fullFileName;
	const bool decoded = ToArchiveRdWC((const char*)pFileBuffer, iFileLen, ar2);
	if (ar.m_pLoader)
		ar.m_pLoader->Unload(fullFileName.c_str(), pFileBuffer, iFileLen);
	if (!decoded)
	{
		SetCompileError(ar2, "Invalid script source encoding (expected UTF-8 or BOM-marked UTF-16)");
		ar.CopyErrorFrom(ar2);
		return false;
	}
	ar2._allowGlobalInitLogic = ar._allowGlobalInitLogic;
	ar2._debug = ar._debug;
	ar2._parserDepth = ar._parserDepth;
	ar2.m_pDefines = ar.m_pDefines;
	ar2.m_pLoader = ar.m_pLoader;                 // 중첩 import 도 같은 loader 로 해석
	ar2.m_sModuleName = fileName;
	ar2.m_pDebugSourceFiles = ar.m_pDebugSourceFiles;
	if (ar2.m_pDebugSourceFiles != nullptr)
	{
		size_t fileSeq = ar2.m_pDebugSourceFiles->size();
		CheckCompileRange("debug source file index", (int64_t)fileSeq, 0, USHRT_MAX);
		if (fileSeq <= 0xffff)
		{
			ar2.m_iFileSeq = (u16)fileSeq;
			ar2.m_pDebugSourceFiles->push_back(fullFileName);
		}
	}

	SFunctionLayer* pLayerBackup = funs._curModule;
	funs._curModule = funs.NewLayer();
	funs._curModule->_moduleName = fileName;   // 별칭으로 public var 를 찾을 때의 한정 이름
	/////////////////////////////////
	vars.m_sImporting.insert(fileName);      // 이 모듈을 파싱하는 동안 재진입 금지
	bool r;
	{
		// ar2가 살아 있는 동안만 빌려 쓴다. 중첩 import와 예외에서도
		// ar2 소멸 전에 상위 모듈 포인터를 복원해 수명을 넘겨 참조하지 않는다.
		struct ModuleNameRestore
		{
			const std::string*& current;
			const std::string* previous;
			~ModuleNameRestore() { current = previous; }
		} restore{vars._moduleName, vars._moduleName};
		vars._moduleName = &ar2.m_sModuleName;
		r = CollectFunctionDeclarations(ar2, funs) && ParseFunctionBody(ar2, funs, vars, false);
	}
	vars.m_sImporting.erase(fileName);
	/////////////////////////////////
	vars.m_sImports[fileName] = funs._curModule;
	pLayerBackup->_defModules[defName] = funs._curModule;
	funs._curModule = pLayerBackup;

	// 하위 모듈에서 난 에러를 덮어쓰지 않는다(SetCompileError 는 first-wins 라
	// 여기서 빈 문자열로 밀면 순환 감지 메시지까지 사라진다).
	ar.CopyErrorFrom(ar2);
	return r;
//#else
//	SetCompileError(ar, "Error (%d, %d): Import Not Defined)", ar.CurLine(), ar.CurCol());
//	return false;
//#endif
}
bool ParseFunctionArg(CArchiveRdWC& ar, SFunctions& funs, SLayerVar* pCurLayer)
{
	std::string tk1, tk2;

	bool bPreviusComa = false;
	TK_TYPE tkType1, tkType2;
	bool blEnd = false;
	while (blEnd == false)
	{
		tkType1 = GetToken(ar, tk1);
		switch (tkType1)
		{
		case TK_VAR:
			tkType2 = GetToken(ar, tk2);
			if (tkType2 == TK_STRING)
			{
				auto it = funs._cur->_args.find(tk2);
				if (it != funs._cur->_args.end())
				{
					SetParserCompileError(ar, PCE_DUPLICATE_FUNCTION_ARGUMENT, tk2.c_str());
					return false;
				}
				int iArg = 1 + (int)funs._cur->_args.size();
				CheckCompileRange("function arguments", iArg, 0, COMPILE_MAX_ARGUMENTS);
				pCurLayer->AddLocalVar(tk2, iArg);
				const SlotType storage = static_cast<size_t>(iArg) <= funs._cur->_parameterStorage.size()
                    ? funs._cur->_parameterStorage[iArg - 1] : funs.FindDeclaredStorage(tk2);
				if (storage.kind != SlotKind::Boxed) funs._cur->_storage[iArg] = storage;
				funs._cur->_debugVarNames[iArg] = tk2;
				funs._cur->_args.insert(tk2);
				bPreviusComa = false;
			}
			else
			{
				SetParserCompileError(ar, PCE_EXPECTED_FUNCTION_ARGUMENT, tk1.c_str());
				return false;
			}
			break;
		case TK_R_SMALL:
			if (bPreviusComa)
			{
				SetParserCompileError(ar, PCE_EXPECTED_FUNCTION_ARGUMENT, ")");
				return false;
			}
			blEnd = true;
			break;
		case TK_COMMA:
			if (funs._cur->_args.size() == 0 || bPreviusComa)
			{
				SetParserCompileError(ar, PCE_EXPECTED_COMMA_BETWEEN_ARGUMENTS);
				return false;
			}
			bPreviusComa = !bPreviusComa;
			break;
		default:
			SetParserCompileError(ar, PCE_EXPECTED_FUNCTION_ARGUMENT, tk1.c_str());
			return false;
		}
	}
	return true;
}

// Collect names and arity without compiling bodies, evaluating initializers or
// loading imports. The real pass retains variable/const scope and import order.
// Named functions already belong to their module (including nested definitions);
// anonymous functions are still created at their expression in the real pass.
static bool CollectFunctionDeclarations(CArchiveRdWC& ar, SFunctions& funs)
{
	CArchiveRdWC scan = ar;
	scan.m_bSuppressDefines = true;
	try
	{
		std::string token;
		for (TK_TYPE type = GetToken(scan, token); type != TK_NONE; type = GetToken(scan, token))
		{
			if (type == TK_QUOTE2 || type == TK_QUOTE1)
			{
				if (!GetQuotationString(scan, token, type == TK_QUOTE2 ? '"' : '\''))
				{
					SetParserCompileError(scan, PCE_UNTERMINATED_STRING);
					break;
				}
				continue;
			}
			if (type == TK_DOT)
			{
				// The real parser reads a raw member name after '.', so t.fun is
				// not a declaration. Consume it the same way here; leave missing
				// or invalid selectors for the real pass to diagnose.
				GetDotString(scan, token);
				continue;
			}
			if (type != TK_FUN) continue;
			type = GetToken(scan, token);
			if (type == TK_L_SMALL) continue; // anonymous fun (...)
			if (type != TK_STRING || !AbleBackendName(scan, token))
			{
				SetParserCompileError(scan, PCE_INVALID_FUNCTION_NAME, token.c_str());
				break;
			}
			const std::string name = token;
			if (funs.FindFun(name))
			{
				SetParserCompileError(scan, PCE_DUPLICATE_FUNCTION, name.c_str());
				break;
			}
			if (GetToken(scan, token) != TK_L_SMALL)
			{
				SetParserCompileError(scan, PCE_EXPECTED_TOKEN, "'(' after function name", token.c_str());
				break;
			}
			const int id = funs.GetFunCountAll();
			CheckCompileRange("function id", id, 0, SHRT_MAX);
			SFunctionInfo* info = funs.NewFun(name, funs._cur->_code, funs._cur->_pDebugData);
			info->_funID = id;
			info->_moduleName = ar.m_sModuleName;
			funs._funIDs[id] = info;
			{
				struct RestoreCurrent
				{
					SFunctions& functions;
					SFunctionInfo* saved;
					~RestoreCurrent() { functions._cur = saved; }
				} restore{funs, funs._cur};
				funs._cur = info;
				SLayerVar arguments;
				if (!ParseFunctionArg(scan, funs, &arguments)) break;
			}
			type = GetToken(scan, token);
			if (type == TK_SEMICOLON)
			{
				SetParserCompileError(scan, PCE_FUNCTION_PROTOTYPE);
				break;
			}
			if (type != TK_L_MIDDLE)
			{
				SetParserCompileError(scan, PCE_EXPECTED_TOKEN, "'{' to start function body", token.c_str());
				break;
			}
		}
	}
	catch (const CompileLimitError& e)
	{ SetCompileError(scan, "Error (%d, %d): %s", scan.CurLine(), scan.CurCol(), e.what()); }
	ar.CopyErrorFrom(scan);
	return scan.m_sErrorString.empty();
}

void AddLocalVar(SLayerVar* pCurLayer)
{
	SLocalVar* pCurLocal = new SLocalVar();
	pCurLayer->_varsLayer.push_back(pCurLocal);
}

void DelLocalVar(SLayerVar* pCurLayer)
{
	int sz = (int)pCurLayer->_varsLayer.size();
	if (sz > 0)
	{
		SLocalVar* pCurLocal = pCurLayer->_varsLayer[sz - 1];
		delete pCurLocal;
		pCurLayer->_varsLayer.resize(sz - 1);
	}
}

SLayerVar* AddVarsFunction(SVars& vars)
{
	SLayerVar* pCurLayer = new SLayerVar();
	vars._varsFunction.push_back(pCurLayer);

	SLocalVar* pCurLocal = new SLocalVar();
	pCurLayer->_varsLayer.push_back(pCurLocal);

	return pCurLayer;
}

SLayerVar* DelVarsFunction(SVars& vars)
{
	int cnt = (int)vars._varsFunction.size();
	if (cnt > 0)
	{
		cnt -= 1;
		SLayerVar* p = vars._varsFunction[cnt];
		delete p;
		vars._varsFunction.resize(cnt);

		if(cnt > 0)
			return vars._varsFunction[cnt - 1];
	}
	return NULL;
}

bool IsTempVar(int iVar)
{
	if (COMPILE_LOCALTMP_VAR_BEGIN <= iVar && iVar < COMPILE_STATIC_VAR_BEGIN)
		return true;

	return false;
}
// (구 IsFloatNumberToken 제거: 정수/실수 판정은 StringToNumber → ParsedNumber.type 으로 통합됨)

// 파싱된 정수의 64비트 비트패턴을 유지하면서 최소 정수의 부호 반전을 처리한다.
static NS_INT NegateParsedInteger(NS_INT value)
{
	return value == NS_INT_MIN ? value : -value;
}

// This Function No Error Because Try Only
// return TK_NONE : error
TK_TYPE Try_ParseIntNum(int& iResultInt, CArchiveRdWC& ar, SFunctions& funs, SVars& vars, TK_TYPE tkTypeEnd1 = TK_UNUSED, TK_TYPE tkTypeEnd2 = TK_UNUSED, TK_TYPE tkTypeEnd3 = TK_UNUSED)
{
	std::string tk1, tk2;
	TK_TYPE tkType1, tkType2, tkTypePre = TK_PLUS;

	ParsedNumber num;

	tkType1 = GetToken(ar, tk1);

	if (true == StringToNumber(num, tk1.c_str()))
	{
		u16 c = ar.GetData(false);
		if (c == '.' || num.type == ParsedNumber::Type::Float || num.intValue < INT32_MIN || num.intValue > INT32_MAX)
		{
			ar.PushToken(tkType1, tk1);
			return TK_NONE; // float or double is not int
		}
		else
		{
			NS_INT inum = num.intValue;   // 정수 리터럴의 64비트 값을 보존한다.
			if (tkTypePre == TK_MINUS)
				inum = NegateParsedInteger(inum);
#if 0
			if (IsShort(inum))
			{
				iResultInt = static_cast<int>(inum);
				tkType2 = GetToken(ar, tk2);
				if (tkType2 == tkTypeEnd1 || tkType2 == tkTypeEnd2 || tkType2 == tkTypeEnd3)
					return tkType2;
				ar.PushToken(tkType2, tk2);
				ar.PushToken(tkType1, tk1);
				return tkType2;
			}
#endif
			iResultInt = static_cast<int>(inum);
			tkType2 = GetToken(ar, tk2);
			if (tkType2 == tkTypeEnd1 || tkType2 == tkTypeEnd2 || tkType2 == tkTypeEnd3)
				return tkType2;
			ar.PushToken(tkType2, tk2);
			ar.PushToken(tkType1, tk1);
			return TK_NONE;
		}
	}
	ar.PushToken(tkType1, tk1);
	return TK_NONE;
}

// 식을 끝까지 미리 훑어 부작용 종류를 돌려준다(비트 집합). 토큰 분류만 하고 코드는 내지 않는다.
//  ASSIGN: =, op=, ++, --   CALL: 이름/']'/')' 뒤의 '('   SELECTOR: '.' '[' (호스트 객체 getter 일 수 있다)
enum { EXPR_EFFECT_ASSIGN = 1, EXPR_EFFECT_CALL = 2, EXPR_EFFECT_SELECTOR = 4,
       EXPR_EFFECT_ALL = EXPR_EFFECT_ASSIGN | EXPR_EFFECT_CALL | EXPR_EFFECT_SELECTOR };
static int ScanExpressionEffects(CArchiveRdWC& ar, TK_TYPE e1, TK_TYPE e2, TK_TYPE e3,
    TK_TYPE e4, TK_TYPE e5, TK_TYPE e6);
// '(' 바로 뒤에서 호출 인자 목록을 짝 ')' 까지 훑어 인자별 부작용 플래그를 모은다. EOF 면 false.
static bool ScanCallArguments(CArchiveRdWC& ar, std::vector<int>& argEffects);
// 타입 IR 표식과 노드 방출(정의는 뒤에). 레거시 호출 경로가 IR 뿌리 하나인 인자를 창 슬롯에 바로 만들 때 쓴다.
static bool ParseCoreIRIndex(const std::string& name, const std::string& prefix, std::size_t count, std::size_t& index, CArchiveRdWC& ar);
// 직전 비교 명령을 조건 점프로 바꿔 쓴다(정의는 뒤에). 삼항식 조건이 쓴다.
static bool TryFuseCompareJump(bool bJumpWhenTrue, const SOperand& operand, SFunctions& funs, SJumpValue& outJump);
static bool EmitCoreIRNode(std::size_t index, SOperand& result, CArchiveRdWC& ar, SFunctions& funs, SVars& vars, int destination);
// 뒤따르는 코드가 실행되기 전에 이 피연산자의 값을 지금 확정해 둬야 하는가.
//  - 임시/상수: 아무도 바꾸지 못한다.
//  - 컨테이너 원소 읽기(a[i], m.k, 체인): 뒤에 호출·선택자·대입이 하나라도 오면 지금 읽는다.
//    원소는 별칭으로 공유되므로 변수가 지역이든 재대입이 없든 상관없다.
//  - 지역·인자 변수 자체: 호출된 함수와 getter 는 호출자의 지역을 바꿀 수 없다(캡처는 값 복사).
//    같은 식 안의 대입(=, op=, ++, --)만 위험하다.
//  - 전역: 호출이 바꿀 수 있고, 선택자도 바꿀 수 있다(호스트 객체가 어떤 변수·괄호 뒤에 있을지 모르므로
//    모든 선택자를 getter 가능으로 본다).
static bool NeedsSnapshot(const SOperand& operand, int effects)
{
    if (effects == 0)
        return false;
    if (operand.IsArray() || !operand._pathKeys.empty() ||
        (operand._container.owner != INVALID_ERROR_PARSEJOB && !operand._container.loaded))
        return true;
    const int slot = operand._iVar;
    if (slot == INVALID_ERROR_PARSEJOB || IsTempVar(slot) || IsCompileConstant(slot))
        return false;
    if (slot <= COMPILE_GLOBAL_VAR_BEGIN)
        return true;
    return (effects & EXPR_EFFECT_ASSIGN) != 0;
}

// 호출 앞에 PREPARE_CALL이 필요한가.
static bool CallNeedsPrepare(const SFunctionInfo* callee, std::size_t argumentCount)
{
    return SFunctionInfo::CallWindowNeedsPrepare(callee, argumentCount);
}

// 다음 인자가 IR 표식 하나(`$irN()`)로 끝나는가. 밀어 둔 토큰 하나(표식 이름)와 그 뒤 원문 `( )` 그리고 `,`/`)`를
// 소비하지 않고 본다. 맞으면 그 IR 계산이 창 슬롯을 목적지로 받을 수 있다.
static bool PeekLoneCoreIRReference(CArchiveRdWC& ar)
{
    if (ar.m_coreTypedIR == nullptr || ar.m_coreTypedIR->prefix.empty() || ar.m_sTokenQueue.size() != 1) return false;
    const SToken& queued = ar.m_sTokenQueue.front();
    const std::string& prefix = ar.m_coreTypedIR->prefix;
    if (queued._type != TK_STRING || queued._tk.size() <= prefix.size() || queued._tk.compare(0, prefix.size(), prefix) != 0) return false;
    for (std::size_t i = prefix.size(); i < queued._tk.size(); ++i)
        if (queued._tk[i] < '0' || queued._tk[i] > '9') return false;
    int k = 0;
    auto Skip = [&]() { for (;;) { const u16 c = ar.PeekData(k); if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return; ++k; } };
    Skip(); if (ar.PeekData(k) != '(') return false; ++k;
    Skip(); if (ar.PeekData(k) != ')') return false; ++k;
    Skip();
    const u16 next = ar.PeekData(k);
    return next == ',' || next == ')';
}

// 피연산자가 상수 풀의 bool 값인가. `x == true` 같은 비교를 값 검사로 줄일 때 쓴다.
static bool StaticBoolOperand(SFunctions& funs, SOperand& operand, bool& value)
{
    if (operand.IsArray() || operand.IsFun() || !IsCompileConstant(operand._iVar)) return false;
    const BoundaryValue& constant = funs._staticVars[StaticPoolIndex(operand._iVar)];
    if (constant.GetType() != VAR_BOOL) return false;
    value = constant._bl;
    return true;
}

bool ParseFunCall(SOperand& iResultStack, TK_TYPE tkTypePre, SFunctionInfo* pFun, CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
	std::string tk1, tk2;
	TK_TYPE tkType1, tkType2, tkType3;


	SOperand iTempVar;

	tkType1 = GetToken(ar, tk1);
	if (tkType1 == TK_L_SMALL)
	{
        ResolveContainer(iResultStack, ar, funs);
        // 인자 목록을 한 번만 훑어 인자별 부작용을 얻는다. 수신자 복사, 인자 복사, 인자 창 융합 판정이 함께 쓴다.
        std::vector<int> argEffects;
        const bool argsScanned = ScanCallArguments(ar, argEffects);
        int allArgEffects = argsScanned ? 0 : EXPR_EFFECT_ALL;
        for (size_t i = 0; i < argEffects.size(); ++i) allArgEffects |= argEffects[i];
        // 수신자(lst.len() 의 lst)는 인자 평가가 그것을 다시 묶을 수 있을 때만 복사한다.
        // 인자가 없거나 단순 값뿐이면 복사할 이유가 없다(이전에는 호출마다 MOV 하나를 냈다).
        if (!pFun && iResultStack._iVar != INVALID_ERROR_PARSEJOB && NeedsSnapshot(SOperand(iResultStack._iVar), allArgEffects))
        {
            SOperand receiver(iResultStack._iVar);
            SnapshotValue(receiver, ar, funs);
            iResultStack._iVar = receiver._iVar;
        }
		int iCallLine = ar.CurLine();
		int iParamCount = 0;
		// 각 인자를 평가하되, 호출-인자 슬롯(COMPILE_CALLARG_VAR_BEGIN)으로의 복사는
		// 모든 인자 평가가 끝난 뒤로 미룬다. 인자식이 그 자체로 함수 호출이면 내부 호출이
		// 같은 호출-인자 윈도우를 재사용하므로, 즉시 복사할 경우 먼저 평가된 인자가 덮어써진다.
		// 다만 뒤쪽 인자에 호출이 없으면(스캔한 부작용에 CALL 없음) 그 인자부터는 창을 먼저 고르고 창 슬롯에 바로
		// 만든다(EmitCoreIRCall 과 같은 규칙). 평가 순서는 그대로이고, 값이 생기는 즉시 창에 놓이므로 더 뒤의
		// 인자가 바꾸는 변수도 따로 복사하지 않는다.
		std::vector<SOperand> argOperands;
		std::size_t directBegin = argEffects.size();
		if (argsScanned)
			while (directBegin > 0 && (argEffects[directBegin - 1] & EXPR_EFFECT_CALL) == 0) --directBegin;
		bool windowPrepared = false;
		auto StageArgument = [&](SOperand& arg, int index)
		{
			const int slot = COMPILE_CALLARG_VAR_BEGIN + 1 + index;
			if (arg.IsFun())
			{
				funs._cur->_calls.back().types[index].kind = SlotKind::Function;
				funs._cur->Push_OP2(ar, NOP_FMOV1, slot, arg._iVar, false, iCallLine);
			}
			else if (!TryPushImmediate(ar, funs, slot, arg, iCallLine))
				funs._cur->Push_OP2(ar, NOP_MOV, slot, arg._iVar, arg.IsShort(), iCallLine);
		};
		while (true)
		{
			tkType2 = GetToken(ar, tk2);
			if (tkType2 == TK_R_SMALL)
				break;

			ar.PushToken(tkType2, tk2);

			//int iTryValue = -1;
			//tkType3 = Try_ParseIntNum(iTryValue, ar, funs, vars, TK_COMMA, TK_R_SMALL);
			//if (tkType3 != TK_NONE)
			//{
			//	funs._cur->Push_MOV(ar, NOP_MOV, COMPILE_CALLARG_VAR_BEGIN + 1 + iParamCount, iTryValue, true);
			//}
			//else
			{
                // 이 인자 뒤의 인자들이 가진 부작용. 인자 창(CALLARG) 을 다시 쓰는 것은 호출뿐이다 —
                // 호스트 getter 는 PropertyNative 가 프레임을 _iSP_Vars_Max2 위에 잡으므로 창을 건드리지 않는다.
                const int thisArgEffects = (argsScanned && iParamCount < (int)argEffects.size()) ? argEffects[iParamCount] : EXPR_EFFECT_ALL;
                int laterArgEffects = argsScanned ? 0 : EXPR_EFFECT_ALL;
                for (size_t later = (size_t)iParamCount + 1; later < argEffects.size(); ++later) laterArgEffects |= argEffects[later];
				const bool direct = windowPrepared ||
					(argsScanned && iParamCount < (int)argEffects.size() && static_cast<std::size_t>(iParamCount) >= directBegin);
				if (direct && !windowPrepared)
				{
					// 첫 직접 인자 앞에서 창을 고르고, 임시에 둔 앞 인자들을 옮긴다. 창에 바로 만드는 인자의 원본은
					// 없으므로(-1) 저장소 추론이 피호출자 서명으로 그 슬롯의 종류를 정한다.
					std::vector<int> callSources(argEffects.size(), -1);
					for (std::size_t s = 0; s < argOperands.size(); ++s) callSources[s] = argOperands[s].IsFun() ? -1 : argOperands[s]._iVar;
					funs._cur->PrepareCall(ar, callSources, iCallLine, CallNeedsPrepare(pFun, argEffects.size()));
					for (std::size_t s = 0; s < argOperands.size(); ++s) StageArgument(argOperands[s], static_cast<int>(s));
					windowPrepared = true;
				}
				iTempVar.Reset();
				const int slot = COMPILE_CALLARG_VAR_BEGIN + 1 + iParamCount;
				if (direct && PeekLoneCoreIRReference(ar))
				{
					// 인자가 IR 뿌리 하나면 그 계산(숫자 연산·컨테이너 읽기)이 창 슬롯을 목적지로 받는다.
					std::size_t irIndex = 0;
					if (GetToken(ar, tk2) != TK_STRING ||
						!ParseCoreIRIndex(tk2, ar.m_coreTypedIR->prefix, ar.m_coreTypedIR->nodes.size(), irIndex, ar) ||
						!EmitCoreIRNode(irIndex, iTempVar, ar, funs, vars, slot))
						return false;
					tkType3 = GetToken(ar, tk2);
				}
				else
				{
					// 인자 식의 ParseJob 은 방금 훑은 이 인자의 비트를 그대로 쓴다(재스캔 생략).
					funs._presetExprEffects = thisArgEffects;
					funs._presetExprOffset = ar.GetBufferOffset();
					tkType3 = ParseShortCircuitLogic(true, iTempVar, ar, funs, vars);
					funs._presetExprEffects = -1;
				}
				if (iTempVar.IsInvalidValue())
				{
					SetParserCompileError(ar, PCE_INVALID_CALL_ARGUMENT);
					return false;
				}
				if (direct)
				{
					if (iTempVar.IsArray() || iTempVar._iVar != slot)
					{
						MaterializeValue(iTempVar, ar, funs);
						if (!iTempVar.IsFun()) funs._cur->_calls.back().sources[iParamCount] = iTempVar._iVar;
						StageArgument(iTempVar, iParamCount);
					}
					argOperands.push_back(SOperand(slot));
				}
				else
				{
					// 컨테이너 읽기는 여기서 실체화하고, 변수는 뒤 인자가 그 값을 바꿀 수 있을 때만 복사한다.
					MaterializeValue(iTempVar, ar, funs);
					if (NeedsSnapshot(iTempVar, laterArgEffects))
						SnapshotValue(iTempVar, ar, funs);
					argOperands.push_back(iTempVar);
				}


			}
			CheckCompileRange("call arguments", (int64_t)iParamCount + 1, 0, COMPILE_MAX_ARGUMENTS);
			iParamCount++;


			if (tkType3 != TK_R_SMALL && tkType3 != TK_COMMA)
			{
				SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "',' or ')' after function argument", tk2.c_str());
				return false;
			}
			if (tkType3 == TK_R_SMALL)
				break;
		}

        // 몸통이 전역 RETURN 하나인 함수의 인자 없는 호출은 호출 창 없이 그 전역을 임시로 복사한다.
        // 복사 시점이 호출 시점과 같아 뒤의 식이 원본을 바꿔도 평가 순서가 같다.
        if (pFun != NULL && pFun->_funType != FUNT_BUILT_IN && pFun->_globalGetterSlot != 0 && iParamCount == 0)
        {
            iResultStack = funs._cur->AllocLocalTempVar();
            funs._cur->Push_OP2(ar, NOP_MOV, iResultStack._iVar, pFun->_globalGetterSlot, false, iCallLine);
            if (tkTypePre == TK_MINUS)
                funs._cur->Push_OP2(ar, RejectLegacyArithmetic(ar), iResultStack._iVar, iResultStack._iVar, false);
            return true;
        }
        // 직접 쓰기가 없었으면 호출 창은 모든 인자 평가가 끝난 뒤에만 선택한다. 다른 호출의 참조와 겹치지 않는다.
        if (!windowPrepared)
        {
            std::vector<int> callSources;
            for (SOperand& argument : argOperands) callSources.push_back(argument.IsFun() ? -1 : argument._iVar);
            funs._cur->PrepareCall(ar, callSources, iCallLine, CallNeedsPrepare(pFun, argOperands.size()));
            for (int i = 0; i < iParamCount; ++i) StageArgument(argOperands[i], i);
        }
		if (pFun != NULL)
		{
			if(pFun->_funType == FUNT_BUILT_IN)
			{
				if(pFun->_built_in_arg_c != -1)
				{
					if (pFun->_built_in_arg_c != iParamCount)
					{
						SetParserCompileError(ar, PCE_ARGUMENT_COUNT_MISMATCH, pFun->_built_in_arg_c, iParamCount);
						return false;
					}
				}
				//SetCompileError(ar, "Error (%d, %d):  Built-In", ar.CurLine(), ar.CurCol());
				int iArrayIndex = funs.AddStaticString(pFun->_name);
				iResultStack = funs._cur->AllocLocalTempVar();
				funs._cur->Push_CallPtr2(ar, iArrayIndex, iParamCount, iResultStack._iVar, iCallLine);
				//funs._cur->Push_OP2(ar, tkTypePre != TK_MINUS ? NOP_MOV : RejectLegacyArithmetic(ar), iResultStack._iVar, STACK_POS_RETURN, false);
				if (tkTypePre == TK_MINUS)
					funs._cur->Push_OP2(ar, RejectLegacyArithmetic(ar), iResultStack._iVar, iResultStack._iVar, false);
				return true;
			}
			else
			{
				if ((int)pFun->_args.size() != iParamCount)
				{
					SetParserCompileError(ar, PCE_ARGUMENT_COUNT_MISMATCH, (int)pFun->_args.size(), iParamCount);
					return false;
				}

				iResultStack = funs._cur->AllocLocalTempVar();
				funs._cur->Push_Call(ar, NOP_CALL, pFun->_funID, iParamCount, iResultStack._iVar, iCallLine);
			}
			if (tkTypePre == TK_MINUS)
				funs._cur->Push_OP2(ar, RejectLegacyArithmetic(ar), iResultStack._iVar, iResultStack._iVar, false);
			return true;
		}
		else if(iResultStack._iVar != -1)
		{
			if(iResultStack._iArrayIndex != -1)
			{
				funs._cur->Push_CallPtr(ar, iResultStack._iVar, iResultStack._iArrayIndex, iParamCount, false, iCallLine);
				iResultStack = funs._cur->AllocLocalTempVar();
				funs._cur->Push_OP2(ar, tkTypePre != TK_MINUS ? NOP_MOV : RejectLegacyArithmetic(ar), iResultStack._iVar, STACK_POS_RETURN, false);
			}
			else
			{	// TODO
				funs._cur->Push_CallPtr(ar, iResultStack._iVar, iResultStack._iArrayIndex, iParamCount, false, iCallLine);
				iResultStack = funs._cur->AllocLocalTempVar();
				funs._cur->Push_OP2(ar, tkTypePre != TK_MINUS ? NOP_MOV : RejectLegacyArithmetic(ar), iResultStack._iVar, STACK_POS_RETURN, false);
			}
		}
		else
		{
			int iArrayIndex = iResultStack._iArrayIndex;
			iResultStack = funs._cur->AllocLocalTempVar();
			funs._cur->Push_CallPtr2(ar, iArrayIndex, iParamCount, iResultStack._iVar, iCallLine);
//			funs._cur->Push_OP2(ar, tkTypePre != TK_MINUS ? NOP_MOV : RejectLegacyArithmetic(ar), iResultStack._iVar, STACK_POS_RETURN, false);
			if (tkTypePre == TK_MINUS)
				funs._cur->Push_OP2(ar, RejectLegacyArithmetic(ar), iResultStack._iVar, iResultStack._iVar, false);
		}
	}
	else if (tkType1 == TK_SEMICOLON)
	{
		ar.PushToken(tkType1, tk1);
		iResultStack._iVar = pFun->_funID;
		iResultStack._operandType = Data_Fun;
	}
	else
	{
		SetParserCompileError(ar, PCE_EXPECTED_LEFT_PAREN);
		return false;
	}
	return true;
}



bool ParseNum(SOperand& iResultStack, TK_TYPE tkTypePre, std::string& tk1, CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
	std::string tk2;

	ParsedNumber num;

	iResultStack = FindReadableVar(funs, vars, tk1);
	if (iResultStack._iVar != -1)
		return true;

	if (true == StringToNumber(num, tk1.c_str(), tkTypePre == TK_MINUS))
	{
		u16 c = ar.GetData(false);
		if (num.type == ParsedNumber::Type::Float) // 통째로 들어온 float(1.5, 1e+20 등)
		{
			double f = num.floatValue;
			if (tkTypePre == TK_MINUS)
				f = -f;
			int staticVar = funs.AddStaticNum(f);
			iResultStack.SetFloatLiteral(staticVar, (NS_FLOAT)f);
		}
		else if (c == '.')  // 정수 토큰 뒤 '.' → float 로 재조합(토크나이저가 "1.5"를 쪼갬)
		{
			ar.GetData(true);

			GetToken(ar, tk2);
			double num2 = 0;
			double f = num.floatValue;
			if (true == StringToDoubleLow(num2, tk2.c_str()))
			{
				f += num2;
			}
			else
			{
				SetParserCompileError(ar, PCE_INVALID_NUMBER_LITERAL);
				return TK_NONE;
			}
			if (tkTypePre == TK_MINUS)
				f = -f;
			int staticVar = funs.AddStaticNum(f);
			iResultStack.SetFloatLiteral(staticVar, (NS_FLOAT)f);
		}
		else
		{
			NS_INT inum = num.intValue;   // 정수 리터럴의 64비트 값을 보존한다.
			if (tkTypePre == TK_MINUS)
				inum = NegateParsedInteger(inum);
			int staticVar = funs.AddStaticInt(inum);
			iResultStack.SetIntLiteral(staticVar, inum);
		}
	}
	else
	{
		if (!tk1.empty() && (tk1[0] >= '0' && tk1[0] <= '9'))
            SetCompileError(ar, "invalid or out-of-range integer literal '%s'", tk1.c_str());
        else
            SetParserCompileError(ar, PCE_UNKNOWN_IDENTIFIER, tk1.c_str());
		return false;
	}
	return true;
}

bool ParseStringOrNum(SOperand& iResultStack, TK_TYPE tkTypePre, std::string& tkPre, CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
	if (tkTypePre == TK_QUOTE2 || tkTypePre == TK_QUOTE1)
	{
		std::string str;
		if (false == GetQuotationString(ar, str, tkTypePre == TK_QUOTE2 ? '"' : '\''))
		{
			SetParserCompileError(ar, PCE_UNTERMINATED_STRING);
			return false;
		}
		iResultStack = funs.AddStaticString(str);
		return true;
	}
	else if (tkTypePre == TK_STRING_LITERAL) // define/const 치환으로 완성된 문자열
	{
		iResultStack = funs.AddStaticString(tkPre);
		return true;
	}
	else if (tkTypePre == TK_STRING)
	{
		if (false == ParseNum(iResultStack, TK_NONE, tkPre, ar, funs, vars))
			return false;
		return true;
	}
	return false;
}

TK_TYPE ParseListDef(SOperand& iResultStack, CArchiveRdWC& ar, SFunctions& funs, SVars& vars, int iTableDeep = 0)
{
	std::string tk1;
	TK_TYPE tkType1;

	// // List alloc
	iResultStack = funs._cur->AllocLocalTempVar();
	funs._cur->Push_ListAlloc(ar, iResultStack._iVar);

	int off_cnt = funs._cur->_code->GetBufferOffset() - sizeof(int);

/*	int off_resize = funs._cur->_code->GetBufferOffset();

	// resize (1.param set cnt, 2.call resize)
	funs._cur->Push_MOV(ar, NOP_MOV, COMPILE_CALLARG_VAR_BEGIN + 1 + 0, 0, true);
	int off_cnt = funs._cur->_code->GetBufferOffset() - (sizeof(short) * 2);

	int iStaticString = -1;// funs.AddStaticString("resize");
	funs._cur->Push_CallPtr(ar, iResultStack._iVar, iStaticString, 1); // list resize
	int off_resize_str = funs._cur->_code->GetBufferOffset() - (sizeof(short) * 2);
	*/
	SOperand iTempOffsetValue;
	std::string str;

	int iItemCount = 0;

	bool blEnd = false;
	while (blEnd == false)
	{
		// 빈 리스트 또는 마지막 쉼표 뒤의 종료 토큰은 식으로 파싱하지 않는다.
		tkType1 = GetToken(ar, tk1);
		if (tkType1 == TK_R_ARRAY)
			break;
		ar.PushToken(tkType1, tk1);

		iTempOffsetValue = INVALID_ERROR_PARSEJOB;

		tkType1 = ParseShortCircuitLogic(true, iTempOffsetValue, ar, funs, vars, TK_COMMA, TK_R_ARRAY, TK_NONE, TK_NONE);

		// 여기까지 왔으면 원소가 하나 있어야 한다(빈 리스트와 마지막 쉼표는 위에서 이미 걸렀다).
		// 그런데 값 없이 돌아오면, 예전에는 조용히 break 해서 원소를 버리고 남은 토큰까지
		// 소비하지 않은 채 빠져나갔다. 그러면 상위 파서가 어긋나 "에러 메시지 없는 컴파일 실패"가
		// 된다. 원인을 볼 수 있도록 반드시 에러로 남긴다.
		// 하위 파싱이 실패해도(TK_NONE) 사유가 안 남는 경로가 있었다. 반드시 남기고 전파한다.
		if (tkType1 == TK_NONE || iTempOffsetValue.IsInvalidValue() || iTempOffsetValue.IsNone())
		{
			SetParserCompileError(ar, PCE_EXPECTED_EXPRESSION);
			return TK_NONE;
		}
#if 0
		if (IsShort(iItemCount))
			funs._cur->Push_Table_MASMDP(ar, NOP_CLT_MOV, iResultStack._iVar, iItemCount, iTempOffsetValue._iVar, false, true, iTempOffsetValue.IsShort());
		else
#endif
		{
			MaterializeValue(iTempOffsetValue, ar, funs);

			int iTempOffsetKey = funs.AddStaticInt(iItemCount);
			funs._cur->Push_Table_MASMDP(ar, NOP_CLT_MOV, iResultStack._iVar, iTempOffsetKey, iTempOffsetValue._iVar, false, false, iTempOffsetValue.IsShort());
		}
		iItemCount++;

		if (iTempOffsetValue.IsNone() == false)
		{
		}

		if (tkType1 == TK_R_ARRAY)
			break;
	}
	// 리스트 뒤의 토큰(`;`, `)`, `,` 등)은 소비해서 반환만 하고, 유효성은 호출자가 검증한다.
	// 문장이면 상위에서 `;`를, 함수 인자면 ParseFunCall이 `)`/`,`를 확인한다. 여기서 `;`를
	// 강제하면 리스트 리터럴을 함수 인자로 쓸 때(예: x.append([1,2,3])) 잘못된 에러가 난다.
	tkType1 = GetToken(ar, tk1);
	ar.PushToken(tkType1, tk1);
	if (iItemCount > 0)
	{
	//	iStaticString = funs.AddStaticString("resize");
		*((int*)((u8*)funs._cur->_code->GetData() + off_cnt)) = iItemCount;
	//	*((short*)((u8*)funs._cur->_code->GetData() + off_resize_str)) = iStaticString;
	}
	else
	{
//		funs._cur->_code->SetBufferOffset(off_resize);
//		funs._cur->ClearLastOP();
	}
	return tkType1;
}

TK_TYPE ParseTableDef(SOperand& iResultStack, CArchiveRdWC& ar, SFunctions& funs, SVars& vars, int iTableDeep = 0)
{
	std::string tk1, tk2;
	TK_TYPE tkType1, tkType2;

	// Table alloc
	iResultStack = funs._cur->AllocLocalTempVar();
	funs._cur->Push_TableAlloc(ar, iResultStack._iVar);

	int off_cnt = funs._cur->_code->GetBufferOffset() - sizeof(int);

	/*	int off_reserve = funs._cur->_code->GetBufferOffset();

	// reserve (1.param set cnt, 2.call reserve)
	funs._cur->Push_MOV(ar, NOP_MOV, COMPILE_CALLARG_VAR_BEGIN + 1 + 0, 0, true);
	int off_cnt = funs._cur->_code->GetBufferOffset() - (sizeof(short) * 2);

	int iStaticString = -1;// funs.AddStaticString("reserve");
	funs._cur->Push_CallPtr(ar, iResultStack._iVar, iStaticString, 1); // map reserve
	int off_reserve_str = funs._cur->_code->GetBufferOffset() - (sizeof(short) * 2);
	*/
	SOperand iTempOffsetKey;
	SOperand iTempOffsetValue;
	std::string str;

	int iItemCount = 0;
	int iCurArrayOffset = -1;

	bool blEnd = false;
	while (blEnd == false)
	{
		// 빈 맵 또는 마지막 쉼표 뒤의 종료 토큰은 식으로 파싱하지 않는다.
		tkType1 = GetToken(ar, tk1);
		if (tkType1 == TK_R_MIDDLE)
			break;
		ar.PushToken(tkType1, tk1);

		iTempOffsetKey = iTempOffsetValue = INVALID_ERROR_PARSEJOB;

		tkType1 = ParseShortCircuitLogic(true, iTempOffsetKey, ar, funs, vars, TK_COLON, TK_COMMA, TK_R_MIDDLE, TK_NONE);

		// 리스트 리터럴과 같은 이유로 침묵 break 를 없앤다(위 ParseListDef 주석 참고).
		if (tkType1 == TK_NONE || iTempOffsetKey.IsInvalidValue() || iTempOffsetKey.IsNone())
		{
			SetParserCompileError(ar, PCE_EXPECTED_EXPRESSION);
			return TK_NONE;
		}

		if (tkType1 == TK_COLON)
		{
			// Resolve the key before parsing the value: that expression can mutate
			// its source container. A container operand is not the element itself.
			SnapshotValue(iTempOffsetKey, ar, funs);
			tkType2 = ParseShortCircuitLogic(true, iTempOffsetValue, ar, funs, vars, TK_COMMA, TK_R_MIDDLE, TK_NONE, TK_NONE);
			if (tkType2 == TK_NONE)
			{
				return TK_NONE;
			}
			// ':' 뒤에 식이 없으면 INVALID_ERROR_PARSEJOB(-1)가 그대로 남는다.
			// 이를 코드 생성까지 통과시키면 잘못된 operand를 참조하게 되므로, 키와
			// 동일하게 이 지점에서 문법 오류로 확정한다.
			if (iTempOffsetValue.IsInvalidValue() || iTempOffsetValue.IsNone())
			{
				SetParserCompileError(ar, PCE_EXPECTED_EXPRESSION);
				return TK_NONE;
			}
			MaterializeValue(iTempOffsetValue, ar, funs);
			funs._cur->Push_Table_MASMDP(ar, NOP_CLT_MOV, iResultStack._iVar, iTempOffsetKey._iVar, iTempOffsetValue._iVar, false, iTempOffsetKey.IsShort(), iTempOffsetValue.IsShort());
			tkType1 = tkType2;
			iItemCount++;
		}
		else
		{
			iTempOffsetValue = iTempOffsetKey;
			MaterializeValue(iTempOffsetValue, ar, funs);
			++iCurArrayOffset;
#if 0
			if(IsShort(iCurArrayOffset))
				funs._cur->Push_Table_MASMDP(ar, NOP_CLT_MOV, iResultStack._iVar, iCurArrayOffset, iTempOffsetValue._iVar, false, true, iTempOffsetValue.IsShort());
			else
#endif
			{
				iTempOffsetKey = funs.AddStaticInt(iCurArrayOffset);
				funs._cur->Push_Table_MASMDP(ar, NOP_CLT_MOV, iResultStack._iVar, iTempOffsetKey._iVar, iTempOffsetValue._iVar, false, false, iTempOffsetValue.IsShort());
			}
			iItemCount++;
		}

		if (iTempOffsetKey.IsNone() == false)
		{
		}

		if (tkType1 == TK_R_MIDDLE)
			break;
	}
	// 맵/테이블 뒤의 토큰은 소비해서 반환만 하고, 유효성은 호출자가 검증한다(ParseListDef와 동일).
	// 여기서 `;`를 강제하면 맵 리터럴을 함수 인자로 쓸 때 잘못된 에러가 난다.
	tkType1 = GetToken(ar, tk1);
	ar.PushToken(tkType1, tk1);
	if (iItemCount > 0)
	{
		//iStaticString = funs.AddStaticString("reserve");
		*((int*)((u8*)funs._cur->_code->GetData() + off_cnt)) = iItemCount;
		//*((short*)((u8*)funs._cur->_code->GetData() + off_reserve_str)) = iStaticString;
	}
	else
	{
		//funs._cur->_code->SetBufferOffset(off_reserve);
		//funs._cur->ClearLastOP();
	}
	return tkType1;
}

TK_TYPE ParseTable(SOperand& operand, CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
	TK_TYPE r = TK_NONE;

	SOperand op;
	r = ParseShortCircuitLogic(true, op, ar, funs, vars);
	if (r != TK_R_ARRAY)
	{
		SetParserCompileError(ar, PCE_EXPECTED_RIGHT_BRACKET);
		return TK_NONE;
	}

	/*if (op.IsArray())
	{
		operand = funs._cur->AllocLocalTempVar();
		funs._cur->Push_TableRead(ar, op._iVar, op._iArrayIndex, operand._iVar);
	}
	else*/
		operand = op;
	return r;
}

// 변수와 호출 결과의 후위 선택자를 같은 규칙으로 읽는다.
// 호출 직후 끝내면 L().len(), callback()[i] 에서 다음 선택자가 식 밖에 남는다.
static bool ParsePostfixSelectors(SOperand& operand, CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
	bool lastSelectorWasDot = false;
	while (true)
	{
		std::string token;
		const TK_TYPE type = GetToken(ar, token);
		if (type == TK_L_SMALL)
		{
			ar.PushToken(type, token);
			// 점 뒤의 호출은 컨테이너 메서드이고, 인덱스 뒤의 호출은 읽어 낸 함수값이다.
			if (!lastSelectorWasDot)
				MaterializeValue(operand, ar, funs);
			if (!ParseFunCall(operand, TK_NONE, NULL, ar, funs, vars))
				return false;
			// 호출 결과는 새 값이다. 이전 점 선택자의 메서드 문맥을 이어받지 않는다.
			lastSelectorWasDot = false;
			continue;
		}
		if (type != TK_L_ARRAY && type != TK_DOT)
		{
			ar.PushToken(type, token);
			return true;
		}

		SOperand key;
        const int keyCodeStart = funs._cur->_code->GetBufferOffset();
		if (type == TK_L_ARRAY)
		{
			if (ParseTable(key, ar, funs, vars) != TK_R_ARRAY)
				return false;
			if (key.IsInvalidValue() || key.IsNone())
			{
				SetParserCompileError(ar, PCE_EXPECTED_EXPRESSION);
				return false;
			}
		}
		else
		{
			std::string member;
			if (!GetDotString(ar, member) || member.empty())
			{
				SetParserCompileError(ar, PCE_EXPECTED_MEMBER_NAME);
				return false;
			}
			std::string apiKey;
			key = funs.AddStaticString(CoreApiMethodKey(member, apiKey) ? apiKey : member);
		}

        // 선택자의 값 타입은 소유 컨테이너 타입과 다르다. 다음 읽기의 임시 슬롯에도 구분해서 전달한다.
        SlotType selectedStorage;
        if (ar.m_coreTypedIR && operand._storage.typeId != 0)
        {
            const auto& types = ar.m_coreTypedIR->types;
            const auto& owner = types.Get(operand._storage.typeId);
            NeoCore::TypeId selected = NeoCore::InvalidType;
            switch (owner.kind)
            {
            case NeoCore::TypeKind::Vector2:
            case NeoCore::TypeKind::Vector3:
            case NeoCore::TypeKind::Vector4:
            case NeoCore::TypeKind::Quaternion: selected = types.Float(); break;
            case NeoCore::TypeKind::Array:
            case NeoCore::TypeKind::Array32:
            case NeoCore::TypeKind::List: selected = owner.element; break;
            case NeoCore::TypeKind::Map: selected = owner.value; break;
            case NeoCore::TypeKind::Struct:
                if (key.IsIntLiteral() && key._intLiteral >= 0 &&
                    static_cast<NS_UINT>(key._intLiteral) < owner.fields.size())
                    selected = owner.fields[static_cast<std::size_t>(key._intLiteral)].type;
                break;
            default: break;
            }
            selectedStorage = CoreStorageType(types, selected);
        }
        // Assignment/prefix keys can return a named slot despite having already
        // executed. Preserve their resulting value; only an unevaluated plain
        // variable key keeps the documented late lookup at the eventual write.
        if (funs._cur->_code->GetBufferOffset() != keyCodeStart) SnapshotValue(key, ar, funs);
        else MaterializeValue(key, ar, funs);
        if (operand._assignedResult || (operand.IsArray() && operand._pathKeys.empty()))
        {
            ResolveContainer(operand, ar, funs);
            SOperand child(funs._cur->AllocLocalTempVar());
            child._container.owner = operand._iVar;
            child._container.key = operand._iArrayIndex;
            if (operand._assignedResult)
            {
                ReadElement(operand, child._iVar, ar, funs);
                child._container.loaded = true;
            }
            operand = child;
            operand._iArrayIndex = key._iVar;
        }
        else
        {
            if (!operand.IsArray())
            {
                MaterializeValue(operand, ar, funs);
                operand._pathRoot = operand._iVar;
            }
            else operand._pathOwners.push_back(funs._cur->AllocLocalTempVar());
            operand._pathKeys.push_back(key._iVar);
            operand._pathResolved = false;
            operand._iArrayIndex = key._iVar;
            if (!operand._pathOwners.empty()) operand._iVar = operand._pathOwners.back();
        }
        operand._operandType = key.IsShort() ? Data_TS : Data_TR;
		operand._storage = selectedStorage;
		lastSelectorWasDot = type == TK_DOT;
	}
}

// Read a member in its module's namespace, without substituting a same-named
// const or host define from the importing file.
static bool ReadModuleMember(CArchiveRdWC& ar, std::string& member)
{
	if (GetToken(ar, member) != TK_DOT)
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'.' after module name", member.c_str());
		return false;
	}
	if (GetRawToken(ar, member) != TK_STRING || !AbleName(member))
	{
		SetParserCompileError(ar, PCE_EXPECTED_MEMBER_NAME);
		return false;
	}
	return true;
}

static bool MakeConstOperand(const NeoCompileDefineToken& constant, SOperand& value,
	CArchiveRdWC& ar, SFunctions& funs)
{
	switch (constant.type)
	{
	case NEO_DEFINE_TOKEN_INT:
	case NEO_DEFINE_TOKEN_FLOAT:
	{
		ParsedNumber number;
		if (!StringToNumber(number, constant.text.c_str()))
		{
			SetParserCompileError(ar, PCE_INVALID_NUMBER_LITERAL);
			return false;
		}
		if (constant.type == NEO_DEFINE_TOKEN_INT)
			value.SetIntLiteral(funs.AddStaticInt(number.intValue), number.intValue);
		else
			value.SetFloatLiteral(funs.AddStaticNum(number.floatValue), (NS_FLOAT)number.floatValue);
		break;
	}
	case NEO_DEFINE_TOKEN_STRING:
		value = SOperand(funs.AddStaticString(constant.text));
		break;
	case NEO_DEFINE_TOKEN_TRUE:
	case NEO_DEFINE_TOKEN_FALSE:
		value = SOperand(funs.AddStaticBool(constant.type == NEO_DEFINE_TOKEN_TRUE));
		break;
	case NEO_DEFINE_TOKEN_NULL:
		value = SOperand(funs._cur->AllocLocalTempVar());
		funs._cur->Push_OP1(ar, NOP_VAR_CLEAR, value._iVar);
		break;
	default:
		SetParserCompileError(ar, PCE_CONST_INVALID_VALUE, constant.text.c_str());
		return false;
	}
	// null is materialized in a temporary, so a pool-slot check alone does not
	// protect every constant from assignment and prefix/postfix increment.
	value._readOnly = true;
	return true;
}

// 이름에 붙은 제어값은 컴파일 때만 읽는다. 실행 명령에는 검증된 작은 정수만 남긴다.
static bool ParseCoreControl(std::string& operation, const char* marker, int& value)
{
    const size_t at = operation.rfind(marker);
    if (at == std::string::npos) return true;
    const size_t begin = at + std::strlen(marker);
    if (begin == operation.size()) return false;
    value = 0;
    for (size_t i = begin; i < operation.size(); ++i)
    {
        const int digit = operation[i] - '0';
        if (digit < 0 || digit > 9 || value > (INT_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    operation.erase(at);
    return true;
}

static const AggregateOpInfo* FindCoreAggregate(const std::string& suffix)
{
    unsigned count = 0;
    const AggregateOpInfo* operations = AggregateOperations(count);
    for (unsigned op = 0; op < count; ++op)
    {
        const AggregateOpInfo* info = operations + op;
        if (info && info->suffix[0] && suffix == info->suffix) return info;
    }
    return nullptr;
}

static SOperand EmitCoreAggregate(const AggregateOpInfo& info, const SOperand* arguments, int control,
    CArchiveRdWC& ar, SFunctions& funs, int sourceLine)
{
    const int destination = funs._cur->AllocLocalTempVar();
    if (info.shape == AggregateOpShape::Window)
    {
        // 인자 계산이 모두 끝난 뒤 전용 지역 구간을 확보한다. 네이티브 호출 프레임은 만들지 않는다.
        const int base = funs._cur->AllocTempWindow(info.arity);
        for (int i = 0; i < info.arity; ++i)
            funs._cur->Push_OP2(ar, NOP_MOV, base + i, arguments[i]._iVar, false, sourceLine);
        funs._cur->Push_OP(ar, info.op, destination, base, control, sourceLine);
    }
    else if (info.shape == AggregateOpShape::Binary)
        funs._cur->Push_OP(ar, info.op, destination, arguments[0]._iVar, arguments[1]._iVar, sourceLine);
    else
        funs._cur->Push_OP(ar, info.op, destination, arguments[0]._iVar, control, sourceLine);
    return SOperand(destination);
}

static SOperand EmitCoreAggregate(const std::string& operation, const SOperand* arguments, int control,
    CArchiveRdWC& ar, SFunctions& funs, int sourceLine)
{
    return EmitCoreAggregate(*FindCoreAggregate(operation), arguments, control, ar, funs, sourceLine);
}

static SOperand EmitCoreNumber(OperationCode op, const SOperand& left, const SOperand& right,
    CArchiveRdWC& ar, SFunctions& funs, int sourceLine, int destination = INVALID_ERROR_PARSEJOB)
{
    SlotType type;
    const TypedOpInfo* info = GetTypedOpInfo(op);
    type.kind = info && info->comparison ? SlotKind::Bool :
        PrimaryOperation(op) == NOP_F64_ARITH || op == OP_I64_TO_F64 ? SlotKind::Float : SlotKind::Int;
    type.typeId = static_cast<u32>(type.kind == SlotKind::Bool ? NeoCore::TypeKind::Bool :
        type.kind == SlotKind::Float ? NeoCore::TypeKind::Float : NeoCore::TypeKind::Int);
    if (destination == INVALID_ERROR_PARSEJOB) destination = funs._cur->AllocLocalTempVar(type);
    funs._cur->Push_OP(ar, op, destination, left._iVar, right._iVar, sourceLine);
    return SOperand(destination);
}

// 쓰기 매크로는 컴파일 중에 전용 명령열로 펼친다. 우변과 대상은 이미 원래 순서로 확정되어 있다.
static SOperand EmitCoreWrite(const std::string& family, bool array, bool field, int component, int mode,
    const SOperand* arguments, CArchiveRdWC& ar, SFunctions& funs, int sourceLine)
{
    const bool vector = family[0] == 'v';
    const bool whole = !vector || component == 4;
    SOperand readArguments[] = { arguments[1], arguments[2] };
    SOperand old;
    if (mode != 0 || !whole)
    {
        if (field)
        {
            old = SOperand(funs._cur->AllocLocalTempVar());
            funs._cur->Push_OP(ar, NOP_CLT_READ, arguments[1]._iVar, arguments[2]._iVar, old._iVar, sourceLine);
        }
        else old = EmitCoreAggregate(array ? "a" + family + "get" : "cellget", readArguments, 0, ar, funs, sourceLine);
    }
    SOperand oldValue = old;
    if (!whole) oldValue = EmitCoreAggregate(family + "get", &old, component, ar, funs, sourceLine);
    SOperand value = arguments[0];
    if (mode != 0)
    {
        if (vector && whole)
        {
            static const char* operations[] = { "", "add", "sub", "mul", "div" };
            SOperand values[] = { oldValue, value };
            value = EmitCoreAggregate(family + operations[mode], values, 0, ar, funs, sourceLine);
        }
        else
        {
            static const OperationCode integers[] = { NOP_NONE, OP_I64_ADD, OP_I64_SUB, OP_I64_MUL,
                OP_I64_DIV, OP_I64_ADD, OP_I64_SUB, OP_I64_MOD, OP_I64_AND, OP_I64_OR,
                OP_I64_XOR, OP_I64_SHL, OP_I64_SHR };
            static const OperationCode floats[] = { NOP_NONE, OP_F64_ADD, OP_F64_SUB, OP_F64_MUL,
                OP_F64_DIV, OP_F64_ADD, OP_F64_SUB };
            value = EmitCoreNumber(family == "i64" || family == "i32" ? integers[mode] : floats[mode], oldValue, value,
                ar, funs, sourceLine);
        }
    }
    SOperand stored = value;
    if (!whole)
    {
        SOperand replacement[] = { old, value };
        stored = EmitCoreAggregate(family + "set", replacement, component, ar, funs, sourceLine);
    }
    SOperand writeArguments[] = { stored, arguments[1], arguments[2] };
    SOperand result = stored;
    if (field)
        funs._cur->Push_OP(ar, NOP_CLT_MOV, arguments[1]._iVar, arguments[2]._iVar, stored._iVar, sourceLine);
    else result = EmitCoreAggregate(array ? "a" + family + "set" : "cellset", writeArguments,
        0, ar, funs, sourceLine);
    if (mode == 5 || mode == 6) return oldValue;
    return whole ? result : EmitCoreAggregate(family + "get", &result, component, ar, funs, sourceLine);
}

static bool IsIdentityAggregateOperation(OperationCode op)
{
    switch (op)
    {
#define NEOCORE_IDENTITY_CASE(OP, NAME, SUFFIX, ARITY, SHAPE, DIMENSION, CONTROL) case OP: return true;
        NEOCORE_IDENTITY_OPS(NEOCORE_IDENTITY_CASE)
#undef NEOCORE_IDENTITY_CASE
    default: return false;
    }
}

// 타입 검사기가 남긴 내부 식을 호출 프레임 없이 전용 명령으로 방출한다.
// 접두사는 현재 컴파일 요청에만 유효하며 원본 식의 줄 번호도 이름에 보존한다.
static bool ParseCoreTypedIntrinsic(SOperand& result, const std::string& name,
    CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
    const std::string suffix = name.substr(ar.m_coreIntrinsicPrefix.size());
    const size_t lineAt = suffix.rfind("_L");
    const TypedOpInfo* info = nullptr;
    const AggregateOpInfo* aggregate = nullptr;
    bool swapOperands = false;
    bool invert = false;
    bool arrayWrite = false;
    bool fieldWrite = false;
    bool collectionSet = false;
    int component = -1;
    int mode = -1;
    std::string writeFamily;
    if (lineAt != std::string::npos)
    {
        std::string operation = suffix.substr(0, lineAt);
        if (!ParseCoreControl(operation, "_M", mode) || !ParseCoreControl(operation, "_C", component))
            operation.clear();
        if (mode >= 0)
        {
            for (int dimension = 2; dimension <= 4; ++dimension)
            {
                const std::string family = "v" + std::to_string(dimension);
                if (operation == family + "cellwrite" || operation == family + "fieldwrite" || operation == "a" + family + "write")
                {
                    if ((component >= 0 && component < dimension && mode <= 6) || (component == 4 && mode <= 4))
                    { writeFamily = family; arrayWrite = operation[0] == 'a'; fieldWrite = operation == family + "fieldwrite"; }
                }
            }
            if (component == -1 && (operation == "ai64write" || operation == "af64write" ||
                operation == "ai32write" || operation == "af32write" ||
                operation == "ci64write" || operation == "cf64write") &&
                mode <= (operation[1] == 'i' ? 12 : 6))
            {
                writeFamily = operation.substr(1, 3);
                arrayWrite = operation[0] == 'a';
                fieldWrite = operation[0] == 'c';
            }
        }
        else
        {
            collectionSet = operation == "cset" && component == -1;
            for (int dimension = 2; dimension <= 4; ++dimension)
                if (operation == "v" + std::to_string(dimension) + "rmul")
                { operation = "v" + std::to_string(dimension) + "mul"; swapOperands = true; break; }
            aggregate = FindCoreAggregate(operation);
            if (aggregate && (aggregate->controlMax ? component < 0 || component > aggregate->controlMax : component != -1))
                aggregate = nullptr;
        }
        unsigned operationCount = 0;
        const TypedOpInfo* operations = TypedOperations(operationCount);
        for (unsigned op = 0; op < operationCount; ++op)
        {
            const TypedOpInfo* candidate = operations + op;
            if (mode == -1 && component == -1 && candidate && !candidate->jump && operation == candidate->suffix) { info = candidate; break; }
        }
    }
    int sourceLine = 0;
    if (info || aggregate || !writeFamily.empty() || collectionSet)
    {
        for (size_t i = lineAt + 2; i < suffix.size(); ++i)
        {
            const int digit = suffix[i] - '0';
            if (digit < 0 || digit > 9 || sourceLine > (INT_MAX - digit) / 10)
            { info = nullptr; aggregate = nullptr; writeFamily.clear(); collectionSet = false; break; }
            sourceLine = sourceLine * 10 + digit;
        }
    }
    if ((!info && !aggregate && writeFamily.empty() && !collectionSet) || sourceLine <= 0)
    {
        SetCompileError(ar, "invalid typed intrinsic '%s'", name.c_str());
        return false;
    }
    std::string token;
    if (GetToken(ar, token) != TK_L_SMALL)
    {
        SetParserCompileError(ar, PCE_EXPECTED_LEFT_PAREN);
        return false;
    }
    std::vector<int> effects;
    const bool scanned = ScanCallArguments(ar, effects);
    const int arity = info ? info->arity : aggregate ? aggregate->arity : (arrayWrite || fieldWrite || collectionSet) ? 3 : 2;
    SOperand arguments[4];
    for (int i = 0; i < arity; ++i)
    {
        // 선스캔에서 얻은 현재 인자의 부작용을 재사용하여 같은 부분식을 다시 훑지 않는다.
        funs._presetExprEffects = scanned && static_cast<size_t>(i) < effects.size() ? effects[i] : EXPR_EFFECT_ALL;
        funs._presetExprOffset = ar.GetBufferOffset();
        const TK_TYPE end = ParseShortCircuitLogic(true, arguments[i], ar, funs, vars);
        funs._presetExprEffects = -1;
        if (arguments[i].IsInvalidValue() || arguments[i].IsNone() ||
            (arguments[i].IsFun() && (!collectionSet || i != 0)) ||
            end != (i + 1 == arity ? TK_R_SMALL : TK_COMMA))
        {
            SetParserCompileError(ar, PCE_INVALID_CALL_ARGUMENT);
            return false;
        }
        // 다음 인자가 바꾸기 전에 현재 값과 선택자 읽기를 확정한다.
        MaterializeValue(arguments[i], ar, funs);
        int laterEffects = scanned ? 0 : EXPR_EFFECT_ALL;
        for (size_t later = static_cast<size_t>(i) + 1; later < effects.size(); ++later)
            laterEffects |= effects[later];
        const bool identityInput = aggregate && IsIdentityAggregateOperation(aggregate->op);
        if ((aggregate && !identityInput) || !writeFamily.empty() ||
            NeedsSnapshot(arguments[i], laterEffects)) SnapshotValue(arguments[i], ar, funs);
    }
    if (collectionSet)
    {
        // 뒤 인자가 값을 바꿀 때만 보존했다. 저장 결과도 같은 값이며 원소 포인터는 보관하지 않는다.
        funs._cur->Push_OP(ar, NOP_CLT_MOV, arguments[1]._iVar, arguments[2]._iVar, arguments[0]._iVar, sourceLine);
        result = arguments[0];
        return true;
    }
    if (!writeFamily.empty())
    {
        result = EmitCoreWrite(writeFamily, arrayWrite, fieldWrite, component, mode, arguments, ar, funs, sourceLine);
        return true;
    }
    if (aggregate)
    {
        if (swapOperands) std::swap(arguments[0], arguments[1]);
        result = EmitCoreAggregate(*aggregate, arguments, component < 0 ? 0 : component, ar, funs, sourceLine);
        return true;
    }
    if (info->op == OP_I64_TO_F64 && arguments[0].IsIntLiteral())
    {
        const NS_FLOAT value = static_cast<NS_FLOAT>(arguments[0]._intLiteral);
        result.SetFloatLiteral(funs.AddStaticNum(value), value);
        return true;
    }
    if (info->op == OP_F64_TO_I64 && arguments[0].IsFloatLiteral())
    {
        const NS_INT value = NeoFloatToInt(arguments[0]._floatLiteral);
        result.SetIntLiteral(funs.AddStaticInt(value), value);
        return true;
    }
    if (info->arity == 2 && TryFoldLiteralBinOp(arguments[0], TypedBinaryOp(info->op), arguments[1], funs))
    {
        result = arguments[0];
        return true;
    }
    result = SOperand(funs._cur->AllocLocalTempVar());
    if (swapOperands) std::swap(arguments[0], arguments[1]);
    if (info->arity == 1)
        funs._cur->Push_OP2(ar, info->op, result._iVar, arguments[0]._iVar, false, sourceLine);
    else
        funs._cur->Push_OP(ar, info->op, result._iVar, arguments[0]._iVar, arguments[1]._iVar, sourceLine);
    if (invert) funs._cur->GetOPPointer(funs._cur->_iLastOPOffset)->argFlag |= NEOS_OP_COMPARE_INVERT;
    return true;
}

// IR은 컴파일 요청의 구조화된 입력이다. 전용 명령을 내기 전에 그래프와 타입 계약을 검사한다.
class CoreIRValidator
{
    CArchiveRdWC& ar_;
    const NeoCore::TypedIR& ir_;
    std::vector<unsigned char> states_;
    std::vector<int> heights_;
    std::vector<unsigned char>& effects_;

    bool Fail(std::size_t index, const char* message)
    {
        SetCompileError(ar_, "invalid typed IR node %llu: %s", static_cast<unsigned long long>(index), message);
        return false;
    }
    bool ValidSpan(const NeoCore::IRSourceSpan& source) const
    { return source.line >= 1 && source.column >= 1 && source.begin <= source.end; }
    bool ValidType(const NeoCore::IRNode& node) const
    {
        using NeoCore::IRValueType;
        using NeoCore::TypeKind;
        switch (ir_.types.Get(node.typeId).kind)
        {
        case TypeKind::Invalid: return false;
        case TypeKind::Void: return node.type == IRValueType::Void;
        case TypeKind::Int: return node.type == IRValueType::Int;
        case TypeKind::Float: return node.type == IRValueType::Float;
        case TypeKind::Bool: return node.type == IRValueType::Bool;
        case TypeKind::String: return node.type == IRValueType::String;
        default: return node.type == IRValueType::Value;
        }
    }
    bool Visit(std::size_t index, int depth)
    {
        using NeoCore::IROp;
        using NeoCore::IRValueType;
        if (index >= ir_.nodes.size()) return Fail(index, "operand index is out of range");
        if (depth > 128) return Fail(index, "expression nesting exceeds limit");
        if (states_[index] == 1) return Fail(index, "cyclic expression graph");
        if (states_[index] == 2)
            return depth + heights_[index] - 1 <= 128 || Fail(index, "expression nesting exceeds limit");
        states_[index] = 1;
        const NeoCore::IRNode& node = ir_.nodes[index];
        if (!ValidType(node)) return Fail(index, "unknown or inconsistent value type");
        if (!ValidSpan(node.source)) return Fail(index, "invalid source span");
        std::size_t arity = 0;
        switch (node.op)
        {
        case IROp::IntLiteral: case IROp::FloatLiteral: case IROp::BoolLiteral:
        case IROp::Symbol: case IROp::LegacyExpression: case IROp::ApiCall: arity = 0; break;
        case IROp::Add: case IROp::Subtract: case IROp::Multiply: case IROp::Divide:
        case IROp::Remainder: case IROp::BitAnd: case IROp::BitOr: case IROp::BitXor:
        case IROp::ShiftLeft: case IROp::ShiftRight: case IROp::Equal: case IROp::NotEqual:
        case IROp::Less: case IROp::LessEqual: case IROp::Greater: case IROp::GreaterEqual:
            arity = 2; break;
        case IROp::Negate: case IROp::BitNot: case IROp::LogicalNot: case IROp::ToFloat: case IROp::ToInt:
            arity = 1; break;
        case IROp::LoadStorage: arity = 0; break;
        case IROp::Assign: case IROp::AddAssign: case IROp::SubtractAssign:
        case IROp::MultiplyAssign: case IROp::DivideAssign: case IROp::RemainderAssign:
        case IROp::BitAndAssign: case IROp::BitOrAssign: case IROp::BitXorAssign:
        case IROp::ShiftLeftAssign: case IROp::ShiftRightAssign:
            arity = 1; break;
        case IROp::PreIncrement: case IROp::PreDecrement:
        case IROp::PostIncrement: case IROp::PostDecrement:
            arity = 0; break;
        case IROp::ScriptCall:
            if (node.functionIndex >= ir_.functions.size()) return Fail(index, "function index is out of range");
            arity = ir_.functions[node.functionIndex].parameterTypes.size(); break;
        case IROp::NullableConvert: case IROp::CopyStruct: arity = 1; break;
        case IROp::MapRead: arity = 2; break;
        case IROp::StringLiteral: arity = 0; break;
        case IROp::Conditional: arity = 3; break;
        case IROp::CollectionRead: arity = 2; break;
        case IROp::CollectionWrite: arity = 3; break;
        default: return Fail(index, "unknown operation");
        }
        if (node.op != IROp::ScriptCall && node.functionIndex != static_cast<std::size_t>(-1))
            return Fail(index, "operation does not accept function metadata");
        if (node.inputs.size() != arity) return Fail(index, "incorrect operand count");
        int height = 1;
        const bool storage = node.op >= IROp::LoadStorage && node.op <= IROp::PostDecrement;
        if (storage)
        {
            const NeoCore::IRStorageTarget& target = node.storage;
            if ((target.kind != NeoCore::IRStorageKind::Variable && target.kind != NeoCore::IRStorageKind::Cell) ||
                !AbleBackendName(ar_, target.backendName) || target.symbolId == 0)
                return Fail(index, "invalid storage target");
            if ((target.kind == NeoCore::IRStorageKind::Cell && !target.fields.empty()) || target.fields.size() > 128)
                return Fail(index, "invalid storage field path");
            for (std::size_t field : target.fields)
                if (field > static_cast<std::size_t>(INT_MAX)) return Fail(index, "storage field index is out of range");
        }
        else if (node.storage.kind != NeoCore::IRStorageKind::Invalid || !node.storage.backendName.empty() ||
            node.storage.symbolId != 0 || !node.storage.fields.empty())
            return Fail(index, "operation does not accept storage metadata");
        if (node.discardResult && node.op != IROp::CollectionWrite && (!storage || node.op == IROp::LoadStorage))
            return Fail(index, "operation does not accept discard metadata");
        // Out 인자는 API 호출 노드에만 있다. 대상은 필드 경로가 없는 변수나 셀이어야 한다.
        if (node.op != IROp::ApiCall && !node.outputs.empty()) return Fail(index, "operation does not accept output arguments");
        for (const NeoCore::IROutput& output : node.outputs)
        {
            const NeoCore::IRStorageTarget& target = output.storage;
            if ((target.kind != NeoCore::IRStorageKind::Variable && target.kind != NeoCore::IRStorageKind::Cell) ||
                !target.fields.empty() || target.symbolId == 0 || !AbleBackendName(ar_, target.backendName) ||
                output.parameter >= 32u || !ir_.types.IsStorable(output.typeId) ||
                (!output.cloneHelper.empty() && !AbleBackendName(ar_, output.cloneHelper)) ||
                (ir_.types.Get(output.typeId).kind == NeoCore::TypeKind::Struct) == output.cloneHelper.empty())
                return Fail(index, "invalid output argument");
        }
        unsigned char effects = node.op == IROp::LegacyExpression || node.op == IROp::ApiCall ? EXPR_EFFECT_ALL
            : node.op == IROp::ScriptCall || node.op == IROp::CopyStruct ? EXPR_EFFECT_CALL
            : (storage && node.op != IROp::LoadStorage) || node.op == IROp::CollectionWrite ? EXPR_EFFECT_ASSIGN : 0;
        for (std::size_t input : node.inputs)
        {
            if (!Visit(input, depth + 1)) return false;
            height = (std::max)(height, heights_[input] + 1);
            effects |= effects_[input];
        }
        const IRValueType input = arity ? ir_.nodes[node.inputs[0]].type : IRValueType::Invalid;
        const NeoCore::TypeId inputType = arity ? ir_.nodes[node.inputs[0]].typeId : NeoCore::InvalidType;
        const bool numeric = input == IRValueType::Int || input == IRValueType::Float;
        if (arity == 2 && node.op != IROp::ScriptCall && node.op != IROp::MapRead && node.op != IROp::CollectionRead &&
            inputType != ir_.nodes[node.inputs[1]].typeId)
            return Fail(index, "operand types differ");
        bool valid = false;
        switch (node.op)
        {
        case IROp::IntLiteral: valid = node.type == IRValueType::Int; break;
        case IROp::FloatLiteral: valid = node.type == IRValueType::Float; break;
        case IROp::BoolLiteral: valid = node.type == IRValueType::Bool; break;
        case IROp::Symbol:
            valid = AbleBackendName(ar_, node.backendName) && node.type != IRValueType::Void; break;
        case IROp::LegacyExpression:
            valid = !node.legacySource.empty() && node.type != IRValueType::Void; break;
        case IROp::ApiCall:
            valid = !node.legacySource.empty() && !node.outputs.empty(); break;
        case IROp::Add: case IROp::Subtract: case IROp::Multiply: case IROp::Divide:
            valid = numeric && node.typeId == inputType; break;
        case IROp::Remainder: case IROp::BitAnd: case IROp::BitOr: case IROp::BitXor:
        case IROp::ShiftLeft: case IROp::ShiftRight:
            valid = input == IRValueType::Int && node.type == IRValueType::Int; break;
        case IROp::Equal: case IROp::NotEqual: case IROp::Less: case IROp::LessEqual:
        case IROp::Greater: case IROp::GreaterEqual:
            valid = numeric && node.type == IRValueType::Bool; break;
        case IROp::Negate: valid = numeric && node.typeId == inputType; break;
        case IROp::BitNot: valid = input == IRValueType::Int && node.type == IRValueType::Int; break;
        case IROp::LogicalNot: valid = input == IRValueType::Bool && node.type == IRValueType::Bool; break;
        case IROp::ToFloat: valid = input == IRValueType::Int && node.type == IRValueType::Float; break;
        case IROp::ToInt: valid = input == IRValueType::Float && node.type == IRValueType::Int; break;
        case IROp::LoadStorage:
            valid = node.type != IRValueType::Void; break;
        case IROp::Assign: valid = node.typeId == inputType && input != IRValueType::Void; break;
        case IROp::AddAssign: case IROp::SubtractAssign: case IROp::MultiplyAssign: case IROp::DivideAssign:
            valid = numeric && node.typeId == inputType; break;
        case IROp::RemainderAssign: case IROp::BitAndAssign: case IROp::BitOrAssign: case IROp::BitXorAssign:
        case IROp::ShiftLeftAssign: case IROp::ShiftRightAssign:
            valid = input == IRValueType::Int && node.type == IRValueType::Int; break;
        case IROp::PreIncrement: case IROp::PreDecrement: case IROp::PostIncrement: case IROp::PostDecrement:
            valid = node.type == IRValueType::Int || node.type == IRValueType::Float; break;
        case IROp::ScriptCall:
        {
            const NeoCore::IRFunction& function = ir_.functions[node.functionIndex];
            valid = node.typeId == function.returnType;
            for (std::size_t i = 0; valid && i < node.inputs.size(); ++i)
                valid = ir_.nodes[node.inputs[i]].typeId == function.parameterTypes[i];
            break;
        }
        case IROp::NullableConvert:
            valid = ir_.types.AllowsNull(node.typeId) &&
                (inputType == ir_.types.Null() || inputType == node.typeId); break;
        case IROp::CopyStruct:
            valid = ir_.types.Get(node.typeId).kind == NeoCore::TypeKind::Struct &&
                node.typeId == inputType && AbleBackendName(ar_, node.backendName); break;
        case IROp::MapRead:
        {
            const NeoCore::TypeDesc& map = ir_.types.Get(inputType);
            valid = map.kind == NeoCore::TypeKind::Map && map.value == node.typeId &&
                (node.type == IRValueType::Int || node.type == IRValueType::Float) &&
                (map.key == ir_.types.Int() || map.key == ir_.types.String()) &&
                map.key == ir_.nodes[node.inputs[1]].typeId;
            break;
        }
        case IROp::StringLiteral:
            valid = node.type == IRValueType::String && node.text.find('\0') == std::string::npos; break;
        case IROp::Conditional:
            valid = input == IRValueType::Bool && node.type != IRValueType::Void &&
                ir_.nodes[node.inputs[1]].typeId == node.typeId && ir_.nodes[node.inputs[2]].typeId == node.typeId;
            break;
        case IROp::CollectionRead:
        case IROp::CollectionWrite:
        {
            const bool write = node.op == IROp::CollectionWrite;
            const std::size_t receiver = write ? 1 : 0;
            const NeoCore::TypeDesc& owner = ir_.types.Get(ir_.nodes[node.inputs[receiver]].typeId);
            const bool map = owner.kind == NeoCore::TypeKind::Map;
            const NeoCore::TypeId element = map ? owner.value : owner.element;
            valid = (map || owner.kind == NeoCore::TypeKind::List || owner.kind == NeoCore::TypeKind::Array || owner.kind == NeoCore::TypeKind::Array32) &&
                element == node.typeId && (node.type == IRValueType::Int || node.type == IRValueType::Float || node.type == IRValueType::Bool) &&
                ir_.nodes[node.inputs[receiver + 1]].typeId == (map ? owner.key : ir_.types.Int());
            if (write)
            {
                valid = valid && inputType == node.typeId &&
                    (node.text == "=" || node.text == "+=" || node.text == "-=" || node.text == "*=" || node.text == "/=" ||
                     node.text == "%=" || node.text == "&=" || node.text == "|=" || node.text == "^=" || node.text == "<<=" || node.text == ">>=");
                if (node.text != "=") valid = valid && node.type != IRValueType::Bool;
                if (node.type == IRValueType::Float) valid = valid &&
                    (node.text == "=" || node.text == "+=" || node.text == "-=" || node.text == "*=" || node.text == "/=");
            }
            break;
        }
        default: break;
        }
        if (!valid) return Fail(index, "operation type contract does not match");
        states_[index] = 2;
        heights_[index] = height;
        effects_[index] = effects;
        return true;
    }
public:
    CoreIRValidator(CArchiveRdWC& ar, const NeoCore::TypedIR& ir, std::vector<unsigned char>& effects)
        : ar_(ar), ir_(ir), states_(ir.nodes.size(), 0), heights_(ir.nodes.size(), 0), effects_(effects)
    { effects_.assign(ir.nodes.size(), 0); }
    bool Validate()
    {
        if (!ir_.nodes.empty() && (ir_.prefix.empty() || !AbleBackendName(ar_, ir_.prefix)))
        { SetCompileError(ar_, "invalid typed IR marker prefix"); return false; }
        if (!ir_.returns.empty() && (ir_.returnPrefix.empty() || !AbleBackendName(ar_, ir_.returnPrefix) || ir_.returnPrefix == ir_.prefix))
        { SetCompileError(ar_, "invalid typed IR return marker prefix"); return false; }
        std::set<std::string> names;
        for (const NeoCore::IRFunction& function : ir_.functions)
        {
            bool valid = AbleBackendName(ar_, function.backendName) && names.insert(function.backendName).second &&
                (function.returnType == ir_.types.Void() || ir_.types.IsStorable(function.returnType)) &&
                function.parameterTypes.size() <= COMPILE_MAX_ARGUMENTS;
            for (NeoCore::TypeId parameter : function.parameterTypes)
                valid = valid && ir_.types.IsStorable(parameter);
            if (!valid) { SetCompileError(ar_, "invalid typed IR function signature"); return false; }
        }
        for (std::size_t i = 0; i < ir_.nodes.size(); ++i)
            if (!Visit(i, 1)) return false;
        for (const NeoCore::IRReturn& statement : ir_.returns)
        {
            if (!ValidSpan(statement.source) || statement.functionIndex >= ir_.functions.size())
            { SetCompileError(ar_, "invalid typed IR return metadata"); return false; }
            const NeoCore::TypeId expected = ir_.functions[statement.functionIndex].returnType;
            const bool noValue = statement.valueIndex == static_cast<std::size_t>(-1);
            if ((noValue && expected != ir_.types.Void()) || (!noValue &&
                (statement.valueIndex >= ir_.nodes.size() || expected == ir_.types.Void() ||
                    ir_.nodes[statement.valueIndex].typeId != expected)))
            { SetCompileError(ar_, "invalid typed IR return type contract"); return false; }
        }
        return true;
    }
};

static OperationCode CoreIRNumberOp(NeoCore::IROp operation, NeoCore::IRValueType type)
{
    using NeoCore::IROp;
    const bool floating = type == NeoCore::IRValueType::Float;
    switch (operation)
    {
    case IROp::Add: return floating ? OP_F64_ADD : OP_I64_ADD;
    case IROp::Subtract: return floating ? OP_F64_SUB : OP_I64_SUB;
    case IROp::Multiply: return floating ? OP_F64_MUL : OP_I64_MUL;
    case IROp::Divide: return floating ? OP_F64_DIV : OP_I64_DIV;
    case IROp::Remainder: return OP_I64_MOD;
    case IROp::BitAnd: return OP_I64_AND;
    case IROp::BitOr: return OP_I64_OR;
    case IROp::BitXor: return OP_I64_XOR;
    case IROp::ShiftLeft: return OP_I64_SHL;
    case IROp::ShiftRight: return OP_I64_SHR;
    case IROp::Equal: return floating ? OP_F64_EQ : OP_I64_EQ;
    case IROp::NotEqual: return floating ? OP_F64_NE : OP_I64_NE;
    case IROp::Less: return floating ? OP_F64_LT : OP_I64_LT;
    case IROp::Greater: return floating ? OP_F64_GT : OP_I64_GT;
    case IROp::LessEqual: return floating ? OP_F64_LE : OP_I64_LE;
    case IROp::GreaterEqual: return floating ? OP_F64_GE : OP_I64_GE;
    case IROp::ToFloat: return OP_I64_TO_F64;
    case IROp::ToInt: return OP_F64_TO_I64;
    default: return NOP_NONE;
    }
}

static OperationCode CoreIRWriteOp(NeoCore::IROp operation, NeoCore::IRValueType type)
{
    using NeoCore::IROp;
    switch (operation)
    {
    case IROp::AddAssign: return CoreIRNumberOp(IROp::Add, type);
    case IROp::SubtractAssign: return CoreIRNumberOp(IROp::Subtract, type);
    case IROp::MultiplyAssign: return CoreIRNumberOp(IROp::Multiply, type);
    case IROp::DivideAssign: return CoreIRNumberOp(IROp::Divide, type);
    case IROp::RemainderAssign: return CoreIRNumberOp(IROp::Remainder, type);
    case IROp::BitAndAssign: return CoreIRNumberOp(IROp::BitAnd, type);
    case IROp::BitOrAssign: return CoreIRNumberOp(IROp::BitOr, type);
    case IROp::BitXorAssign: return CoreIRNumberOp(IROp::BitXor, type);
    case IROp::ShiftLeftAssign: return CoreIRNumberOp(IROp::ShiftLeft, type);
    case IROp::ShiftRightAssign: return CoreIRNumberOp(IROp::ShiftRight, type);
    case IROp::PreIncrement: return CoreIRNumberOp(IROp::Add, type);
    case IROp::PreDecrement: return CoreIRNumberOp(IROp::Subtract, type);
    case IROp::PostIncrement: return CoreIRNumberOp(IROp::Add, type);
    case IROp::PostDecrement: return CoreIRNumberOp(IROp::Subtract, type);
    default: return NOP_NONE;
    }
}

static SOperand SnapshotCoreIRValue(SOperand value, CArchiveRdWC& ar, SFunctions& funs, int line)
{
    MaterializeValue(value, ar, funs);
    if (!IsTempVar(value._iVar) && !IsCompileConstant(value._iVar))
    {
        const int saved = funs._cur->AllocLocalTempVar(value._storage);
        funs._cur->Push_OP2(ar, NOP_MOV, saved, value._iVar, false, line);
        return SOperand(saved, value._storage);
    }
    return value;
}

static bool ResolveCoreIRStorage(const NeoCore::IRStorageTarget& storage, int line, SOperand& target,
    CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
    target = SOperand(FindReadableVar(funs, vars, storage.backendName));
    if (target.IsInvalidValue() || target.IsNone() || IsTempVar(target._iVar) || IsCompileConstant(target._iVar))
    {
        SetCompileError(ar, "unresolved or readonly typed IR storage '%s'", storage.backendName.c_str());
        return false;
    }
    // 구조체는 평면 값 목록이다. 우변이 끝난 뒤 마지막 필드를 소유한 목록까지 다시 찾는다.
    for (std::size_t i = 0; i + 1 < storage.fields.size(); ++i)
    {
        const int value = funs._cur->AllocLocalTempVar();
        funs._cur->Push_OP(ar, NOP_CLT_READ, target._iVar,
            funs.AddStaticInt(static_cast<NS_INT>(storage.fields[i])), value, line);
        target = SOperand(value);
    }
    return true;
}
static bool ResolveCoreIRStorage(const NeoCore::IRNode& node, SOperand& target,
    CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
    return ResolveCoreIRStorage(node.storage, node.source.line, target, ar, funs, vars);
}

static SOperand ReadCoreIRStorage(const NeoCore::IRNode& node, const SOperand& target,
    CArchiveRdWC& ar, SFunctions& funs)
{
    if (!node.storage.fields.empty())
    {
        const int value = funs._cur->AllocLocalTempVar();
        const int key = funs.AddStaticInt(static_cast<NS_INT>(node.storage.fields.back()));
        const OperationCode op = node.type == NeoCore::IRValueType::Int ? NOP_LIST_I64_LOAD :
            node.type == NeoCore::IRValueType::Float ? NOP_LIST_F64_LOAD :
            node.type == NeoCore::IRValueType::Bool ? NOP_LIST_BOOL_LOAD : NOP_NONE;
        if (op != NOP_NONE) funs._cur->Push_OP(ar, op, value, target._iVar, key, node.source.line);
        else funs._cur->Push_OP(ar, NOP_CLT_READ, target._iVar, key, value, node.source.line);
        return SOperand(value);
    }
    return node.storage.kind == NeoCore::IRStorageKind::Cell
        ? EmitCoreAggregate("cellget", &target, 0, ar, funs, node.source.line) : target;
}

static SlotType CoreNumericStorage(NeoCore::IRValueType value)
{
    SlotType type;
    switch (value)
    {
    case NeoCore::IRValueType::Int: type.kind = SlotKind::Int; type.typeId = static_cast<u32>(NeoCore::TypeKind::Int); break;
    case NeoCore::IRValueType::Float: type.kind = SlotKind::Float; type.typeId = static_cast<u32>(NeoCore::TypeKind::Float); break;
    case NeoCore::IRValueType::Bool: type.kind = SlotKind::Bool; type.typeId = static_cast<u32>(NeoCore::TypeKind::Bool); break;
    default: break;
    }
    return type;
}

static bool EmitCoreIRNode(std::size_t index, SOperand& result,
    CArchiveRdWC& ar, SFunctions& funs, SVars& vars, int destination = INVALID_ERROR_PARSEJOB);

static bool EmitCoreIRMapInputs(const NeoCore::IRNode& node, SOperand (&arguments)[2],
    CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
    if (!EmitCoreIRNode(node.inputs[0], arguments[0], ar, funs, vars)) return false;
    MaterializeValue(arguments[0], ar, funs);
    // 키 계산이 맵 변수를 바꾸더라도 처음 읽은 수신 맵에 접근한다.
    if (NeedsSnapshot(arguments[0], (*ar.m_coreIREffects)[node.inputs[1]]))
        SnapshotValue(arguments[0], ar, funs);
    if (!EmitCoreIRNode(node.inputs[1], arguments[1], ar, funs, vars)) return false;
    MaterializeValue(arguments[1], ar, funs);
    return true;
}

// 호출 인자는 순서대로 평가한 뒤 한꺼번에 인자 창으로 옮긴다. 중첩 호출이 같은 창을 재사용한다.
static bool EmitCoreIRCall(const NeoCore::IRNode& node, const std::string& name, SOperand& result,
    CArchiveRdWC& ar, SFunctions& funs, SVars& vars, int destination = INVALID_ERROR_PARSEJOB)
{
    if (funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME && !ar._allowGlobalInitLogic)
    { SetParserCompileError(ar, PCE_GLOBAL_CALL_NOT_ALLOWED); return false; }
    SFunctionInfo* function = funs.FindFun(name);
    if (!function || function->_funType == FUNT_BUILT_IN || function->_args.size() != node.inputs.size())
    { SetCompileError(ar, "unresolved or inconsistent typed IR function '%s'", name.c_str()); return false; }
    const std::size_t count = node.inputs.size();
    CheckCompileRange("call arguments", static_cast<int64_t>(count), 0, COMPILE_MAX_ARGUMENTS);
    // 뒤쪽 인자에 호출이 없으면(EXPR_EFFECT_CALL 없음) 그 인자들은 임시를 거치지 않고 창 슬롯에 바로 만든다. 앞쪽
    // 인자는 중첩 호출이 같은 창을 다시 쓰므로 임시에 두었다가 계획을 고른 뒤 옮긴다. 평가 순서는 앞 인자 전부 → 뒤
    // 인자 순서 그대로이고, 뒤 인자는 값이 생기는 즉시 창에 놓이므로 더 뒤의 인자가 바꾸는 변수도 따로 복사하지 않는다.
    std::size_t directBegin = count;
    while (directBegin > 0 && ((*ar.m_coreIREffects)[node.inputs[directBegin - 1]] & EXPR_EFFECT_CALL) == 0) --directBegin;
    std::vector<SOperand> arguments;
    std::vector<int> laterEffects(count, 0);
    int effects = 0;
    for (std::size_t i = count; i > 0; --i)
    {
        laterEffects[i - 1] = effects;
        effects |= (*ar.m_coreIREffects)[node.inputs[i - 1]];
    }
    for (std::size_t i = 0; i < directBegin; ++i)
    {
        SOperand value;
        if (!EmitCoreIRNode(node.inputs[i], value, ar, funs, vars)) return false;
        MaterializeValue(value, ar, funs);
        if (NeedsSnapshot(value, laterEffects[i])) value = SnapshotCoreIRValue(value, ar, funs, node.source.line);
        arguments.push_back(value);

    }
    // 몸통이 전역 RETURN 하나인 함수의 인자 없는 호출은 호출 창 없이 그 전역을 복사한다.
    if (function->_globalGetterSlot != 0 && count == 0)
    {
        const bool directCopy = !ar._debug && destination != INVALID_ERROR_PARSEJOB &&
            ((destination > 0 && destination < COMPILE_LOCALTMP_VAR_BEGIN) || destination <= COMPILE_GLOBAL_VAR_BEGIN);
        result = SOperand(directCopy ? destination : funs._cur->AllocLocalTempVar(CoreStorageType(ar.m_coreTypedIR->types, node.typeId)));
        funs._cur->Push_OP2(ar, NOP_MOV, result._iVar, function->_globalGetterSlot, false, node.source.line);
        return true;
    }
    // 창에 바로 만드는 인자의 원본은 없다(-1). 저장소 추론은 피호출자 서명으로 그 창 슬롯의 종류를 정한다.
    std::vector<int> callSources(count, -1);
    for (std::size_t i = 0; i < directBegin; ++i) callSources[i] = arguments[i]._iVar;
    funs._cur->PrepareCall(ar, callSources, node.source.line, SFunctionInfo::CallWindowNeedsPrepare(function, count));
    for (std::size_t i = 0; i < directBegin; ++i)
    {
        const int slot = COMPILE_CALLARG_VAR_BEGIN + 1 + static_cast<int>(i);
        if (!TryPushImmediate(ar, funs, slot, arguments[i], node.source.line))
            funs._cur->Push_OP2(ar, NOP_MOV, slot, arguments[i]._iVar, arguments[i].IsShort(), node.source.line);
    }
    for (std::size_t i = directBegin; i < count; ++i)
    {
        const int slot = COMPILE_CALLARG_VAR_BEGIN + 1 + static_cast<int>(i);
        SOperand value;
        if (!EmitCoreIRNode(node.inputs[i], value, ar, funs, vars, slot)) return false;
        // 숫자 연산과 컨테이너 읽기는 창 슬롯을 목적지로 받아 바로 썼다. 나머지는 값을 옮긴다.
        if (!value.IsArray() && value._iVar == slot) continue;
        MaterializeValue(value, ar, funs);
        funs._cur->_calls.back().sources[i] = value._iVar;
        if (!TryPushImmediate(ar, funs, slot, value, node.source.line))
            funs._cur->Push_OP2(ar, NOP_MOV, slot, value._iVar, value.IsShort(), node.source.line);
    }
    // 일반 변수의 최종 대입만 직접 반환한다. 디버그에서는 복귀와 대입 사이의 중단 위치를 유지한다.
    const bool direct = !ar._debug && destination != INVALID_ERROR_PARSEJOB &&
        ((destination > 0 && destination < COMPILE_LOCALTMP_VAR_BEGIN) || destination <= COMPILE_GLOBAL_VAR_BEGIN);
    result = SOperand(direct ? destination : funs._cur->AllocLocalTempVar(CoreStorageType(ar.m_coreTypedIR->types, node.typeId)));
    funs._cur->Push_Call(ar, NOP_CALL, function->_funID, static_cast<int>(count), result._iVar, node.source.line);
    return true;
}

static OperationCode CoreIRCollectionOp(const NeoCore::TypeDesc& owner, NeoCore::IRValueType value, bool write)
{
    const bool integer = value == NeoCore::IRValueType::Int, floating = value == NeoCore::IRValueType::Float;
#define NEOCORE_SELECT_ACCESS(OWNER) \
    return write ? (integer ? NOP_##OWNER##_I64_STORE : floating ? NOP_##OWNER##_F64_STORE : NOP_##OWNER##_BOOL_STORE) \
        : (integer ? NOP_##OWNER##_I64_LOAD : floating ? NOP_##OWNER##_F64_LOAD : NOP_##OWNER##_BOOL_LOAD)
    if (owner.kind == NeoCore::TypeKind::List) { NEOCORE_SELECT_ACCESS(LIST); }
    if (owner.kind == NeoCore::TypeKind::Map)
    {
        if (owner.key == static_cast<NeoCore::TypeId>(NeoCore::TypeKind::Int)) { NEOCORE_SELECT_ACCESS(MAP_INT); }
        NEOCORE_SELECT_ACCESS(MAP_STR);
    }
#undef NEOCORE_SELECT_ACCESS
    if (owner.kind == NeoCore::TypeKind::Array32)
        return write ? (integer ? NOP_ARRAY_I32_STORE : NOP_ARRAY_F32_STORE)
            : (integer ? NOP_ARRAY_I32_GET : NOP_ARRAY_F32_GET);
    return write ? (integer ? NOP_ARRAY_I64_STORE : floating ? NOP_ARRAY_F64_STORE : NOP_ARRAY_BOOL_STORE)
        : (integer ? NOP_ARRAY_I64_GET : floating ? NOP_ARRAY_F64_GET : NOP_ARRAY_BOOL_GET);
}

static bool EmitCoreIRCollection(const NeoCore::IRNode& node, SOperand& result,
    CArchiveRdWC& ar, SFunctions& funs, SVars& vars, int destination)
{
    using NeoCore::IROp;
    const bool write = node.op == IROp::CollectionWrite;
    const std::size_t receiverIndex = write ? 1 : 0;
    const NeoCore::TypeDesc& owner = ar.m_coreTypedIR->types.Get(ar.m_coreTypedIR->nodes[node.inputs[receiverIndex]].typeId);
    SOperand value, receiver, key;
    if (write)
    {
        if (!EmitCoreIRNode(node.inputs[0], value, ar, funs, vars)) return false;
        MaterializeValue(value, ar, funs);
        const int effects = (*ar.m_coreIREffects)[node.inputs[1]] | (*ar.m_coreIREffects)[node.inputs[2]];
        if (NeedsSnapshot(value, effects)) SnapshotValue(value, ar, funs);
    }
    if (!EmitCoreIRNode(node.inputs[receiverIndex], receiver, ar, funs, vars)) return false;
    MaterializeValue(receiver, ar, funs);
    if (NeedsSnapshot(receiver, (*ar.m_coreIREffects)[node.inputs[receiverIndex + 1]])) SnapshotValue(receiver, ar, funs);
    if (!EmitCoreIRNode(node.inputs[receiverIndex + 1], key, ar, funs, vars)) return false;
    MaterializeValue(key, ar, funs);
    const OperationCode read = CoreIRCollectionOp(owner, node.type, false);
    if (!write)
    {
        const int target = destination != INVALID_ERROR_PARSEJOB ? destination : funs._cur->AllocLocalTempVar(CoreStorageType(ar.m_coreTypedIR->types, node.typeId));
        funs._cur->Push_OP(ar, read, target, receiver._iVar, key._iVar, node.source.line);
        result = SOperand(target); return true;
    }
    if (node.text != "=")
    {
        const int old = funs._cur->AllocLocalTempVar(CoreStorageType(ar.m_coreTypedIR->types, node.typeId));
        funs._cur->Push_OP(ar, read, old, receiver._iVar, key._iVar, node.source.line);
        IROp operation = node.text == "+=" ? IROp::Add : node.text == "-=" ? IROp::Subtract :
            node.text == "*=" ? IROp::Multiply : node.text == "/=" ? IROp::Divide : node.text == "%=" ? IROp::Remainder :
            node.text == "&=" ? IROp::BitAnd : node.text == "|=" ? IROp::BitOr : node.text == "^=" ? IROp::BitXor :
            node.text == "<<=" ? IROp::ShiftLeft : IROp::ShiftRight;
        value = EmitCoreNumber(CoreIRNumberOp(operation, node.type), SOperand(old), value, ar, funs, node.source.line);
    }
    // 소비되는 대입 결과는 후속 식에서 대상이 바뀌어도 보존한다.
    if (!node.discardResult) value = SnapshotCoreIRValue(value, ar, funs, node.source.line);
    funs._cur->Push_OP(ar, CoreIRCollectionOp(owner, node.type, true), receiver._iVar, key._iVar, value._iVar, node.source.line);
    if (!node.discardResult && owner.kind == NeoCore::TypeKind::Array32 && node.type == NeoCore::IRValueType::Float)
    {
        const int stored = funs._cur->AllocLocalTempVar(CoreStorageType(ar.m_coreTypedIR->types, node.typeId));
        funs._cur->Push_OP(ar, read, stored, receiver._iVar, key._iVar, node.source.line);
        value = SOperand(stored);
    }
    result = value; return true;
}

// `x += c[k];` 문장: 직전 명령이 우변 하나를 임시에 적재한 스칼라 컨테이너 읽기이고 대상이 일반 변수이면,
// 그 읽기를 누적 형식(x += c[k])으로 바꿔 제자리 덧셈 명령과 임시를 없앤다. 우변 임시는 다른 곳에서 읽히지 않는다.
// 결과를 쓰는 식(`y = (x += c[k])`)과 변환이 끼는 우변(실수 += 정수 원소)은 마지막 명령이 읽기가 아니므로 대상이 아니다.
static bool TryFuseContainerLoadAdd(SFunctions& funs, const SOperand& target, SOperand& value, NeoCore::IRValueType type)
{
    SFunctionInfo& fun = *funs._cur;
    const int end = fun._code->GetBufferOffset();
    if (value.IsFun() || value._iArrayIndex != INVALID_ERROR_PARSEJOB || !IsTempVar(value._iVar) ||
        target._iVar == INVALID_ERROR_PARSEJOB || IsTempVar(target._iVar) || IsCompileConstant(target._iVar) ||
        fun._iLastOPOffset < 0 || fun._iLastOPOffset + static_cast<int>(sizeof(SVMOperation)) != end ||
        fun._jumpTargetOffsets.count(end) != 0)
        return false;
    SVMOperation* load = fun.GetOPPointer(fun._iLastOPOffset);
    if (load->n1 != value._iVar) return false;
    OperationCode fused = NOP_NONE;
    switch (load->Code())
    {
#define NEOCORE_FUSE_LOAD_ADD(OWNER, VALUE, TYPE) \
    case NOP_##OWNER##_##VALUE##_LOAD: if (type == NeoCore::IRValueType::TYPE) fused = NOP_##OWNER##_##VALUE##_LOAD_ADD; break;
    NEOCORE_FUSE_LOAD_ADD(LIST, I64, Int)
    NEOCORE_FUSE_LOAD_ADD(LIST, F64, Float)
    NEOCORE_FUSE_LOAD_ADD(MAP_INT, I64, Int)
    NEOCORE_FUSE_LOAD_ADD(MAP_INT, F64, Float)
    NEOCORE_FUSE_LOAD_ADD(MAP_STR, I64, Int)
    NEOCORE_FUSE_LOAD_ADD(MAP_STR, F64, Float)
#undef NEOCORE_FUSE_LOAD_ADD
    default: break;
    }
    if (fused == NOP_NONE) return false;
    load->SetCode(fused);
    load->n1 = CompileShort("accumulate destination", target._iVar);
    return true;
}

// `s = s .. a .. b;`: 왼쪽 결합 연결 사슬이 대상 변수에서 시작해 대상에 끝나고 중간 임시만 거치면 고리마다 대상에
// 직접 쓴다. 참조 하나뿐인 문자열의 제자리 덧붙이기가 사슬 전체에 적용되어 누적이 길이에 비례해 복사하지 않는다.
// 고리는 연속 명령이어야 하고(오른쪽 피연산자가 모두 먼저 계산되어 있음), 오른쪽 피연산자는 대상이 아니어야 하며,
// 사슬 중간으로 들어오는 점프가 없어야 한다.
static void FuseConcatChainIntoTarget(SFunctions& funs, int target)
{
    SFunctionInfo& fun = *funs._cur;
    const int size = static_cast<int>(sizeof(SVMOperation));
    const int end = fun._code->GetBufferOffset();
    if (fun._iLastOPOffset < 0 || fun._iLastOPOffset + size != end) return;
    std::vector<SVMOperation*> links;
    int expected = target;
    bool head = false;
    for (int offset = fun._iLastOPOffset; offset >= fun._iCode_Begin; offset -= size)
    {
        SVMOperation* link = fun.GetOPPointer(offset);
        if (link->op != NOP_STR_ADD || link->n1 != expected || link->n3 == target) return;
        if (!links.empty() && fun._jumpTargetOffsets.count(offset) != 0) return;
        links.push_back(link);
        if (link->n2 == target) { head = true; break; }
        if (!IsTempVar(link->n2)) return;
        expected = link->n2;
    }
    if (!head || links.size() < 2) return;
    for (SVMOperation* link : links)
    {
        link->n1 = CompileShort("concatenation target", target);
        link->n2 = CompileShort("concatenation target", target);
    }
}

static bool EmitCoreIRStorage(const NeoCore::IRNode& node, SOperand& result,
    CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
    using NeoCore::IROp;
    // 필드·셀이 없는 변수 대입은 우변 뒤에 읽기만 이어지므로 우변 슬롯을 복사해 둘 필요가 없다.
    const bool plainVariable = node.storage.kind == NeoCore::IRStorageKind::Variable && node.storage.fields.empty();
    SOperand target;
    // 일반 변수의 슬롯 조회는 코드나 부작용을 만들지 않는다. 필드·셀의 실행 경로는 여전히 우변 뒤에 찾는다.
    if (plainVariable && !ResolveCoreIRStorage(node, target, ar, funs, vars)) return false;
    const int destination = plainVariable && node.discardResult ? target._iVar : INVALID_ERROR_PARSEJOB;
    SOperand value;
    if (!node.inputs.empty())
    {
        // 우변을 먼저 끝내고 값을 보존한 다음 대상 셀과 옛 값을 찾는다.
        if (!EmitCoreIRNode(node.inputs[0], value, ar, funs, vars,
            node.op == IROp::Assign ? destination : INVALID_ERROR_PARSEJOB)) return false;
        if (plainVariable) MaterializeValue(value, ar, funs);
        else value = SnapshotCoreIRValue(value, ar, funs, node.source.line);
    }
    if (!plainVariable && !ResolveCoreIRStorage(node, target, ar, funs, vars)) return false;
    if (node.op == IROp::LoadStorage)
    {
        result = ReadCoreIRStorage(node, target, ar, funs);
        return true;
    }
    SOperand old;
    if (node.op != IROp::Assign)
    {
        if (node.op == IROp::AddAssign && plainVariable && node.discardResult && !node.inputs.empty() &&
            TryFuseContainerLoadAdd(funs, target, value, node.type))
        {
            result = target;
            return true;
        }
        old = ReadCoreIRStorage(node, target, ar, funs);
        const bool postfix = node.op == IROp::PostIncrement || node.op == IROp::PostDecrement;
        // 결과를 버리는 후위 증감은 옛 값을 돌려줄 필요가 없다.
        if (postfix && !node.discardResult) old = SnapshotCoreIRValue(old, ar, funs, node.source.line);
        if (node.inputs.empty())
            value = SOperand(node.type == NeoCore::IRValueType::Float ? funs.AddStaticNum(1.0) : funs.AddStaticInt(1));
        value = EmitCoreNumber(CoreIRWriteOp(node.op, node.type), old, value, ar, funs, node.source.line, destination);
    }
    SOperand stored = value;
    if (!node.storage.fields.empty())
    {
        const OperationCode op = node.type == NeoCore::IRValueType::Int ? NOP_LIST_I64_STORE :
            node.type == NeoCore::IRValueType::Float ? NOP_LIST_F64_STORE :
            node.type == NeoCore::IRValueType::Bool ? NOP_LIST_BOOL_STORE : NOP_CLT_MOV;
        funs._cur->Push_OP(ar, op, target._iVar,
            funs.AddStaticInt(static_cast<NS_INT>(node.storage.fields.back())), value._iVar, node.source.line);
    }
    else if (node.storage.kind == NeoCore::IRStorageKind::Cell)
    {
        const SOperand arguments[] = { value, target };
        stored = EmitCoreAggregate("cellset", arguments, 0, ar, funs, node.source.line);
    }
    else if (node.discardResult)
    {
        // 산술·스크립트 호출은 처음부터 목적지에 쓴다. 나머지 경로만 기존 안전한 저장 규칙을 사용한다.
        if (value._iVar != target._iVar && !TryStoreCallResult(ar, funs, target._iVar, value) &&
            !TryPushImmediate(ar, funs, target._iVar, value, node.source.line))
            funs._cur->Push_OP2(ar, NOP_MOV, target._iVar, value._iVar, false, node.source.line);
        if (node.op == IROp::Assign && plainVariable) FuseConcatChainIntoTarget(funs, target._iVar);
        result = target;
        return true;
    }
    else
    {
        // 대입식 결과 임시는 이후에도 사용한다. MOV peephole로 생산자의 목적지를 변수로 옮기지 않는다.
        funs._cur->Push_OP(ar, NOP_MOV, target._iVar, value._iVar, 0, node.source.line);
    }
    result = node.op == IROp::PostIncrement || node.op == IROp::PostDecrement ? old : stored;
    return true;
}

// IR 잎의 원문(legacySource)을 그 자리에서 컴파일한다. 이관하지 않은 식과 서명으로 검사한 API 호출이 함께 쓴다.
static bool EmitCoreLegacySource(const NeoCore::IRNode& node, SOperand& result,
    CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
    // 아직 이관하지 않은 호출·선택자는 이 잎의 차례에서만 실행 코드를 만든다. 인자를 먼저 모으면 오류 순서가 바뀐다.
    CArchiveRdWC fragment;
    const std::string source = node.legacySource + ";";
    CheckCompileRange("typed IR fragment bytes", static_cast<int64_t>(source.size()), 0, INT_MAX - 1);
    if (!ToArchiveRdWC(source.data(), static_cast<int>(source.size()), fragment))
    { SetCompileError(ar, "invalid typed IR fragment encoding"); return false; }
    fragment._allowGlobalInitLogic = ar._allowGlobalInitLogic;
    fragment._debug = ar._debug;
    fragment._iTableDeep = ar._iTableDeep;
    fragment._parserDepth = ar._parserDepth;
    fragment.m_sSourceName = ar.m_sSourceName;
    fragment.m_sModuleName = ar.m_sModuleName;
    fragment.m_iFileSeq = ar.m_iFileSeq;
    fragment.m_pDebugSourceFiles = ar.m_pDebugSourceFiles;
    fragment.m_pDefines = ar.m_pDefines;
    // 부모는 이 조각의 파싱이 끝날 때까지 살아 있고 상수 표도 변경되지 않는다.
    fragment.m_pScriptDefineParent = &ar;
    fragment.m_pGlobalSymbols = ar.m_pGlobalSymbols;
    fragment.m_pLoader = ar.m_pLoader;
    fragment.m_coreIntrinsicPrefix = ar.m_coreIntrinsicPrefix;
    fragment.m_coreTypedIR = ar.m_coreTypedIR;
    fragment.m_coreExpectedType = node.typeId;
    fragment.m_coreIREffects = ar.m_coreIREffects;
    CArchiveRdWC::Cursor cursor = fragment.SaveCursor();
    cursor.line = node.source.line; cursor.column = node.source.column;
    fragment.RestoreCursor(std::move(cursor));
    struct PresetScope
    {
        SFunctions& functions; int effects, offset;
        explicit PresetScope(SFunctions& value)
            : functions(value), effects(value._presetExprEffects), offset(value._presetExprOffset)
        { functions._presetExprEffects = -1; functions._presetExprOffset = -1; }
        ~PresetScope() { functions._presetExprEffects = effects; functions._presetExprOffset = offset; }
    } preset(funs);
    const TK_TYPE end = ParseShortCircuitLogic(true, result, fragment, funs, vars,
        TK_SEMICOLON, TK_NONE, TK_NONE, TK_NONE);
    std::string trailing;
    // 이름 있는 함수 참조는 컴파일 타임 함수 ID 로 나온다. MaterializeValue 가 FMOV1 로 값 슬롯에 옮기므로
    // 대입 경로와 같은 의미로 호출 인자·반환값에도 쓸 수 있다.
    const bool valid = end == TK_SEMICOLON && !result.IsInvalidValue() && !result.IsNone() &&
        GetToken(fragment, trailing) == TK_NONE && fragment.m_sErrorString.empty();
    // 반환이 없는 API 호출문은 결과 슬롯을 값으로 쓰지 않는다.
    if (valid && node.type != NeoCore::IRValueType::Void)
    { result._storage = CoreStorageType(ar.m_coreTypedIR->types, node.typeId); MaterializeValue(result, fragment, funs); }
    ar.CopyErrorFrom(fragment);
    if (!valid && ar.m_sErrorString.empty()) SetCompileError(ar, "invalid typed IR expression fragment");
    return valid;
}

static bool EmitCoreIRNodeImpl(std::size_t index, SOperand& result,
    CArchiveRdWC& ar, SFunctions& funs, SVars& vars, int destination)
{
    using NeoCore::IROp;
    ParserDepthGuard depth(ar);
    const NeoCore::IRNode& node = ar.m_coreTypedIR->nodes[index];
    if (node.op >= IROp::LoadStorage && node.op <= IROp::PostDecrement)
        return EmitCoreIRStorage(node, result, ar, funs, vars);
    switch (node.op)
    {
    case IROp::IntLiteral:
        result.SetIntLiteral(funs.AddStaticInt(node.integer), node.integer); return true;
    case IROp::FloatLiteral:
        result.SetFloatLiteral(funs.AddStaticNum(node.number), node.number); return true;
    case IROp::BoolLiteral:
        result = SOperand(funs.AddStaticBool(node.boolean)); return true;
    case IROp::Symbol:
        result = SOperand(FindReadableVar(funs, vars, node.backendName));
        if (!result.IsInvalidValue() && !result.IsNone()) return true;
        SetCompileError(ar, "unresolved typed IR symbol '%s'", node.backendName.c_str()); return false;
    case IROp::LegacyExpression:
        return EmitCoreLegacySource(node, result, ar, funs, vars);
    case IROp::ApiCall:
    {
        // 호출은 프런트엔드가 서명으로 검사한 표기 그대로 컴파일한다. 인자 평가·호출 창·결과 처리는 다른 API 호출과 같다.
        if (!EmitCoreLegacySource(node, result, ar, funs, vars)) return false;
        const auto& types = ar.m_coreTypedIR->types;
        // 방금 낸 호출의 계획에 Out 슬롯을 표시한다. 중첩 호출의 계획은 인자 평가 중에 먼저 만들어지므로
        // 마지막 계획이 이 호출의 것이다. 내장 API 와 호스트 API 가 같은 표시를 쓴다.
        if (funs._cur->_calls.empty()) { SetCompileError(ar, "API call with output arguments has no call plan"); return false; }
        CompileCallStorage& plan = funs._cur->_calls.back();
        for (const NeoCore::IROutput& output : node.outputs)
        {
            if (output.parameter >= plan.sources.size())
            { SetCompileError(ar, "output argument is outside the call window"); return false; }
            plan.outMask |= 1u << output.parameter;
        }
        // Out 인자의 값은 호출 창 슬롯에 남아 있다(런타임이 그 슬롯을 놓지 않는다). 대입이나 복제가 창을 다시 쓰기 전에
        // 먼저 모두 임시로 옮겨 소유권을 가져온다.
        std::vector<int> values;
        for (const NeoCore::IROutput& output : node.outputs)
        {
            const int value = funs._cur->AllocLocalTempVar(CoreStorageType(types, output.typeId));
            funs._cur->Push_OP2(ar, NOP_MOV, value, COMPILE_CALLARG_VAR_BEGIN + 1 + static_cast<int>(output.parameter), false, node.source.line);
            values.push_back(value);
        }
        for (std::size_t i = 0; i < node.outputs.size(); ++i)
        {
            const NeoCore::IROutput& output = node.outputs[i];
            SOperand stored(values[i]);
            if (!output.cloneHelper.empty())
            {
                // 구조체는 값이다. 컬렉션이 가진 객체를 변수와 공유하지 않도록 복제해서 대입한다.
                SFunctionInfo* clone = funs.FindFun(output.cloneHelper);
                if (!clone || clone->_args.size() != 1)
                { SetCompileError(ar, "unresolved struct copy for an out argument"); return false; }
                std::vector<int> sources(1, values[i]);
                funs._cur->PrepareCall(ar, sources, node.source.line, SFunctionInfo::CallWindowNeedsPrepare(clone, 1));
                funs._cur->Push_OP2(ar, NOP_MOV, COMPILE_CALLARG_VAR_BEGIN + 1, values[i], false, node.source.line);
                stored = SOperand(funs._cur->AllocLocalTempVar(CoreStorageType(types, output.typeId)));
                funs._cur->Push_Call(ar, NOP_CALL, clone->_funID, 1, stored._iVar, node.source.line);
            }
            SOperand target;
            if (!ResolveCoreIRStorage(output.storage, node.source.line, target, ar, funs, vars)) return false;
            if (output.storage.kind == NeoCore::IRStorageKind::Cell)
            {
                SOperand writeArguments[] = { stored, target };
                EmitCoreAggregate("cellset", writeArguments, 0, ar, funs, node.source.line);
            }
            else funs._cur->Push_OP2(ar, NOP_MOV, target._iVar, stored._iVar, false, node.source.line);
        }
        funs._cur->ClearLastOP();
        return true;
    }
    case IROp::ScriptCall:
        return EmitCoreIRCall(node, ar.m_coreTypedIR->functions[node.functionIndex].backendName, result, ar, funs, vars, destination);
    case IROp::NullableConvert:
        return EmitCoreIRNode(node.inputs[0], result, ar, funs, vars);
    case IROp::CopyStruct:
        return EmitCoreIRCall(node, node.backendName, result, ar, funs, vars);
    case IROp::MapRead:
    {
        SOperand arguments[2];
        if (!EmitCoreIRMapInputs(node, arguments, ar, funs, vars)) return false;
        const bool stringKey = ar.m_coreTypedIR->nodes[node.inputs[1]].type == NeoCore::IRValueType::String;
        result = EmitCoreAggregate(stringKey ? "msread" : "miread", arguments, 0, ar, funs, node.source.line);
        return true;
    }
    case IROp::StringLiteral:
        result = SOperand(funs.AddStaticString(node.text)); return true;
    case IROp::Conditional:
    {
        SOperand condition;
        if (!EmitCoreIRNode(node.inputs[0], condition, ar, funs, vars)) return false;
        MaterializeValue(condition, ar, funs);
        // 조건이 방금 낸 비교의 결과면 if 문처럼 비교와 점프를 한 명령으로 합친다. 비교가 바로 앞 명령이고 그 결과가
        // 이 조건이며 그 사이로 들어오는 점프가 없을 때만 한다.
        SJumpValue falseJump;
        bool fused = false;
        if (funs._cur->_iLastOPOffset >= 0 && IsTempVar(condition._iVar))
        {
            const int end = funs._cur->_code->GetBufferOffset();
            if (funs._cur->_iLastOPOffset + static_cast<int>(sizeof(SVMOperation)) == end &&
                funs._cur->_jumpTargetOffsets.count(end) == 0 &&
                funs._cur->GetOPPointer(funs._cur->_iLastOPOffset)->n1 == condition._iVar)
                fused = TryFuseCompareJump(false, condition, funs, falseJump);
        }
        if (!fused)
        {
            funs._cur->Push_JMPFalse(ar, condition._iVar, 0);
            const int falseBase = funs._cur->_code->GetBufferOffset();
            falseJump.Set(falseBase - static_cast<int>(sizeof(short) * 3), falseBase);
        }
        // 호출자가 준 목적지가 있으면 결과를 거기에 바로 둔다. 가지의 값은 자기 임시에서 다 계산한 뒤 마지막에
        // 옮기고 실행되는 가지는 하나뿐이므로, 가지가 목적지의 옛 값을 읽어도 순서가 바뀌지 않는다. 없으면 새 임시를 쓴다.
        const int selected = destination != INVALID_ERROR_PARSEJOB ? destination
            : funs._cur->AllocLocalTempVar(CoreStorageType(ar.m_coreTypedIR->types, node.typeId));
        SJumpValue endJump;
        int writers[2] = { -1, -1 };
        for (std::size_t branch = 1; branch < 3; ++branch)
        {
            funs._cur->ClearLastOP();
            SOperand value;
            if (!EmitCoreIRNode(node.inputs[branch], value, ar, funs, vars)) return false;
            MaterializeValue(value, ar, funs);
            // 가지의 값이 목적지 변수 자신이면(`x = c ? y : x`) 옮길 것이 없다.
            if ((value.IsArray() || value._iVar != selected) && !TryPushImmediate(ar, funs, selected, value, node.source.line))
                funs._cur->Push_OP2(ar, NOP_MOV, selected, value._iVar, value.IsShort(), node.source.line);
            // 이 가지에서 결과 임시에 쓰는 마지막 명령. 방금 낸 복사·즉값이거나 목적지가 접힌 생산 명령이다.
            writers[branch - 1] = -1;
            if (funs._cur->_iLastOPOffset >= 0 &&
                funs._cur->_iLastOPOffset + static_cast<int>(sizeof(SVMOperation)) == funs._cur->_code->GetBufferOffset() &&
                funs._cur->GetOPPointer(funs._cur->_iLastOPOffset)->n1 == selected &&
                (funs._cur->GetLastOP() == NOP_MOV || SFunctionInfo::IsRetargetableProducerOP(funs._cur->GetLastOP())))
                writers[branch - 1] = funs._cur->_iLastOPOffset;
            if (branch == 1)
            {
                funs._cur->Push_JMP(ar, 0);
                const int endBase = funs._cur->_code->GetBufferOffset();
                endJump.Set(endBase - static_cast<int>(sizeof(short) * 3), endBase);
                funs._cur->Set_JumpOffet(falseJump, endBase);
            }
        }
        funs._cur->Set_JumpOffet(endJump, funs._cur->_code->GetBufferOffset());
        result = SOperand(selected);
        // 마지막 이동은 거짓 가지 전용이다. 합류 뒤의 반환·목적지 접기를 금지한다.
        funs._cur->ClearLastOP();
        // 결과를 새 임시에 두었고 두 가지의 쓰기 위치를 알면, 바로 뒤의 변수 대입이 두 쓰기를 그 변수로 돌릴 수 있다.
        funs._cur->_conditionalTemp = -1;
        if (destination == INVALID_ERROR_PARSEJOB && writers[0] >= 0 && writers[1] >= 0)
        {
            funs._cur->_conditionalTemp = selected;
            funs._cur->_conditionalWriters[0] = writers[0];
            funs._cur->_conditionalWriters[1] = writers[1];
            funs._cur->_conditionalEnd = funs._cur->_code->GetBufferOffset();
        }
        return true;
    }
    case IROp::CollectionRead: case IROp::CollectionWrite:
        return EmitCoreIRCollection(node, result, ar, funs, vars, destination);
    default: break;
    }

    SOperand operands[2];
    if (!EmitCoreIRNode(node.inputs[0], operands[0], ar, funs, vars)) return false;
    MaterializeValue(operands[0], ar, funs);
    if (node.inputs.size() == 2)
    {
        const int effects = (*ar.m_coreIREffects)[node.inputs[1]];
        if (NeedsSnapshot(operands[0], effects)) SnapshotValue(operands[0], ar, funs);
        if (!EmitCoreIRNode(node.inputs[1], operands[1], ar, funs, vars)) return false;
        MaterializeValue(operands[1], ar, funs);
    }
    if (node.op == IROp::Negate)
    {
        if (operands[0].IsIntLiteral())
        { const NS_INT value = NeoIntNeg(operands[0]._intLiteral); result.SetIntLiteral(funs.AddStaticInt(value), value); return true; }
        if (operands[0].IsFloatLiteral())
        { const NS_FLOAT value = -operands[0]._floatLiteral; result.SetFloatLiteral(funs.AddStaticNum(value), value); return true; }
        result = SOperand(destination != INVALID_ERROR_PARSEJOB ? destination : funs._cur->AllocLocalTempVar(CoreNumericStorage(node.type)));
        funs._cur->Push_OP2(ar, node.type == NeoCore::IRValueType::Int ? OP_I64_NEG : OP_F64_NEG,
            result._iVar, operands[0]._iVar, false, node.source.line);
        return true;
    }
    if (node.op == IROp::BitNot)
    {
        if (operands[0].IsIntLiteral())
        { const NS_INT value = ~operands[0]._intLiteral; result.SetIntLiteral(funs.AddStaticInt(value), value); return true; }
        result = EmitCoreNumber(OP_I64_XOR, operands[0], SOperand(funs.AddStaticInt(-1)), ar, funs, node.source.line, destination);
        return true;
    }
    if (node.op == IROp::LogicalNot)
    {
        if (IsTempVar(operands[0]._iVar) && funs._cur->_iLastOPOffset >= 0)
        {
            SVMOperation* comparison = funs._cur->GetOPPointer(funs._cur->_iLastOPOffset);
            const TypedOpInfo* info = GetTypedOpInfo(comparison->Code());
            if (info && info->comparison && !info->jump && comparison->n1 == operands[0]._iVar &&
                funs._cur->_iLastOPOffset + static_cast<int>(sizeof(SVMOperation)) == funs._cur->_code->GetBufferOffset() &&
                funs._cur->_jumpTargetOffsets.count(funs._cur->_code->GetBufferOffset()) == 0)
            {
                comparison->argFlag ^= NEOS_OP_COMPARE_INVERT;
                result = operands[0]; return true;
            }
        }
        result = SOperand(destination != INVALID_ERROR_PARSEJOB ? destination : funs._cur->AllocLocalTempVar(CoreNumericStorage(node.type)));
        funs._cur->Push_OP2(ar, NOP_LOG_NOT, result._iVar, operands[0]._iVar, false, node.source.line);
        return true;
    }
    const OperationCode op = CoreIRNumberOp(node.op, ar.m_coreTypedIR->nodes[node.inputs[0]].type);
    if (node.op == IROp::ToFloat && operands[0].IsIntLiteral())
    { const NS_FLOAT value = static_cast<NS_FLOAT>(operands[0]._intLiteral); result.SetFloatLiteral(funs.AddStaticNum(value), value); return true; }
    if (node.op == IROp::ToInt && operands[0].IsFloatLiteral())
    { const NS_INT value = NeoFloatToInt(operands[0]._floatLiteral); result.SetIntLiteral(funs.AddStaticInt(value), value); return true; }
    if (node.inputs.size() == 2 && TryFoldLiteralBinOp(operands[0], TypedBinaryOp(op), operands[1], funs))
    { result = operands[0]; return true; }
    result = SOperand(destination != INVALID_ERROR_PARSEJOB ? destination : funs._cur->AllocLocalTempVar(CoreNumericStorage(node.type)));
    if (node.inputs.size() == 1)
        funs._cur->Push_OP2(ar, op, result._iVar, operands[0]._iVar, false, node.source.line);
    else
        funs._cur->Push_OP(ar, op, result._iVar, operands[0]._iVar, operands[1]._iVar, node.source.line);
    return true;
}

static bool EmitCoreIRNode(std::size_t index, SOperand& result,
    CArchiveRdWC& ar, SFunctions& funs, SVars& vars, int destination)
{
    if (!EmitCoreIRNodeImpl(index, result, ar, funs, vars, destination)) return false;
    result._storage = CoreStorageType(ar.m_coreTypedIR->types,
        ar.m_coreTypedIR->nodes[index].typeId);
    if (IsTempVar(result._iVar)) funs._cur->SetStorage(result._iVar, result._storage);
    return true;
}

static bool ParseCoreIRIndex(const std::string& name, const std::string& prefix,
    std::size_t count, std::size_t& index, CArchiveRdWC& ar)
{
    index = 0;
    bool valid = name.size() > prefix.size();
    for (std::size_t i = prefix.size(); valid && i < name.size(); ++i)
    {
        const int digit = name[i] - '0';
        if (digit < 0 || digit > 9 || index > ((std::numeric_limits<std::size_t>::max)() - digit) / 10)
            valid = false;
        else index = index * 10 + digit;
    }
    std::string token;
    if (!valid || index >= count || GetToken(ar, token) != TK_L_SMALL || GetToken(ar, token) != TK_R_SMALL)
    { SetCompileError(ar, "invalid typed IR marker '%s'", name.c_str()); return false; }
    return true;
}

static bool ParseCoreIR(SOperand& result, const std::string& name,
    CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
    const NeoCore::TypedIR& ir = *ar.m_coreTypedIR;
    std::size_t index;
    return ParseCoreIRIndex(name, ir.prefix, ir.nodes.size(), index, ar) &&
        EmitCoreIRNode(index, result, ar, funs, vars);
}

static bool ParseCoreIRReturn(const std::string& name, CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
    const NeoCore::TypedIR& ir = *ar.m_coreTypedIR;
    std::size_t index;
    if (!ParseCoreIRIndex(name, ir.returnPrefix, ir.returns.size(), index, ar)) return false;
    const NeoCore::IRReturn& statement = ir.returns[index];
    if (ir.functions[statement.functionIndex].backendName != funs.GetCurFunName())
    { SetCompileError(ar, "typed IR return belongs to another function"); return false; }
    std::string token;
    if (GetToken(ar, token) != TK_SEMICOLON)
    { SetParserCompileError(ar, PCE_INVALID_RETURN_STATEMENT); return false; }
    if (statement.valueIndex == static_cast<std::size_t>(-1))
        funs._cur->Push_RETURN(ar, 0, true, statement.source.line);
    else
    {
        SOperand value;
        if (!EmitCoreIRNode(statement.valueIndex, value, ar, funs, vars)) return false;
        MaterializeValue(value, ar, funs);
        funs._cur->Push_RETURN(ar, value._iVar, value.IsShort(), statement.source.line);
    }
    return true;
}

bool ParseString(SOperand& operand, TK_TYPE tkTypePre, CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
	std::string tk1, tk2;
	TK_TYPE tkType1, tkType2;

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_STRING)
	{
		SetParserCompileError(ar, PCE_UNKNOWN_IDENTIFIER, tk1.c_str());
		return false;
	}

	const int lexicalSlot = FindReadableVar(funs, vars, tk1);
	SFunctionLayer* pOtherModule = nullptr;
	std::string moduleName;
	auto it = funs._curModule->_defModules.find(tk1);
	if (lexicalSlot == -1 && it != funs._curModule->_defModules.end())
	{
		moduleName = tk1;
		if (!ReadModuleMember(ar, tk2))
			return false;
		tk1 = tk2;
		pOtherModule = (*it).second;
	}

	SOperand value;
	std::string coreApiKey;
	if (pOtherModule == nullptr)
		value._iVar = lexicalSlot;
	else
	{
		const auto constant = pOtherModule->_exportedConsts.values.find(tk1);
		if (constant != pOtherModule->_exportedConsts.values.end()
			&& !MakeConstOperand(constant->second, value, ar, funs))
			return false;
		// public var 는 전역 슬롯 그대로라 뒤의 선택자 · 대입 · 증감이 자기 모듈 변수와 똑같이 붙는다.
		if (value._iVar == -1)
			value._iVar = FindPublicModuleVar(vars, pOtherModule, tk1);
	}
    if (pOtherModule == nullptr && ar.m_coreTypedIR && !ar.m_coreTypedIR->returnPrefix.empty() &&
        tk1.compare(0, ar.m_coreTypedIR->returnPrefix.size(), ar.m_coreTypedIR->returnPrefix) == 0)
    { SetCompileError(ar, "typed IR return marker is only valid in a return statement"); return false; }
	if (pOtherModule == nullptr && ar.m_coreTypedIR && !ar.m_coreTypedIR->prefix.empty() &&
        tk1.compare(0, ar.m_coreTypedIR->prefix.size(), ar.m_coreTypedIR->prefix) == 0)
    {
        if (!ParseCoreIR(value, tk1, ar, funs, vars)) return false;
    }
    else if (pOtherModule == nullptr && !ar.m_coreIntrinsicPrefix.empty() &&
        tk1.compare(0, ar.m_coreIntrinsicPrefix.size(), ar.m_coreIntrinsicPrefix) == 0)
	{
        if (!ParseCoreTypedIntrinsic(value, tk1, ar, funs, vars)) return false;
	}
	else if (value._iVar == -1)
	{
		SFunctionInfo* pFun = pOtherModule == nullptr ? funs.FindFun(tk1) : funs.FindFun(tk1, pOtherModule);
		// 다른 모듈의 함수는 public 인 것만 보인다(내장 모듈은 전부). export 는 호스트용이라 모듈에는 안 보인다.
		if (pFun != NULL && pOtherModule != nullptr && !pOtherModule->_blBuiltInModule && !pFun->_blPublic)
			pFun = NULL;
		if (pFun != NULL)
		{
			if (funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME && false == ar._allowGlobalInitLogic)
			{
				SetParserCompileError(ar, PCE_GLOBAL_CALL_NOT_ALLOWED);
				return false;
			}
			tkType2 = GetToken(ar, tk2);
			ar.PushToken(tkType2, tk2);
			if (tkType2 == TK_L_SMALL)
			{
				if (!ParseFunCall(value, TK_NONE, pFun, ar, funs, vars))
					return false;
			}
			else
			{
				// 호출하지 않은 함수 이름은 컴파일 전용 함수 참조로 유지한다.
                if (pOtherModule != nullptr && pOtherModule->_blBuiltInModule)
                {
                    SetCompileError(ar, "native function '%s.%s' must be called directly", moduleName.c_str(), tk1.c_str());
                    return false;
                }
				value = SOperand(pFun->_funID, INVALID_ERROR_PARSEJOB, Data_Fun);
			}
		}
		else if (pOtherModule == nullptr && CoreApiFunctionKey(tk1, coreApiKey))
		{
			tkType2 = GetToken(ar, tk2);
			if (tkType2 != TK_L_SMALL)
			{
				SetParserCompileError(ar, PCE_INVALID_FUNCTION_CALL);
				return false;
			}
			value = SOperand(-1, funs.AddStaticString(coreApiKey));
			ar.PushToken(tkType2, tk2);
			if (!ParseFunCall(value, TK_NONE, NULL, ar, funs, vars))
				return false;
		}
		else
		{
			if (pOtherModule != nullptr)
			{
				SetParserCompileError(ar, PCE_MODULE_MEMBER, moduleName.c_str(), tk1.c_str());
				return false;
			}
			// 숫자 리터럴의 부호와 즉값 최적화는 기존 숫자 파서에서 처리한다.
			return ParseNum(operand, tkTypePre, tk1, ar, funs, vars);
		}
	}

	if (!ParsePostfixSelectors(value, ar, funs, vars))
		return false;
	if (tkTypePre == TK_MINUS)
	{
		// -L()[i], -S().len() 의 부호는 호출 결과가 아니라 최종 선택자 결과에 한 번만 적용한다.
		MaterializeValue(value, ar, funs);
		const int negated = IsTempVar(value._iVar) ? value._iVar : funs._cur->AllocLocalTempVar();
		funs._cur->Push_OP2(ar, RejectLegacyArithmetic(ar), negated, value._iVar, false);
		value = SOperand(negated);
	}
	operand = value;
	return true;
}

bool ParseToType(int& result, TK_TYPE prefix, CArchiveRdWC& ar, SFunctions& funs, SVars& vars);

static bool ParseUnaryOperand(SOperand& operand, TK_TYPE prefix, CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
    ParserDepthGuard depth(ar);
    std::string token;
    const TK_TYPE type = GetToken(ar, token);
    bool appliedSign = false;
    if (type == TK_PLUS || type == TK_MINUS || type == TK_NOT || type == TK_LOGIC_NOT)
    {
        if (!ParseUnaryOperand(operand, type, ar, funs, vars)) return false;
    }
    else if (type == TK_PLUS2 || type == TK_MINUS2)
    {
        if (!ParseUnaryOperand(operand, TK_PLUS, ar, funs, vars)) return false;
        if (operand.IsFun() || operand._readOnly || (!operand.IsArray() &&
            (IsTempVar(operand._iVar) || IsCompileConstant(operand._iVar))))
        {
            SetParserCompileError(ar, PCE_INVALID_INCREMENT_TARGET, token.c_str());
            return false;
        }
        if (operand.IsArray())
            WriteElement(operand, RejectLegacyArithmetic(ar), funs.AddStaticInt(1), false, ar, funs);
        else funs._cur->Push_OP1(ar, RejectLegacyArithmetic(ar), operand._iVar);
    }
    else if (type == TK_L_SMALL)
    {
        if (ParseShortCircuitLogic(true, operand, ar, funs, vars) != TK_R_SMALL) return false;
        if (!ParsePostfixSelectors(operand, ar, funs, vars)) return false;
    }
    else if (type == TK_TOSTRING || type == TK_TOINT || type == TK_TOFLOAT)
    {
        if (!ParseToType(operand._iVar, type, ar, funs, vars)) return false;
        if (!ParsePostfixSelectors(operand, ar, funs, vars)) return false;
    }
    else if (type == TK_TRUE || type == TK_FALSE)
        operand = SOperand(funs.AddStaticBool(type == TK_TRUE));
    else if (type == TK_NULL)
    {
        operand = SOperand(funs._cur->AllocLocalTempVar());
        funs._cur->Push_OP1(ar, NOP_VAR_CLEAR, operand._iVar);
    }
    else if (type == TK_QUOTE1 || type == TK_QUOTE2 || type == TK_STRING_LITERAL)
    {
        if (type != TK_STRING_LITERAL && !GetQuotationString(ar, token, type == TK_QUOTE1 ? '\'' : '"')) return false;
        operand = SOperand(funs.AddStaticString(token));
        if (!ParsePostfixSelectors(operand, ar, funs, vars)) return false;
    }
    else if (type == TK_L_ARRAY || type == TK_L_MIDDLE)
    {
        const TK_TYPE end = type == TK_L_ARRAY ? ParseListDef(operand, ar, funs, vars) : ParseTableDef(operand, ar, funs, vars);
        if (end == TK_NONE || !ParsePostfixSelectors(operand, ar, funs, vars)) return false;
    }
    else
    {
        ar.PushToken(type, token);
        appliedSign = prefix == TK_MINUS && type == TK_STRING && !token.empty() && token[0] >= '0' && token[0] <= '9';
        if (!ParseString(operand, appliedSign ? TK_MINUS : TK_NONE, ar, funs, vars)) return false;
    }
    // Postfix mutation binds more tightly than the leading unary operator.
    const TK_TYPE next = GetToken(ar, token);
    if (next == TK_PLUS2 || next == TK_MINUS2)
    {
        if (operand.IsFun() || operand._readOnly || (!operand.IsArray() &&
            (IsTempVar(operand._iVar) || IsCompileConstant(operand._iVar))))
        {
            SetParserCompileError(ar, PCE_INVALID_INCREMENT_TARGET, token.c_str());
            return false;
        }
        const int old = funs._cur->AllocLocalTempVar();
        ReadElement(operand, old, ar, funs);
        if (operand.IsArray())
            WriteElement(operand, RejectLegacyArithmetic(ar), funs.AddStaticInt(1), false, ar, funs);
        else
            funs._cur->Push_OP1(ar, RejectLegacyArithmetic(ar), operand._iVar);
        operand = SOperand(old);
    }
    else ar.PushToken(next, token);
    if (operand.IsInvalidValue())
    {
        SetParserCompileError(ar, PCE_EXPECTED_EXPRESSION);
        return false;
    }
    if (appliedSign || prefix == TK_PLUS) return true;
    MaterializeValue(operand, ar, funs);
    if (prefix == TK_LOGIC_NOT && IsTempVar(operand._iVar))
    {
        const TypedOpInfo* typed = GetTypedOpInfo(funs._cur->GetLastOP());
        if (typed && typed->comparison && !typed->jump)
        {
            SVMOperation* comparison = funs._cur->GetOPPointer(funs._cur->_iLastOPOffset);
            if (comparison->n1 == operand._iVar &&
                funs._cur->_iLastOPOffset + static_cast<int>(sizeof(SVMOperation)) == funs._cur->_code->GetBufferOffset() &&
                funs._cur->_jumpTargetOffsets.count(funs._cur->_code->GetBufferOffset()) == 0)
            {
                // NaN에서도 !(a < b)와 a >= b는 다르므로 결과 반전 비트만 뒤집는다.
                comparison->argFlag ^= NEOS_OP_COMPARE_INVERT;
                return true;
            }
        }
    }
    const int result = funs._cur->AllocLocalTempVar();
    if (prefix == TK_NOT)
    {
        SetCompileError(ar, "bitwise expressions require typed IR");
        return false;
    }
    else
        funs._cur->Push_OP2(ar, prefix == TK_MINUS ? RejectLegacyArithmetic(ar) : NOP_LOG_NOT, result, operand._iVar, false);
    operand = SOperand(result);
    return true;
}

bool ParseToType(int& iResultStack, TK_TYPE tkTypePre, CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
	std::string tk1;
	TK_TYPE tkType1;
	TK_TYPE r = TK_NONE;

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_SMALL)
	{
		SetParserCompileError(ar, PCE_EXPECTED_TYPE_LEFT_PAREN);
		return false;
	}

	SOperand operand;
	r = ParseShortCircuitLogic(true, operand, ar, funs, vars);
	if (r != TK_R_SMALL)
	{
		SetParserCompileError(ar, PCE_EXPECTED_TYPE_RIGHT_PAREN);
		return TK_NONE;
	}

	MaterializeValue(operand, ar, funs);

	int iTempOffset3 = funs._cur->AllocLocalTempVar();
	int iPriority = 0;
	funs._cur->Push_ToType(ar, TokenToOP(tkTypePre, iPriority), iTempOffset3, operand._iVar);
	iResultStack = iTempOffset3;
	return true;
}

//case TK_SEMICOLON:	// ;
//case TK_COMMA:		// ,
//case TK_R_SMALL:	// )
//case TK_R_ARRAY:	// ]
static bool IsAssignmentToken(TK_TYPE type)
{
    return type == TK_EQUAL || type == TK_PLUS_EQ || type == TK_MINUS_EQ || type == TK_MUL_EQ ||
        type == TK_DIV_EQ || type == TK_PERCENT_EQ || type == TK_LSHIFT_EQ || type == TK_RSHIFT_EQ ||
        type == TK_AND_EQ || type == TK_OR_EQ || type == TK_XOR_EQ || type == TK_PLUS2 || type == TK_MINUS2;
}

// ---------------------------------------------------------------------------
// 식 선스캔. 토큰을 만들지 않고 소스 문자를 직접 훑는다 — GetToken 은 토큰마다 std::string 을 만들고
// define 표를 찾기 때문에, 식마다 한 번 더 어휘분석하면 컴파일 시간이 중첩 깊이에 제곱으로 늘었다.
// 판정에 필요한 것만 본다: 대입 연산자, 호출 '(', 선택자, 괄호 깊이, 종결 토큰. 문자열·주석은 건너뛰고
// 숫자 리터럴은 ScanNumberLiteral 과 같은 규칙으로 지나간다(0x1e+F() 의 '+' 는 연산자다).
// 해석할 수 없는 상황이면 보수적으로 EXPR_EFFECT_ALL 을 돌려준다.
// ---------------------------------------------------------------------------
// IR 표식(`$irN`)의 부작용은 프런트엔드가 노드별로 모아 둔 비트를 쓴다(하위 트리 포함). 표가 없거나 번호를 읽지
// 못하면 보수적으로 전부로 본다. 숫자 식 하나인 인자가 뒤따르는 인자의 복사·직접 쓰기 판정을 막지 않게 한다.
static int CoreIRReferenceEffects(const CArchiveRdWC& ar, const std::string& digits)
{
    if (ar.m_coreIREffects == nullptr || digits.empty()) return EXPR_EFFECT_ALL;
    std::size_t index = 0;
    for (std::size_t i = 0; i < digits.size(); ++i)
    {
        const int digit = digits[i] - '0';
        if (digit < 0 || digit > 9 || index > ((std::numeric_limits<std::size_t>::max)() - digit) / 10) return EXPR_EFFECT_ALL;
        index = index * 10 + digit;
    }
    return index < ar.m_coreIREffects->size() ? (*ar.m_coreIREffects)[index] : EXPR_EFFECT_ALL;
}

namespace
{
enum EffectPrev { EP_NONE, EP_IDENT, EP_CLOSE, EP_IRREF };   // 직전 토큰: 이름 / ')' ']' / 그 외 / IR 표식(뒤의 `()`는 호출이 아니다)

struct EffectScanner
{
    CArchiveRdWC& ar;
    const TK_TYPE* ends;      // 종결 토큰(depth 0 에서만), 최대 6개, TK_NONE/TK_UNUSED 는 무시
    int endCount;
    bool splitArgs;           // 호출 인자 모드: depth 0 의 ',' 로 나누고 ')' 에서 끝난다
    std::vector<int>* perArg;

    int effects = 0;          // splitArgs 면 현재 인자의 비트
    int depth = 0;
    EffectPrev prev = EP_NONE;
    bool sawArgToken = false;
    bool complete = false;    // 종결 토큰(또는 짝 ')')을 만나 정상 종료했는가
    int k = 0;                // 현재 오프셋에서의 상대 위치
    u16 pendingQuote = 0;     // 큐의 마지막 토큰이 따옴표면 본문이 아직 소스에 남아 있다 — 먼저 건너뛴다

    EffectScanner(CArchiveRdWC& a, const TK_TYPE* e, int n, bool split, std::vector<int>* out)
        : ar(a), ends(e), endCount(n), splitArgs(split), perArg(out) {}

    u16 Peek(int d = 0) { return ar.PeekData(k + d); }
    bool AtEnd() { return ar.GetBufferOffset() + k >= ar.GetBufferSize(); }
    static bool IsDig(u16 c) { return c >= '0' && c <= '9'; }
    static bool IsHex(u16 c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
    bool IsEndType(TK_TYPE t)
    {
        for (int i = 0; i < endCount; ++i) if (ends[i] == t) return true;
        return false;
    }
    void Add(int bits) { effects |= bits; }
    bool AllKnown() const { return !splitArgs && effects == EXPR_EFFECT_ALL; }

    // depth 0 의 닫는 괄호/종결 문자. true 면 스캔 종료.
    bool Closer(TK_TYPE t, bool isParen)
    {
        if (depth > 0) { --depth; prev = isParen ? EP_CLOSE : EP_NONE; return false; }
        if (splitArgs) { if (t == TK_R_SMALL) { if (sawArgToken) perArg->push_back(effects); complete = true; } return true; }
        complete = true;   // 짝이 맞지 않는 닫힘도 식의 끝이다(원래 토큰 스캐너와 같은 규칙)
        return true;
    }
    bool Separator(TK_TYPE t)   // ';' ',' ':' '&&' '||'
    {
        if (depth > 0) { prev = EP_NONE; return false; }
        if (splitArgs && t == TK_COMMA) { perArg->push_back(effects); effects = 0; sawArgToken = false; prev = EP_NONE; return false; }
        if (IsEndType(t)) { complete = true; return true; }
        prev = EP_NONE;
        return false;
    }

    // 1) 밀어 둔 토큰(PushToken) 이 먼저 나온다.
    bool Queued()
    {
        for (const SToken& st : ar.m_sTokenQueue)
        {
            const TK_TYPE t = st._type;
            if (t == TK_NONE) return true;
            if (pendingQuote != 0) { effects = EXPR_EFFECT_ALL; return true; }   // 따옴표 뒤에 또 토큰: 해석 불가
            sawArgToken = true;
            if (IsAssignmentToken(t)) { Add(EXPR_EFFECT_ASSIGN); prev = EP_NONE; continue; }
            switch (t)
            {
            case TK_STRING:
                // IR 표식 뒤에는 원문에서 보이지 않는 대입이나 호스트 호출이 있을 수 있다.
                if (ar.m_coreTypedIR && !ar.m_coreTypedIR->prefix.empty() &&
                    st._tk.compare(0, ar.m_coreTypedIR->prefix.size(), ar.m_coreTypedIR->prefix) == 0)
                {
                    Add(CoreIRReferenceEffects(ar, st._tk.substr(ar.m_coreTypedIR->prefix.size())));
                    prev = EP_IRREF; continue;
                }
                prev = EP_IDENT; continue;
            case TK_QUOTE1: pendingQuote = '\''; prev = EP_NONE; continue;
            case TK_QUOTE2: pendingQuote = '"'; prev = EP_NONE; continue;
            case TK_L_SMALL:
                if (prev == EP_IDENT || prev == EP_CLOSE) Add(EXPR_EFFECT_CALL);
                ++depth; prev = EP_NONE; continue;
            case TK_L_ARRAY: Add(EXPR_EFFECT_SELECTOR); ++depth; prev = EP_NONE; continue;
            case TK_DOT: Add(EXPR_EFFECT_SELECTOR); prev = EP_NONE; continue;
            case TK_L_MIDDLE: ++depth; prev = EP_NONE; continue;
            case TK_R_SMALL: if (Closer(t, true)) return true; continue;
            case TK_R_ARRAY: if (Closer(t, true)) return true; continue;
            case TK_R_MIDDLE: if (Closer(t, false)) return true; continue;
            case TK_SEMICOLON: case TK_COMMA: case TK_COLON: case TK_AND2: case TK_OR2:
                if (Separator(t)) return true; continue;
            default: prev = EP_NONE; continue;
            }
        }
        return false;
    }

    // 숫자 리터럴. ScanNumberLiteral 과 같은 경계로 지나간다.
    void SkipNumber()
    {
        if (Peek() == '0' && (Peek(1) == 'x' || Peek(1) == 'X'))
        {
            k += 2;
            while (IsHex(Peek())) ++k;
            if (Peek() == '.' && IsHex(Peek(1))) { ++k; while (IsHex(Peek())) ++k; }
            if (Peek() == 'p' || Peek() == 'P')
            {
                const u16 n1 = Peek(1);
                if (IsDig(n1) || ((n1 == '+' || n1 == '-') && IsDig(Peek(2))))
                {
                    ++k;
                    if (Peek() == '+' || Peek() == '-') ++k;
                    while (IsDig(Peek())) ++k;
                }
            }
            return;
        }
        while (IsDig(Peek())) ++k;
        if (Peek() == '.' && IsDig(Peek(1))) { ++k; while (IsDig(Peek())) ++k; }
        if (Peek() == 'e' || Peek() == 'E')
        {
            const u16 n1 = Peek(1);
            if (IsDig(n1) || ((n1 == '+' || n1 == '-') && IsDig(Peek(2))))
            {
                ++k;
                if (Peek() == '+' || Peek() == '-') ++k;
                while (IsDig(Peek())) ++k;
            }
        }
    }

    // 2) 소스 문자. 반환 true 면 종료.
    bool Chars()
    {
        if (pendingQuote != 0)
        {
            for (;;)
            {
                if (AtEnd()) { effects = EXPR_EFFECT_ALL; return true; }
                const u16 d = Peek();
                if (d == '\\') { k += 2; continue; }
                ++k;
                if (d == pendingQuote) break;
            }
            pendingQuote = 0;
        }
        for (;;)
        {
            if (AllKnown()) return true;
            if (AtEnd()) return true;   // complete=false (EOF)
            const u16 c = Peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++k; continue; }
            if (c == '/')
            {
                const u16 n = Peek(1);
                if (n == '/') { k += 2; while (!AtEnd() && Peek() != '\n') ++k; continue; }
                if (n == '*')
                {
                    k += 2;
                    for (;;) { if (AtEnd()) { effects = EXPR_EFFECT_ALL; return true; } if (Peek() == '*' && Peek(1) == '/') { k += 2; break; } ++k; }
                    continue;
                }
                sawArgToken = true;
                if (n == '=') { Add(EXPR_EFFECT_ASSIGN); k += 2; } else ++k;
                prev = EP_NONE; continue;
            }
            sawArgToken = true;
            if (c == '"' || c == '\'')
            {
                ++k;
                for (;;)
                {
                    if (AtEnd()) { effects = EXPR_EFFECT_ALL; return true; }
                    const u16 d = Peek();
                    if (d == '\\') { k += 2; continue; }
                    ++k;
                    if (d == c) break;
                }
                prev = EP_NONE; continue;
            }
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80 ||
                (c == '$' && ar.m_coreTypedIR))
            {
                bool irReference = false;
                if (ar.m_coreTypedIR && !ar.m_coreTypedIR->prefix.empty())
                {
                    const std::string& prefix = ar.m_coreTypedIR->prefix;
                    bool matches = true;
                    for (std::size_t i = 0; i < prefix.size(); ++i)
                        if (Peek(static_cast<int>(i)) != static_cast<unsigned char>(prefix[i])) { matches = false; break; }
                    if (matches)
                    {
                        std::string digits;
                        for (int d = static_cast<int>(prefix.size()); !AtEnd(); ++d)
                        {
                            const u16 digit = Peek(d);
                            if (digit < '0' || digit > '9') break;
                            digits.push_back(static_cast<char>(digit));
                        }
                        Add(CoreIRReferenceEffects(ar, digits));
                        irReference = true;
                    }
                }
                if (c == '$') ++k;
                while (!AtEnd())
                {
                    const u16 d = Peek();
                    if (!((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || (d >= '0' && d <= '9') || d == '_' || d >= 0x80)) break;
                    ++k;
                }
                prev = irReference ? EP_IRREF : EP_IDENT;
                continue;
            }
            if (IsDig(c)) { SkipNumber(); prev = EP_NONE; continue; }
            ++k;
            const u16 n = Peek();
            switch (c)
            {
            case '(':
                if (prev == EP_IDENT || prev == EP_CLOSE) Add(EXPR_EFFECT_CALL);
                ++depth; prev = EP_NONE; break;
            case '[': Add(EXPR_EFFECT_SELECTOR); ++depth; prev = EP_NONE; break;
            case '{': ++depth; prev = EP_NONE; break;
            case ')': if (Closer(TK_R_SMALL, true)) return true; break;
            case ']': if (Closer(TK_R_ARRAY, true)) return true; break;
            case '}': if (Closer(TK_R_MIDDLE, false)) return true; break;
            case '.':
                if (n == '.') { ++k; prev = EP_NONE; break; }   // '..' 잇기
                Add(EXPR_EFFECT_SELECTOR); prev = EP_NONE; break;
            case ';': if (Separator(TK_SEMICOLON)) return true; break;
            case ',': if (Separator(TK_COMMA)) return true; break;
            case ':': if (Separator(TK_COLON)) return true; break;
            case '=': if (n == '=') ++k; else Add(EXPR_EFFECT_ASSIGN); prev = EP_NONE; break;
            case '!': if (n == '=') ++k; prev = EP_NONE; break;
            case '<': case '>':
                if (n == c) { ++k; if (Peek() == '=') { ++k; Add(EXPR_EFFECT_ASSIGN); } }
                else if (n == '=') ++k;
                prev = EP_NONE; break;
            case '+': case '-':
                if (n == c || n == '=') { ++k; Add(EXPR_EFFECT_ASSIGN); }
                prev = EP_NONE; break;
            case '*': case '%': case '^':
                if (n == '=') { ++k; Add(EXPR_EFFECT_ASSIGN); }
                prev = EP_NONE; break;
            case '&':
                if (n == '&') { ++k; if (Separator(TK_AND2)) return true; break; }
                if (n == '=') { ++k; Add(EXPR_EFFECT_ASSIGN); }
                prev = EP_NONE; break;
            case '|':
                if (n == '|') { ++k; if (Separator(TK_OR2)) return true; break; }
                if (n == '=') { ++k; Add(EXPR_EFFECT_ASSIGN); }
                prev = EP_NONE; break;
            default: prev = EP_NONE; break;
            }
        }
    }
};
} // namespace

static int ScanExpressionEffects(CArchiveRdWC& ar, TK_TYPE e1, TK_TYPE e2, TK_TYPE e3,
    TK_TYPE e4, TK_TYPE e5, TK_TYPE e6)
{
    const TK_TYPE ends[6] = { e1, e2, e3, e4, e5, e6 };
    EffectScanner scan(ar, ends, 6, false, nullptr);
    if (!scan.Queued()) scan.Chars();
    return scan.effects;
}
static bool ScanCallArguments(CArchiveRdWC& ar, std::vector<int>& argEffects)
{
    argEffects.clear();
    EffectScanner scan(ar, nullptr, 0, true, &argEffects);
    if (!scan.Queued()) scan.Chars();
    if (!scan.complete) { argEffects.clear(); return false; }
    return true;
}
TK_TYPE ParseJob(bool bReqReturn, SOperand& sResultStack, std::vector<SJumpValue>* pJumps, CArchiveRdWC& ar, SFunctions& funs, SVars& vars, bool bAllowVarDef,
	TK_TYPE tkEnd1, TK_TYPE tkEnd2, TK_TYPE tkEnd3, TK_TYPE tkEnd4, std::vector<SJumpValue>* pContinueJumps, TK_TYPE tkEnd5, TK_TYPE tkEnd6)
{
	ParserDepthGuard depth(ar);
	std::string tk1, tk2;
	TK_TYPE tkType1, tkType2;
	TK_TYPE r = TK_NONE;

	SOperand iCurrentStack = INVALID_ERROR_PARSEJOB;
	SOperand iTempOffset;

	std::vector<SOperand> operands;
	std::vector<TK_TYPE> operators;
	bool blApperOperator = false;

    int exprEffects;
    if (funs._presetExprEffects >= 0 && funs._presetExprOffset == ar.GetBufferOffset())
        exprEffects = funs._presetExprEffects;
    else
        exprEffects = ScanExpressionEffects(ar, tkEnd1, tkEnd2, tkEnd3, tkEnd4, tkEnd5, tkEnd6);
    funs._presetExprEffects = -1;
    auto reduce = [&](int maximumPriority) -> bool
    {
	while (operators.empty() == false)
	{
		int iTempPriority;
		int iFindOffset = -1;
		int iFindPriority = 100;
		for (int i = 0; i < (int)operators.size(); i++)
		{
			TokenToOP(operators[i], iTempPriority);
			if (i == 0 || iFindPriority > iTempPriority)
			{
				iFindPriority = iTempPriority;
				iFindOffset = i;
			}
		}
		if (iFindPriority > maximumPriority) break;
		const TK_TYPE operatorToken = operators[iFindOffset];
		auto op = TokenToOP(operatorToken, iTempPriority);
		RemoveAt<TK_TYPE>(operators, iFindOffset);

		SOperand& a = operands[iFindOffset];
		auto b = operands[iFindOffset + 1];

		// 양쪽이 숫자 리터럴이면 여기서 계산해 버린다(런타임 연산이 통째로 사라진다).
		// 결과 리터럴이 다시 피연산자가 되므로 (2 + 3) * 4 처럼 중첩된 식도 끝까지 접힌다.
		if (TryFoldLiteralBinOp(a, TokenBinaryOp(operatorToken), b, funs))
		{
			operands[iFindOffset] = a;
			RemoveAt<SOperand>(operands, iFindOffset + 1);
			continue;
		}

        if (op == NOP_NONE)
        {
            SetCompileError(ar, "numeric expressions require typed IR");
            return false;
        }
        // 함수 값 대입에서도 목적지를 먼저 검사한다.
		if (op < NOP_VAR_CLEAR && (a.IsFun() || a._readOnly ||
			(!a.IsArray() && (IsTempVar(a._iVar) || IsCompileConstant(a._iVar)))))
		{
			SetParserCompileError(ar, PCE_EXPECTED_LVALUE);
			return false;
		}
		if (op < NOP_VAR_CLEAR)
		{
			if (b.IsFun() && op == NOP_MOV)
			{
				if (a.IsArray())
					WriteElement(a, NOP_FMOV2, b._iVar, false, ar, funs);
				else
					funs._cur->Push_OP2(ar, NOP_FMOV1, a._iVar, b._iVar, false);
			}
			else if (op == NOP_MOV && !a.IsArray() && b.IsArray())
				ReadElement(b, a._iVar, ar, funs); // retain READ-to-destination fusion
			else
			{
				MaterializeValue(b, ar, funs); // complete the RHS before resolving the destination
				if (a.IsArray())
					WriteElement(a, op, b._iVar, b.IsShort(), ar, funs);
				else if (op != NOP_MOV || (!funs._cur->RetargetConditional(a._iVar, b._iVar) &&
					!TryStoreCallResult(ar, funs, a._iVar, b) && !TryPushImmediate(ar, funs, a._iVar, b)))
					funs._cur->Push_OP2(ar, op, a._iVar, b._iVar, b.IsShort());
			}
		}
		else
		{
			MaterializeValue(a, ar, funs);
			MaterializeValue(b, ar, funs);
			ElideImmediateSnapshot(a, ar, funs);
			if (op == NOP_STR_ADD) ElideConcatConversion(a, b, ar, funs);
			// `x == true`, `x != false` 는 x 자체이고 `x == false`, `x != true` 는 !x 다. 정적 타입이 양쪽을 bool로
			// 보증하므로 태그를 읽는 범용 비교를 내지 않는다. 뒤따르는 조건 점프는 NOT 을 반대 점프로 접는다.
			if (op == OP_EQUAL2 || op == OP_NEQUAL)
			{
				bool constant = false;
				SOperand* other = nullptr;
				if (StaticBoolOperand(funs, b, constant)) other = &a;
				else if (StaticBoolOperand(funs, a, constant)) other = &b;
				if (other != nullptr)
				{
					if ((op == OP_EQUAL2) == constant)
						operands[iFindOffset] = SOperand(*other);
					else
					{
						const int negated = funs._cur->AllocLocalTempVar();
						funs._cur->Push_OP2(ar, NOP_LOG_NOT, negated, other->_iVar, false);
						operands[iFindOffset] = negated;
					}
					RemoveAt<SOperand>(operands, iFindOffset + 1);
					continue;
				}
			}
			const int iTempOffset2 = funs._cur->AllocLocalTempVar();
			funs._cur->Push_OP(ar, op, iTempOffset2, a._iVar, b._iVar, a.IsShort(), b.IsShort());
			operands[iFindOffset] = iTempOffset2;
		}
		RemoveAt<SOperand>(operands, iFindOffset + 1);
	}

        return true;
    };

	bool blEnd = false;
	while (blEnd == false)
	{
		iTempOffset.Reset();

		tkType1 = GetToken(ar, tk1);
		if (tkType1 == tkEnd1 || tkType1 == tkEnd2 || tkType1 == tkEnd3 || tkType1 == tkEnd4 ||
			(tkEnd5 != TK_UNUSED && tkType1 == tkEnd5) ||
			(tkEnd6 != TK_UNUSED && tkType1 == tkEnd6))
		{
			r = tkType1;
			blEnd = true;
			break;
		}
		switch (tkType1)
		{
		case TK_NONE:
			SetParserCompileError(ar, PCE_UNEXPECTED_FUNCTION_END, funs.GetCurFunName().c_str());
			return TK_NONE;
		case TK_RETURN:
			if (funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME)
			{
				SetParserCompileError(ar, PCE_UNEXPECTED_GLOBAL_BLOCK_END);
				return TK_NONE;
			}
            if (ar.m_coreTypedIR && !ar.m_coreTypedIR->returnPrefix.empty())
            {
                tkType2 = GetToken(ar, tk2);
                if (tkType2 == TK_STRING && tk2.compare(0, ar.m_coreTypedIR->returnPrefix.size(), ar.m_coreTypedIR->returnPrefix) == 0)
                {
                    if (!ParseCoreIRReturn(tk2, ar, funs, vars)) return TK_NONE;
                    r = TK_SEMICOLON;
                    blEnd = true;
                    break;
                }
                ar.PushToken(tkType2, tk2);
            }
			iTempOffset.Reset();
			if (TK_SEMICOLON != ParseShortCircuitLogic(true, iTempOffset, ar, funs, vars, TK_SEMICOLON, TK_NONE, TK_NONE, TK_NONE))
			{
				SetParserCompileError(ar, PCE_INVALID_RETURN_STATEMENT);
				return TK_NONE;
			}
			if(iTempOffset.IsInvalidValue() == false)
			{
				MaterializeValue(iTempOffset, ar, funs);
				funs._cur->Push_RETURN(ar, iTempOffset._iVar, iTempOffset.IsShort());
			}
			else 
				funs._cur->Push_RETURN(ar, 0, true);
			r = TK_SEMICOLON;
			blEnd = true;
			break;
		case TK_L_SMALL:
			iTempOffset = INVALID_ERROR_PARSEJOB;
			// 괄호 안은 이 식의 부분 범위다 — 바깥 비트가 상한이므로 다시 훑지 않는다.
			funs._presetExprEffects = exprEffects;
			funs._presetExprOffset = ar.GetBufferOffset();
			if (TK_R_SMALL != ParseShortCircuitLogic(bReqReturn, iTempOffset, ar, funs, vars))
			{
				funs._presetExprEffects = -1;
				return TK_NONE;
			}
			funs._presetExprEffects = -1;
			if (!ParsePostfixSelectors(iTempOffset, ar, funs, vars)) return TK_NONE;
			operands.push_back(SOperand(iTempOffset));
			blApperOperator = true;
			break;
		case TK_STRING:
			ar.PushToken(tkType1, tk1);
			if (false == ParseString(iTempOffset, TK_NONE, ar, funs, vars))
			{
				return TK_NONE;
			}

			operands.push_back(iTempOffset);
			blApperOperator = true;
			break;
		case TK_QUOTE2:
		case TK_QUOTE1:
		{
			std::string str;
			if (false == GetQuotationString(ar, str, tkType1 == TK_QUOTE2 ? '"' : '\''))
			{
				SetParserCompileError(ar, PCE_UNTERMINATED_STRING);
				return TK_NONE;
			}
			iTempOffset = funs.AddStaticString(str);
			if (!ParsePostfixSelectors(iTempOffset, ar, funs, vars)) return TK_NONE;
			operands.push_back(SOperand(iTempOffset));
			blApperOperator = true;
			break;
		}
		case TK_STRING_LITERAL: // define/const 치환으로 완성된 문자열
			iTempOffset = funs.AddStaticString(tk1);
			if (!ParsePostfixSelectors(iTempOffset, ar, funs, vars)) return TK_NONE;
			operands.push_back(SOperand(iTempOffset));
			blApperOperator = true;
			break;
		case TK_PLUS:		// +
		case TK_MINUS:		// -
            if (!blApperOperator)
            {
                SOperand value;
                if (!ParseUnaryOperand(value, tkType1, ar, funs, vars)) return TK_NONE;
                operands.push_back(value);
                blApperOperator = true;
                break;
            }
            if (NeedsSnapshot(operands.back(), exprEffects)) SnapshotValue(operands.back(), ar, funs);
            else ResolveContainer(operands.back(), ar, funs);
            {
                int priority = 0;
                TokenToOP(tkType1, priority);
                if (!reduce(priority)) return TK_NONE;
            }
			operators.push_back(tkType1);
			blApperOperator = false;
			break;
		case TK_NOT:
		case TK_LOGIC_NOT:	// !
			if (blApperOperator == false)
			{
				SOperand a;
				if (false == ParseUnaryOperand(a, tkType1, ar, funs, vars))
					return TK_NONE;
				operands.push_back(a);
				blApperOperator = true;
				break;
			}
			SetParserCompileError(ar, PCE_INVALID_OPERATOR, tk1.c_str());
			return TK_NONE;
		case TK_MUL:		// *
		case TK_DIV:		// /
		case TK_PERCENT:	// %
		case TK_LSHIFT:		// <<
		case TK_RSHIFT:		// >>
		case TK_GREAT:		// >
		case TK_GREAT_EQ:	// >=
		case TK_LESS:		// <
		case TK_LESS_EQ:	// <=
		case TK_EQUAL_EQ:	// ==
		case TK_EQUAL_NOT:	// !=
		case TK_AND:		// &
		case TK_XOR:		// ^
		case TK_OR:			// |
		case TK_DOT2:		// ..
		{
			if (blApperOperator == false)
			{
				SetParserCompileError(ar, PCE_INVALID_OPERATOR, tk1.c_str());
				return TK_NONE;
			}
            if (NeedsSnapshot(operands.back(), exprEffects)) SnapshotValue(operands.back(), ar, funs);
            else ResolveContainer(operands.back(), ar, funs);
            {
                int priority = 0;
                TokenToOP(tkType1, priority);
                if (!reduce(priority)) return TK_NONE;
            }
			operators.push_back(tkType1);
			blApperOperator = false;
			break;
		}
		case TK_EQUAL:		// =
		case TK_PLUS_EQ:	// +=
		case TK_MINUS_EQ:	// -=
		case TK_MUL_EQ:		// *=
		case TK_DIV_EQ:		// /=
		case TK_PERCENT_EQ:	// %=
		case TK_LSHIFT_EQ:	// <<=
		case TK_RSHIFT_EQ:	// >>=
		case TK_AND_EQ:		// &=
		case TK_OR_EQ:		// |=
		case TK_XOR_EQ:		// ^=
		{
			SOperand iTempVar;
			r = ParseShortCircuitLogic(true, iTempVar, ar, funs, vars, tkEnd1, tkEnd2, tkEnd3, tkEnd4);
			if (TK_NONE == r)
				return TK_NONE;
			if (iTempVar._iVar == INVALID_ERROR_PARSEJOB)
			{
				SetParserCompileError(ar, PCE_INVALID_ASSIGNMENT);
				return TK_NONE;
			}
			operators.push_back(tkType1);

			operands.push_back(SOperand(iTempVar));
			blApperOperator = true;
			blEnd = true;
			break;
		}
		case TK_VAR:
			if (bAllowVarDef)
			{
				if (false == ParseVarDef(ar, funs, vars, false, false))
					return TK_NONE;
				return TK_SEMICOLON; // ㅡㅡ;
			}
			else
			{
				SetParserCompileError(ar, PCE_VAR_DECLARATION_NOT_ALLOWED);
				return TK_NONE;
			}
			break;
		case TK_BREAK:
			if(pJumps == NULL)
			{
				SetParserCompileError(ar, PCE_BREAK_OUTSIDE_LOOP);
				return TK_NONE;
			}
			funs._cur->Push_JMP(ar, 0);
			pJumps->push_back(SJumpValue(funs._cur->_code->GetBufferOffset() - (sizeof(short) * 3), funs._cur->_code->GetBufferOffset()));
			break;
		case TK_CONTINUE:
			if (pContinueJumps == NULL)
			{
				SetParserCompileError(ar, PCE_CONTINUE_OUTSIDE_LOOP);
				return TK_NONE;
			}
			funs._cur->Push_JMP(ar, 0);
			pContinueJumps->push_back(SJumpValue(funs._cur->_code->GetBufferOffset() - (sizeof(short) * 3), funs._cur->_code->GetBufferOffset()));
			break;
		case TK_L_MIDDLE:
			iTempOffset.Reset();
			ar._iTableDeep++;
			r = ParseTableDef(iTempOffset, ar, funs, vars);
			ar._iTableDeep--;
			// 실패를 무시하고 계속 진행하면 사유 없는 컴파일 실패로 이어진다.
			if (r == TK_NONE)
				return TK_NONE;
			if (!ParsePostfixSelectors(iTempOffset, ar, funs, vars)) return TK_NONE;
			operands.push_back(iTempOffset);
			blApperOperator = true;
			break;
		case TK_L_ARRAY:
			iTempOffset.Reset();
			ar._iTableDeep++;
			r = ParseListDef(iTempOffset, ar, funs, vars);
			ar._iTableDeep--;
			if (r == TK_NONE)
				return TK_NONE;
			if (!ParsePostfixSelectors(iTempOffset, ar, funs, vars)) return TK_NONE;
			operands.push_back(iTempOffset);
			blApperOperator = true;
			break;

		case TK_NULL:
			//iTempOffset._iVar = COMPILE_VAR_NULL;
			iTempOffset._iVar = funs._cur->AllocLocalTempVar();
			funs._cur->Push_OP1(ar, NOP_VAR_CLEAR, iTempOffset._iVar);
			operands.push_back(SOperand(iTempOffset._iVar));
			blApperOperator = true;
			break;
		case TK_TRUE:
		case TK_FALSE:
			iTempOffset._iVar = funs.AddStaticBool(tkType1 == TK_TRUE);
			operands.push_back(SOperand(iTempOffset._iVar));
			blApperOperator = true;
			break;
		case TK_TOSTRING:
		case TK_TOINT:
		case TK_TOFLOAT:
			// iTempOffset 에 -1 을 대입하면 SOperand(int) 생성자가 _operandType 을 Data_None 으로
			// 잡아버린다. ParseToType 은 _iVar 만 채우므로 그 표시가 남아, 값은 멀쩡한데
			// "값 없음" 으로 보이는 피연산자가 만들어진다(리스트 리터럴이 원소를 버리는 원인).
			// TK_TRUE/TK_FALSE 처럼 결과 int 로 SOperand 를 새로 만든다.
			iTempOffset.Reset();
			if (false == ParseToType(iTempOffset._iVar, tkType1, ar, funs, vars))
				return TK_NONE;
			if (!ParsePostfixSelectors(iTempOffset, ar, funs, vars)) return TK_NONE;
			operands.push_back(iTempOffset);
			blApperOperator = true;
			break;
		case TK_PLUS2: // ++
		case TK_MINUS2: // --
			if (blApperOperator == false)
			{	// 전위
				SOperand a;
				if (false == ParseString(a, TK_NONE, ar, funs, vars))
				{
					return TK_NONE;
				}
				if (a.IsFun() || a._readOnly || IsCompileConstant(a._iVar))
				{	// 상수 풀(리터럴/const) 증감 불가
					SetParserCompileError(ar, PCE_INVALID_INCREMENT_TARGET, tk1.c_str());
					return TK_NONE;
				}
				if (a.IsArray())
				{
					const int one = funs.AddStaticInt(1);
					WriteElement(a, RejectLegacyArithmetic(ar), one, false, ar, funs);
					const int value = funs._cur->AllocLocalTempVar();
					ReadElement(a, value, ar, funs);
					a = SOperand(value);
				}
				else
				{
					a._operandType = Increment_Prefix;
					funs._cur->Push_OP1(ar, RejectLegacyArithmetic(ar), a._iVar);
				}

				operands.push_back(a);
				blApperOperator = true;
			}
			else
			{	// 후위
				SOperand& a = operands[operands.size() - 1];
				if (a.IsFun() || a._readOnly || (a.IsArray() == false && IsTempVar(a._iVar)))
				{
					SetParserCompileError(ar, PCE_TEMP_VAR_UNSUPPORTED, tk1.c_str());
					return TK_NONE;
				}
				if (a.IsConst() ||
					(IsCompileConstant(a._iVar)))
				{	// 상수 풀(리터럴/const) 증감 불가
					SetParserCompileError(ar, PCE_INVALID_INCREMENT_TARGET, tk1.c_str());
					return TK_NONE;
				}
				int iTempOffset2;
				if (a.IsArray())
				{
					if (bReqReturn)
					{
						iTempOffset2 = funs._cur->AllocLocalTempVar();
						ReadElement(a, iTempOffset2, ar, funs);
					}
					const int one = funs.AddStaticInt(1);
					WriteElement(a, RejectLegacyArithmetic(ar), one, false, ar, funs);
				}
				else
				{
					if (bReqReturn)
					{
						iTempOffset2 = funs._cur->AllocLocalTempVar();
						funs._cur->Push_OP2(ar, NOP_MOV, iTempOffset2, a._iVar, false);
					}
					funs._cur->Push_OP1(ar, RejectLegacyArithmetic(ar), a._iVar);
				}

				if (bReqReturn)
					a = SOperand(iTempOffset2);
			}
			break;
		case TK_FUN: // FUNT_ANONYMOUS ?
		{
			int r = ParseFunctionBase(ar, funs, vars, "", FUNT_ANONYMOUS);
			if(r == -1)
				return TK_NONE; // error
			SFunctionInfo* pFun = funs.FindFun(r);
			if (pFun == NULL)
				return TK_NONE; // error
			operands.push_back(SOperand(pFun->_funID, INVALID_ERROR_PARSEJOB, Data_Fun));
			blApperOperator = true;
			break;
		}
		case TK_YIELD: // 
		{
			if(operands.empty() == false || operators.empty() == false)
			{
				SetParserCompileError(ar, PCE_INVALID_OPERATOR, tk1.c_str());
				return TK_NONE;
			}
			blEnd = true;

			//tkType2 = GetToken(ar, tk2);
			//if (tkType2 != TK_RETURN)
			//{
			//	SetCompileError(ar, "Error (%d, %d): Invalid Operator", ar.CurLine(), ar.CurCol(), tk1.c_str());
			//	return TK_NONE;
			//}

			tkType2 = GetToken(ar, tk2);
			if (tkType2 != TK_SEMICOLON)
			{
				SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "';' after 'yield'", tk2.c_str());
				return TK_NONE;
			}

			funs._cur->Push_OP(ar, NOP_YIELD, YILED_RETURN, 0, 0);
			r = TK_SEMICOLON;
			break;
		}
		default:
			SetParserCompileError(ar, PCE_SYNTAX_ERROR, tk1.c_str());
			return TK_NONE;
		}
	}
	if (operands.empty() == true)
		return r;

	if (operands.size() != operators.size() + 1)
	{
		SetParserCompileError(ar, PCE_INVALID_OPERATOR, tk1.c_str());
		return TK_NONE;
	}
	if (!reduce(100)) return TK_NONE;

	// 빈 리스트/맵의 첫 원소처럼 종결 토큰을 바로 만난 경우에는 값이 없다.
	// operands[0] 접근은 UB이며, 호출자가 빈 컨테이너를 정상 종료로 처리할 수 있게 한다.
	if (operands.empty())
	{
		sResultStack = SOperand(INVALID_ERROR_PARSEJOB);
		return r;
	}

	sResultStack = operands[0];
	if (!bReqReturn && r == TK_SEMICOLON)
		ResolveContainer(sResultStack, ar, funs); // discarded expression, not a parenthesized lvalue
	return r;
}

// 논리 연산은 우측 피연산자의 코드가 실행되기 전에 분기한다.
// 따라서 "a == null || a.member"에서 a가 null이면 member 접근 자체를 건너뛴다.
// ParseJob 과 동일한 계약: 실제로 만난 종결 토큰을 반환, 에러는 TK_NONE.
// 논리 연산자가 없는 식은 emit 없이 그대로 통과한다 (기존 fused jump 최적화 유지).
OperationCode ConvertCheckOPToOptimize(OperationCode n);      // 아래에 정의(비교 → 참일때점프)
OperationCode ConvertCheckOPToOptimizeInv(OperationCode n);   // 아래에 정의(비교 → 거짓일때점프)

static bool UsesCompareInvert(OperationCode op)
{
    const TypedOpInfo* typed = GetTypedOpInfo(op);
    return typed ? typed->comparison :
        (op == OP_GREAT || op == OP_GREAT_EQ || op == OP_LESS || op == OP_LESS_EQ);
}

// 직전에 생성된 비교 OP(LS/GR/...)를 "비교+점프" 결합 명령(JMP_LESS 등)으로 덮어쓴다.
// 비교 OP 와 결합 점프는 인자 크기가 같아(dst/src1/src2 ↔ jumpOfs/src1/src2) 제자리 치환이 된다.
// bJumpWhenTrue=true  : 조건이 참일 때 점프(|| 그룹)
// bJumpWhenTrue=false : 조건이 거짓일 때 점프(&& 그룹 / 마지막 그룹)
// 성공하면 outJump 에 패치용 점프 위치를 담고 true 를 돌려준다.
static bool TryFuseCompareJump(bool bJumpWhenTrue, const SOperand& operand, SFunctions& funs, SJumpValue& outJump)
{
	if (false == IsTempVar(operand._iVar))
		return false;
	const OperationCode opCheck = funs._cur->GetLastOP();
	const OperationCode opOpz = bJumpWhenTrue ? ConvertCheckOPToOptimize(opCheck)
	                                        : ConvertCheckOPToOptimizeInv(opCheck);
	if (opOpz == NOP_NONE)
		return false;

	const int argLen = (int)sizeof(short) * 3;
	funs._cur->_code->SetPointer(-((int)sizeof(OpType) + (int)sizeof(ArgFlag) + argLen), SEEK_CUR);
	OpType optype = PrimaryOperation(opOpz);
	funs._cur->_code->Write(&optype, sizeof(optype));
	funs._cur->GetOPPointer(funs._cur->_code->GetBufferOffset() - sizeof(OpType))->SetCode(opOpz);
	if (!bJumpWhenTrue && UsesCompareInvert(opCheck))
		funs._cur->GetOPPointer(funs._cur->_code->GetBufferOffset() - sizeof(OpType))->argFlag ^= NEOS_OP_COMPARE_INVERT;
	funs._cur->_code->SetPointer((int)sizeof(ArgFlag), SEEK_CUR);
	const int cur = funs._cur->_code->GetBufferOffset();
	// This is an unresolved forward jump, not a jump to byte offset zero.
	funs._cur->SetN(cur - (int)sizeof(OpType) - (int)sizeof(ArgFlag), 0, 0);
	funs._cur->_code->SetPointer(argLen, SEEK_CUR);
	outJump.Set(funs._cur->_code->GetBufferOffset() - 6, funs._cur->_code->GetBufferOffset());
	return true;
}

enum ERangeCompareRelation
{
	RANGE_REL_NONE,
	RANGE_REL_LESS,
	RANGE_REL_LESS_EQ,
	RANGE_REL_GREAT,
	RANGE_REL_GREAT_EQ,
};

struct SRangeBoundCandidate
{
	short	_test = 0;
	short	_bound = 0;
	bool	_lower = false;
	bool	_inclusive = false;
};

static ERangeCompareRelation GetRangeCompareRelation(OperationCode op)
{
	switch (op)
	{
	case OP_JMP_LESS:     return RANGE_REL_LESS;
	case OP_JMP_I64_LT:  return RANGE_REL_LESS;
	case OP_JMP_F64_LT:  return RANGE_REL_LESS;
	case OP_JMP_LESS_EQ:  return RANGE_REL_LESS_EQ;
	case OP_JMP_I64_LE:  return RANGE_REL_LESS_EQ;
	case OP_JMP_F64_LE:  return RANGE_REL_LESS_EQ;
	case OP_JMP_GREAT:    return RANGE_REL_GREAT;
	case OP_JMP_I64_GT:  return RANGE_REL_GREAT;
	case OP_JMP_F64_GT:  return RANGE_REL_GREAT;
	case OP_JMP_GREAT_EQ: return RANGE_REL_GREAT_EQ;
	case OP_JMP_I64_GE:  return RANGE_REL_GREAT_EQ;
	case OP_JMP_F64_GE:  return RANGE_REL_GREAT_EQ;
	default:                return RANGE_REL_NONE;
	}
}

static ERangeCompareRelation InvertRangeCompareRelation(ERangeCompareRelation relation)
{
	switch (relation)
	{
	case RANGE_REL_GREAT:    return RANGE_REL_LESS_EQ;
	case RANGE_REL_GREAT_EQ: return RANGE_REL_LESS;
	case RANGE_REL_LESS:     return RANGE_REL_GREAT_EQ;
	case RANGE_REL_LESS_EQ:  return RANGE_REL_GREAT;
	default:                 return RANGE_REL_NONE;
	}
}

// `a < b`는 a의 upper bound 이면서 b의 lower bound 이기도 하다. 두 비교에서
// 동일한 test slot을 찾은 뒤 lower/upper 한 쌍으로 맞춰, 피연산자 순서까지 포함한
// 모든 < / <= / >= / > 표기를 처리한다.
static int BuildRangeBoundCandidates(const SVMOperation& op, bool jumpWhenTrue, bool buildInsideRange,
	SRangeBoundCandidate outCandidates[2])
{
	ERangeCompareRelation relation = GetRangeCompareRelation(op.Code());
	const TypedOpInfo* typed = GetTypedOpInfo(op.Code());
	// 원본의 ! 비교까지 범위식으로 뒤집으면 NaN에서 !(x < lo)와 x >= lo가 달라진다.
	// 이 경우에는 결과 반전을 가진 두 전용 분기를 그대로 유지한다.
	if (typed != nullptr && jumpWhenTrue == ((op.argFlag & NEOS_OP_COMPARE_INVERT) != 0))
		return 0;
	if (jumpWhenTrue == ((op.argFlag & NEOS_OP_COMPARE_INVERT) != 0))
		relation = InvertRangeCompareRelation(relation);
	// OR 형태는 두 "outside" 조건을 하나의 inside 판정으로 바꾼다. AND 형태는
	// 두 조건이 이미 inside 경계이므로 그대로 쓴다.
	if (buildInsideRange)
		relation = InvertRangeCompareRelation(relation);
	if (relation == RANGE_REL_NONE)
		return 0;

	const bool inclusive = relation == RANGE_REL_LESS_EQ || relation == RANGE_REL_GREAT_EQ;
	const bool leftIsLower = relation == RANGE_REL_GREAT || relation == RANGE_REL_GREAT_EQ;

	// n2 relation n3
	outCandidates[0]._test = op.n2;
	outCandidates[0]._bound = op.n3;
	outCandidates[0]._lower = leftIsLower;
	outCandidates[0]._inclusive = inclusive;
	// 반대 피연산자를 test로 보면 bound 방향도 반대가 된다.
	outCandidates[1]._test = op.n3;
	outCandidates[1]._bound = op.n2;
	outCandidates[1]._lower = !leftIsLower;
	outCandidates[1]._inclusive = inclusive;
	return 2;
}

// 분기 컨텍스트의 정확히 두 비교만 한 opcode로 바꾼다.
//
//   x < lo || x >= hi  -> JMP_RANGE_INSIDE (inside면 body를 건너뜀)
//   lo <= x && x < hi  -> JMP_RANGE_OUTSIDE (outside면 body를 건너뜀)
//
// 두 점프 사이에 피연산자 계산 코드가 있으면 해당 코드는 short-circuit 순서 자체를
// 보장해야 하므로 건드리지 않는다. 이 최적화는 인접 비교 op 두 개에만 적용한다.
static bool TryFuseRangeJump(SFunctions& funs, const std::vector<SJumpValue>& trueJumps,
	const std::vector<SJumpValue>& falseJumps, SJumpValue& outFalseJump)
{
	const SJumpValue* firstJump = nullptr;
	const SJumpValue* secondJump = nullptr;
	bool firstJumpWhenTrue = false;
	bool secondJumpWhenTrue = false;
	OperationCode rangeOp = NOP_NONE;

	if (trueJumps.size() == 1 && falseJumps.size() == 1)
	{
		// OR: 첫 비교가 참이면 body, 마지막 비교는 거짓이면 body 밖으로 간다.
		firstJump = &trueJumps[0];
		secondJump = &falseJumps[0];
		firstJumpWhenTrue = true;
		secondJumpWhenTrue = false;
		rangeOp = NOP_JMP_RANGE_INSIDE;
	}
	else if (trueJumps.empty() && falseJumps.size() == 2)
	{
		// AND: 두 비교 모두 거짓이면 body 밖으로 간다.
		firstJump = &falseJumps[0];
		secondJump = &falseJumps[1];
		rangeOp = NOP_JMP_RANGE_OUTSIDE;
	}
	else
	{
		return false;
	}

	const int firstOpOffset = firstJump->_iCodePosOffset - (int)sizeof(OpType) - (int)sizeof(ArgFlag);
	const int secondOpOffset = secondJump->_iCodePosOffset - (int)sizeof(OpType) - (int)sizeof(ArgFlag);
	if (firstOpOffset < 0 || secondOpOffset != firstOpOffset + (int)sizeof(SVMOperation) ||
		funs._cur->_code->GetBufferOffset() != secondOpOffset + (int)sizeof(SVMOperation))
	{
		return false;
	}
	// 둘째 비교로 들어오는 점프가 있으면 합칠 수 없다. `a && x < lo || x > hi` 에서 a 가 거짓이면 둘째 비교만
	// 봐야 하는데, 합치면 그 점프의 대상이던 둘째 비교가 사라져 바로 몸통으로 떨어진다.
	if (funs._cur->_jumpTargetOffsets.count(secondOpOffset) != 0)
		return false;

	SVMOperation* firstOp = funs._cur->GetOPPointer(firstOpOffset);
	SVMOperation* secondOp = funs._cur->GetOPPointer(secondOpOffset);
	SRangeBoundCandidate firstCandidates[2];
	SRangeBoundCandidate secondCandidates[2];
	const bool buildInsideRange = rangeOp == NOP_JMP_RANGE_INSIDE;
	const int firstCount = BuildRangeBoundCandidates(*firstOp, firstJumpWhenTrue, buildInsideRange, firstCandidates);
	const int secondCount = BuildRangeBoundCandidates(*secondOp, secondJumpWhenTrue, buildInsideRange, secondCandidates);
	if (firstCount == 0 || secondCount == 0)
		return false;

	SRangeBoundCandidate lower;
	SRangeBoundCandidate upper;
	bool found = false;
	bool firstIsLower = false;
	for (int i = 0; i < firstCount && found == false; ++i)
	{
		for (int j = 0; j < secondCount; ++j)
		{
			if (firstCandidates[i]._test != secondCandidates[j]._test ||
				firstCandidates[i]._lower == secondCandidates[j]._lower)
				continue;
			lower = firstCandidates[i]._lower ? firstCandidates[i] : secondCandidates[j];
			upper = firstCandidates[i]._lower ? secondCandidates[j] : firstCandidates[i];
			firstIsLower = firstCandidates[i]._lower;
			found = true;
			break;
		}
	}
	if (found == false || funs._rangeJumps.size() > USHRT_MAX)
		return false;

	SRangeJumpCompile range;
	range._lower = lower._bound;
	range._upper = upper._bound;
	range._flags = (lower._inclusive ? NEOS_RANGE_LOWER_INCLUSIVE : 0) |
		(upper._inclusive ? NEOS_RANGE_UPPER_INCLUSIVE : 0) |
		(firstIsLower ? 0 : NEOS_RANGE_UPPER_FIRST);
	// 첫 논리 그룹의 비교 순서를 유지한다. 타입 오류가 나는 비교에서는 short-circuit
	// 순서도 스크립트 관찰 가능 동작이므로, 상한을 먼저 본 표현은 상한부터 검사한다.
	range._ownerFunction = funs._cur->_funID;
	const short rangeIndex = (short)funs._rangeJumps.size();
	funs._rangeJumps.push_back(range);

	// 첫 opcode를 재사용하고, 아직 어느 곳에서도 참조하지 않은 마지막 opcode는
	// write cursor를 되돌려 다음 emission이 덮어쓰게 한다. NOP_IDLE을 남기지 않는다.
	firstOp->SetCode(rangeOp);
	firstOp->argFlag = 0;
	firstOp->n1 = 0;
	firstOp->n2 = lower._test;
	firstOp->n3 = rangeIndex;
	funs._cur->_code->SetBufferOffset(secondOpOffset);
	outFalseJump.Set(firstOpOffset + (int)sizeof(OpType) + (int)sizeof(ArgFlag),
		firstOpOffset + (int)sizeof(SVMOperation));
	return true;
}

TK_TYPE ParseShortCircuitLogic(bool bReqReturn, SOperand& sResultStack, CArchiveRdWC& ar, SFunctions& funs, SVars& vars,
	TK_TYPE tkEnd1, TK_TYPE tkEnd2, TK_TYPE tkEnd3, TK_TYPE tkEnd4, std::vector<SJumpValue>* pOutFalseJumps)
{
	std::vector<SJumpValue> trueJumps;
	std::vector<SJumpValue> andFalseJumps;
	SOperand operand;
	bool hasLogicOperator = false;
	TK_TYPE endToken;

	for (;;)
	{
		operand.Reset();
		endToken = ParseJob(bReqReturn, operand, NULL, ar, funs, vars, false,
			tkEnd1, tkEnd2, tkEnd3, tkEnd4, NULL, TK_AND2, TK_OR2);
		if (endToken == TK_NONE)
			return TK_NONE; // 에러는 ParseJob 이 설정 (TK_NONE 을 종결로 쓰는 곳도 EOF=오류 상황)

		if (endToken != TK_AND2 && endToken != TK_OR2)
		{
			if (false == hasLogicOperator)
			{
				sResultStack = operand;
				return endToken;
			}
			break; // 마지막 그룹
		}

		hasLogicOperator = true;

		// 그룹 결과가 배열 접근(m[k])이면 값을 읽어와야 진리값 검사가 가능하다
		MaterializeValue(operand, ar, funs);

		if (endToken == TK_AND2)
		{
			// 비교식이면 "비교 + 거짓점프" 를 결합 명령 하나로(구: CMP 1개 + JF 1개).
			SJumpValue fused;
			if (TryFuseCompareJump(false, operand, funs, fused))
				andFalseJumps.push_back(fused);
			else
			{
				funs._cur->Push_JMPFalse(ar, operand._iVar, 0);
				andFalseJumps.emplace_back(funs._cur->_code->GetBufferOffset() - (int)(sizeof(short) * 3), funs._cur->_code->GetBufferOffset());
			}
			continue;
		}

		// TK_OR2 — 비교식이면 "비교 + 참점프" 를 결합 명령 하나로.
		{
			SJumpValue fused;
			if (TryFuseCompareJump(true, operand, funs, fused))
				trueJumps.push_back(fused);
			else
			{
				funs._cur->Push_JMPTrue(ar, operand._iVar, 0, ar.CurLine());
				trueJumps.emplace_back(funs._cur->_code->GetBufferOffset() - (int)(sizeof(short) * 3), funs._cur->_code->GetBufferOffset());
			}
		}

		const int nextOrGroup = funs._cur->_code->GetBufferOffset();
		for (const SJumpValue& jump : andFalseJumps)
			funs._cur->Set_JumpOffet(jump, nextOrGroup);
		andFalseJumps.clear();
	}

	// 마지막 그룹: false 분기는 false 값 생성으로 연결한다.
	MaterializeValue(operand, ar, funs);
	// 마지막 그룹의 false 분기도 비교식이면 결합 명령으로.
	{
		SJumpValue fused;
		if (TryFuseCompareJump(false, operand, funs, fused))
			andFalseJumps.push_back(fused);
		else
		{
			funs._cur->Push_JMPFalse(ar, operand._iVar, 0);
			andFalseJumps.emplace_back(funs._cur->_code->GetBufferOffset() - (int)(sizeof(short) * 3), funs._cur->_code->GetBufferOffset());
		}
	}

	// [분기 컨텍스트] 값이 필요 없는 조건식(if 등)이면 불린 materialize 를 생략한다.
	// true 경로는 바로 다음(몸통)으로 떨어지고, false 점프들은 호출자가 패치한다.
	// 이것만으로 조건식에서 MOV true / JMP / MOV false / 재검사 JF 4개 명령이 사라진다.
	if (pOutFalseJumps != NULL)
	{
		SJumpValue rangeFalseJump;
		if (TryFuseRangeJump(funs, trueJumps, andFalseJumps, rangeFalseJump))
		{
			trueJumps.clear();
			andFalseJumps.clear();
			andFalseJumps.push_back(rangeFalseJump);
		}
		const int bodyPos = funs._cur->_code->GetBufferOffset();
		for (const SJumpValue& jump : trueJumps)
			funs._cur->Set_JumpOffet(jump, bodyPos);
		*pOutFalseJumps = andFalseJumps;
		sResultStack = operand;      // 실제로 쓰이지 않음(유효성 검사 통과용)
		funs._cur->ClearLastOP();    // 마지막 OP 는 점프 — 뒤의 비교결합 최적화 대상이 아니다
		return endToken;
	}

	const int result = funs._cur->AllocLocalTempVar();
	const int trueValue = funs.AddStaticBool(true);
	const int falseValue = funs.AddStaticBool(false);
	const int truePos = funs._cur->_code->GetBufferOffset();
	funs._cur->Push_OP2(ar, NOP_MOV, result, trueValue, false);
	funs._cur->Push_JMP(ar, 0);
	const SJumpValue endJump(funs._cur->_code->GetBufferOffset() - (int)(sizeof(short) * 3), funs._cur->_code->GetBufferOffset());

	const int falsePos = funs._cur->_code->GetBufferOffset();
	for (const SJumpValue& jump : trueJumps)
		funs._cur->Set_JumpOffet(jump, truePos);
	for (const SJumpValue& jump : andFalseJumps)
		funs._cur->Set_JumpOffet(jump, falsePos);
	funs._cur->Push_OP2(ar, NOP_MOV, result, falseValue, false);
	funs._cur->Set_JumpOffet(endJump, funs._cur->_code->GetBufferOffset());

	sResultStack = SOperand(result);
	// 마지막 MOV는 false 경로 전용이다. 이후 return peephole이 이를 반환값으로 바꾸면
	// true 경로가 잘못된 위치로 빠지므로, 이 분기 블록에서는 마지막 OP 최적화를 막는다.
	funs._cur->ClearLastOP();
	return endToken;
}

OperationCode ConvertCheckOPToOptimize(OperationCode n)
{
	const TypedOpInfo* typed = GetTypedOpInfo(n);
	if (typed != nullptr) return typed->branchOp;
	switch (n)
	{
	case OP_EQUAL2:	// ==
		return OP_JMP_EQUAL2;
	case OP_LESS:		// <
		return OP_JMP_LESS;
	case OP_LESS_EQ:	// <=
		return OP_JMP_LESS_EQ;
	case OP_NEQUAL:	// !=
		return OP_JMP_NEQUAL;
	case OP_GREAT:		// >
		return OP_JMP_GREAT;
	case OP_GREAT_EQ:	// >=
		return OP_JMP_GREAT_EQ;
	default:
		break;
	}
	return NOP_NONE;
}
OperationCode ConvertCheckOPToOptimizeInv(OperationCode n)
{
	const TypedOpInfo* typed = GetTypedOpInfo(n);
	if (typed != nullptr) return typed->branchOp;
	switch (n)
	{
	case OP_EQUAL2:	// ==
		return OP_JMP_NEQUAL;
	case OP_LESS:
	case OP_LESS_EQ:
	case OP_GREAT:
	case OP_GREAT_EQ:
		return ConvertCheckOPToOptimize(n);
	case OP_NEQUAL:	// !=
		return OP_JMP_EQUAL2;
	default:
		break;
	}
	return NOP_NONE;
}



#ifdef CSTYLE_FOR
//	for Init
//	for {} Process
//	for Increase
//	for Check
bool ParseFor(CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
    ParserDepthGuard depth(ar);
	if (funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME && false == ar._allowGlobalInitLogic)
	{
		SetParserCompileError(ar, PCE_LOGIC_NOT_ALLOWED_GLOBAL, "for");
		return false;
	}

	std::string tk1;
	TK_TYPE tkType1;
	TK_TYPE r;

	SOperand iTempOffset;
	u8 byTempCheck[128 * 8];
	u8 byTempInc[128 * 8];
	debug_info DebugCheck[128];
	debug_info DebugInc[128];

	// "for (var i = 0; i < 789; i++)" 로직 처리
	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_SMALL) // (
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'(' after 'for'", tk1.c_str());
		return false;
	}

	AddLocalVar(vars.GetCurrentLayer());

	//==> for Init
	iTempOffset.Reset();
	r = ParseJob(false, iTempOffset, NULL, ar, funs, vars, true);
	if (TK_SEMICOLON != r) // 초기화
	{
		SetParserCompileError(ar, PCE_INVALID_FOR_INIT);
		return false;
	}

	funs._cur->Push_JMP(ar, 0); // for Check 위치로 JMP(일단은 위치만 확보)
	SJumpValue jmp1(funs._cur->_code->GetBufferOffset() - (sizeof(short)*3), funs._cur->_code->GetBufferOffset());

	int PosLoopTop = funs._cur->_code->GetBufferOffset(); // Loop 의 맨위

	// For Check
	int Pos1 = PosLoopTop;
	const std::size_t checkPlansBegin = funs._cur->_calls.size();
	iTempOffset.Reset();
	r = ParseShortCircuitLogic(true, iTempOffset, ar, funs, vars, TK_SEMICOLON, TK_COMMA, TK_R_SMALL, TK_R_ARRAY);
	if (TK_SEMICOLON != r)
	{
		SetParserCompileError(ar, PCE_INVALID_FOR_CONDITION);
		return false;
	}
	MaterializeValue(iTempOffset, ar, funs);
	int iStackCheckVar = iTempOffset._iVar;
	int Pos2 = funs._cur->_code->GetBufferOffset();
	OperationCode opCheck = funs._cur->GetLastOP();
	bool isCheckOPOpt = false;
	OperationCode opOpz = NOP_NONE;
	if (IsTempVar(iStackCheckVar))
	{
		opOpz = ConvertCheckOPToOptimize(opCheck);
		if (opOpz != NOP_NONE)
		{
			isCheckOPOpt = true;
		}
	}
	funs._cur->_code->SetPointer(Pos1, SEEK_SET);
	int iCheckCodeSize = Pos2 - Pos1;
	if (iCheckCodeSize > sizeof(byTempCheck))
	{
		SetParserCompileError(ar, PCE_CODE_BLOCK_TOO_LARGE, "for condition", iCheckCodeSize);
		return false;
	}
	funs._cur->_code->Read(byTempCheck, iCheckCodeSize);
	funs._cur->_code->SetPointer(Pos1, SEEK_SET);
	for (int i = 0; i < iCheckCodeSize / 8; i++)
		DebugCheck[i] = (*funs._cur->_pDebugData)[Pos1 / 8 + i];

	// For Increase
	const std::size_t incPlansBegin = funs._cur->_calls.size();
	iTempOffset = INVALID_ERROR_PARSEJOB;
	r = ParseJob(false, iTempOffset, NULL, ar, funs, vars); // 증감
	if (TK_R_SMALL != r)
	{
		SetParserCompileError(ar, PCE_INVALID_FOR_INCREMENT);
		return false;
	}
	Pos2 = funs._cur->_code->GetBufferOffset();
	funs._cur->_code->SetPointer(Pos1, SEEK_SET);
	int iIncCodeSize = Pos2 - Pos1;
	if (iIncCodeSize > sizeof(byTempInc))
	{
		SetParserCompileError(ar, PCE_CODE_BLOCK_TOO_LARGE, "for increment", iIncCodeSize);
		return false;
	}
	funs._cur->_code->Read(byTempInc, iIncCodeSize);
	funs._cur->_code->SetPointer(Pos1, SEEK_SET);
	for (int i = 0; i < iIncCodeSize / 8; i++)
		DebugInc[i] = (*funs._cur->_pDebugData)[Pos1 / 8 + i];
	const std::size_t incPlansEnd = funs._cur->_calls.size();

	//	for {} Process
	std::vector<SJumpValue> sJumps;
	std::vector<SJumpValue> sContinueJumps;
	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_MIDDLE) // {
	{
        ar.PushToken(tkType1, tk1);
        if (!ParseSingleStatement(&sJumps, ar, funs, vars, NULL, &sContinueJumps)) return false;
        ClearTempVars(funs);
    }
	else
	{
		if (false == ParseMiddleArea(&sJumps, ar, funs, vars, NULL, &sContinueJumps))
			return false;
	}

	// Write Inc Code
	int continuePos = funs._cur->_code->GetBufferOffset();
	for (int i = 0; i < (int)sContinueJumps.size(); i++)
	{
		funs._cur->Set_JumpOffet(sContinueJumps[i], continuePos);
	}
	funs._cur->_code->Write(byTempInc, iIncCodeSize);
	// 조건·증감식은 본문 뒤로 옮겨졌다. 그 안의 호출 계획 활성 위치도 같은 거리만큼 옮긴다.
	funs._cur->ShiftCallPlans(incPlansBegin, incPlansEnd, continuePos - Pos1);
	funs._cur->ShiftCallPlans(checkPlansBegin, incPlansBegin, continuePos + iIncCodeSize - Pos1);

	funs._cur->Set_JumpOffet(jmp1, funs._cur->_code->GetBufferOffset());
	// Write Check Code
	funs._cur->_code->Write(byTempCheck, iCheckCodeSize);
	if(false == isCheckOPOpt)
		funs._cur->Push_JMPTrue(ar, iStackCheckVar, PosLoopTop);
	else
	{
		int argLen = sizeof(short) * 3;
		funs._cur->_code->SetPointer(-((int)sizeof(OpType) + (int)sizeof(ArgFlag) + argLen), SEEK_CUR); // OP + flag + n1, n2, n3
		OpType optype = PrimaryOperation(opOpz);
		funs._cur->_code->Write(&optype, sizeof(optype));
		funs._cur->GetOPPointer(funs._cur->_code->GetBufferOffset() - sizeof(OpType))->SetCode(opOpz);
		// 부정 비교를 복사해 온 경우에도 결과 반전 비트를 그대로 보존한다.
		funs._cur->_code->SetPointer(static_cast<int>(sizeof(ArgFlag)), SEEK_CUR);
		int cur = funs._cur->_code->GetBufferOffset();
		funs._cur->Set_JumpOffet(SJumpValue(cur, cur + argLen), PosLoopTop);
		funs._cur->_code->SetPointer(argLen, SEEK_CUR);
	}
	funs._cur->ClearLastOP();


	int forEndPos = funs._cur->_code->GetBufferOffset();

	for (int i = 0; i < (int)sJumps.size(); i++)
	{
		funs._cur->Set_JumpOffet(sJumps[i], forEndPos);
	}

	DelLocalVar(vars.GetCurrentLayer());

	// Debug info Inc, check restore
	int iAddDebugCnt = iIncCodeSize / 8 + iCheckCodeSize / 8;
	int iOffDebug = forEndPos / 8 - iAddDebugCnt;
	funs._cur->_pDebugData->resize(iOffDebug + iAddDebugCnt);

	for (int i = 0; i < iIncCodeSize / 8; i++)
		(*funs._cur->_pDebugData)[iOffDebug + i] = DebugInc[i];
	for (int i = 0; i < iCheckCodeSize / 8; i++)
		(*funs._cur->_pDebugData)[iOffDebug + (iIncCodeSize / 8) + i] = DebugCheck[i];

	return true;
}
#else
// 범위 루프의 시작·끝·증분 슬롯을 정수로 맞추는 명령. 타입 검사기가 세 식을 모두 int로 확정하는 .nc에서는
// 할 일이 없어 내지 않는다. 내보내기가 빈 명령으로 바꾸던 자리가 그대로 사라진다. 동적 경로만 실수 경계를 바꾼다.
static void PushRangeIntConversion(CArchiveRdWC& ar, SFunctions& funs, int slot)
{
	if (ar.m_coreTypedIR != nullptr) return;
	funs._cur->Push_OP(ar, NOP_CHANGE_INT, slot, 0, 0);
}
bool ParseFor(CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
    ParserDepthGuard depth(ar);
	if (funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME && false == ar._allowGlobalInitLogic)
	{
		SetParserCompileError(ar, PCE_LOGIC_NOT_ALLOWED_GLOBAL, "for");
		return false;
	}

	std::string tk1;
	TK_TYPE tkType1;
	TK_TYPE r;

	SOperand iTempVar;

	// "for(var a in range(begin,end,step))" 로직 처리
	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_SMALL) // (
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'(' after 'for'", tk1.c_str());
		return false;
	}

	AddLocalVar(vars.GetCurrentLayer());

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_VAR) // var
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'var' in for-loop variable declaration", tk1.c_str());
		return false;
	}

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_STRING) // key
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "loop variable name", tk1.c_str());
		return false;
	}
	int iKey = AddLocalVarName(ar, funs, vars, false, false, tk1);
	if (iKey == -1)
		return false;

	//int iIterator1 = AddLocalVar(ar, funs, vars); // Current Save
	//if (iIterator1 == -1)
	//	return false;
	int i_Begin = AddLocalVar(ar, funs, vars); // Begin & iIterator1
	if (i_Begin == -1)
		return false;
	int i_End = AddLocalVar(ar, funs, vars); // End
	if (i_End == -1)
		return false;
	int i_Step = AddLocalVar(ar, funs, vars); // Step
	if (i_Step == -1)
		return false;


	//if (iKey + 1 != iIterator1)
	if (iKey + 1 != i_Begin)
	{
		SetParserCompileError(ar, PCE_INVALID_LOOP_VARIABLE_LAYOUT);
		return false;
	}

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_STRING || tk1 != "in") // in
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'in' in for-loop", tk1.c_str());
		return false;
	}

	int iTryValue = -1;
	if (Try_ParseIntNum(iTryValue, ar, funs, vars, TK_COMMA) != TK_NONE)
	{
#if 0
		if (IsShort(iTryValue))
			funs._cur->Push_OP2(ar, NOP_MOV, i_Begin, iTryValue, true);
		else
#endif
			funs._cur->Push_MOVI(ar, i_Begin, iTryValue);
	}
	else
	{
		iTempVar.Reset();
		r = ParseShortCircuitLogic(true, iTempVar, ar, funs, vars); // begin value
		if (TK_COMMA != r)
		{
			SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "',' after for-loop begin value", tk1.c_str());
			return false;
		}
		if (iTempVar.IsFun()) MaterializeValue(iTempVar, ar, funs);
		if (iTempVar._iArrayIndex == INVALID_ERROR_PARSEJOB)
			funs._cur->Push_OP2(ar, NOP_MOV, i_Begin, iTempVar._iVar, false);
		else
			ReadElement(iTempVar, i_Begin, ar, funs);
		PushRangeIntConversion(ar, funs, i_Begin);
	}

	int iDebugLoopLine = ar.CurLine();

	// end 값. 여기서 ')' 로 끝나면 step 이 생략된 형태다: for (var i in a, b)
	TK_TYPE tkEndTerm;
	iTryValue = -1;
	tkEndTerm = Try_ParseIntNum(iTryValue, ar, funs, vars, TK_COMMA, TK_R_SMALL);
	if (tkEndTerm != TK_NONE)
	{
#if 0
		if(IsShort(iTryValue))
			funs._cur->Push_OP2(ar, NOP_MOV, i_End, iTryValue, true);
		else
#endif
			funs._cur->Push_MOVI(ar, i_End, iTryValue);
	}
	else
	{
		iTempVar.Reset();
		r = ParseShortCircuitLogic(true, iTempVar, ar, funs, vars); // end value
		if (TK_COMMA != r && TK_R_SMALL != r)
		{
			SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "',' or ')' after for-loop end value", tk1.c_str());
			return false;
		}
		tkEndTerm = r;
		if (iTempVar.IsFun()) MaterializeValue(iTempVar, ar, funs);
		if (iTempVar._iArrayIndex == INVALID_ERROR_PARSEJOB)
			funs._cur->Push_OP2(ar, NOP_MOV, i_End, iTempVar._iVar, false);
		else
			ReadElement(iTempVar, i_End, ar, funs);
		PushRangeIntConversion(ar, funs, i_End);
	}

	// step 은 선택 인자. 생략하면 1 (Lua/Python 과 동일한 관례).
	// 값 종류(양수/음수/변수)와 무관하게 NOP_JMP_FOR 하나로 처리한다.
	// 방향은 런타임에 step 부호를 보고 정하고, step==0 은 런타임 에러다.
	if (tkEndTerm == TK_R_SMALL)
	{
		funs._cur->Push_MOVI(ar, i_Step, 1);   // step 생략 = 1
	}
	else
	{
		iTryValue = -1;
		if (Try_ParseIntNum(iTryValue, ar, funs, vars, TK_COMMA, TK_R_SMALL) != TK_NONE)
		{
#if 0
			if (IsShort(iTryValue))
				funs._cur->Push_OP2(ar, NOP_MOV, i_Step, iTryValue, true);
			else
#endif
				funs._cur->Push_MOVI(ar, i_Step, iTryValue);
		}
		else
		{
			iTempVar.Reset();
			r = ParseShortCircuitLogic(true, iTempVar, ar, funs, vars); // increase value
			if (TK_R_SMALL != r)
			{
				SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "')' after for-loop step value", tk1.c_str());
				return false;
			}
			if (iTempVar.IsFun()) MaterializeValue(iTempVar, ar, funs);
			if (iTempVar._iArrayIndex == INVALID_ERROR_PARSEJOB)
				funs._cur->Push_OP2(ar, NOP_MOV, i_Step, iTempVar._iVar, false);
			else
				ReadElement(iTempVar, i_Step, ar, funs);
			PushRangeIntConversion(ar, funs, i_Step);
		}
	}


	//funs._cur->Push_OP2(ar, NOP_MOV, iIterator1, i_Begin, false); // Cur_inter = Begin

	funs._cur->Push_JMP(ar, 0); // for Check 위치로 JMP(일단은 위치만 확보)
	SJumpValue jmp1(funs._cur->_code->GetBufferOffset() - (sizeof(short) * 3), funs._cur->_code->GetBufferOffset());

	int PosLoopTop = funs._cur->_code->GetBufferOffset(); // Loop 의 맨위


	//	for {} Process
	std::vector<SJumpValue> sJumps;
	std::vector<SJumpValue> sContinueJumps;
	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_MIDDLE) // {
	{
        ar.PushToken(tkType1, tk1);
        if (!ParseSingleStatement(&sJumps, ar, funs, vars, NULL, &sContinueJumps)) return false;
        ClearTempVars(funs);
    }
	else
	{
		if (false == ParseMiddleArea(&sJumps, ar, funs, vars, NULL, &sContinueJumps))
			return false;
	}

	funs._cur->Set_JumpOffet(jmp1, funs._cur->_code->GetBufferOffset());
	int continuePos = funs._cur->_code->GetBufferOffset();
	for (int i = 0; i < (int)sContinueJumps.size(); i++)
	{
		funs._cur->Set_JumpOffet(sContinueJumps[i], continuePos);
	}
	funs._cur->Push_JMPFor(ar, PosLoopTop, iKey, iKey + 2, iDebugLoopLine);

	funs._cur->ClearLastOP();


	int forEndPos = funs._cur->_code->GetBufferOffset();

	for (int i = 0; i < (int)sJumps.size(); i++)
	{
		funs._cur->Set_JumpOffet(sJumps[i], forEndPos);
	}

	DelLocalVar(vars.GetCurrentLayer());

	return true;
}
#endif

bool ParseForEach(CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
    ParserDepthGuard depth(ar);
	if (funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME && false == ar._allowGlobalInitLogic)
	{
		SetParserCompileError(ar, PCE_LOGIC_NOT_ALLOWED_GLOBAL, "foreach");
		return false;
	}

	std::string tk1;
	TK_TYPE tkType1;
	TK_TYPE r;

	SOperand iTempOffset;

	int iDebugLoopLine = ar.CurLine();

	// "foreach(var a, b in table)" 로직 처리
	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_SMALL) // (
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'(' after 'foreach'", tk1.c_str());
		return false;
	}

	AddLocalVar(vars.GetCurrentLayer());

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_VAR) // var
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'var' in foreach variable declaration", tk1.c_str());
		return false;
	}

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_STRING) // key
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "foreach key variable name", tk1.c_str());
		return false;
	}
	int iKey = AddLocalVarName(ar, funs, vars, false, false, tk1);
	if(iKey == -1)
		return false;

	int iValue = -1;
	bool bTwoVar = false; // foreach(var k, v in ...) 형태인지 (value 이름이 선언됐는지)
	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_COMMA) // ,
	{
		if (tkType1 == TK_STRING && tk1 == "in")
		{
			iValue = AddLocalVar(ar, funs, vars);
			ar.PushToken(tkType1, tk1);
		}
		else
		{
			SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "',' or 'in' in foreach declaration", tk1.c_str());
			return false;
		}
	}
	else
	{
		tkType1 = GetToken(ar, tk1);
		if (tkType1 != TK_STRING) // value
		{
			SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "foreach value variable name", tk1.c_str());
			return false;
		}
		iValue = AddLocalVarName(ar, funs, vars, false, false, tk1);
		bTwoVar = true;
	}
	if (iValue == -1)
		return false;

	if (iKey + 1 != iValue)
	{
		SetParserCompileError(ar, PCE_INVALID_LOOP_VARIABLE_LAYOUT);
		return false;
	}

	// Iterator를 저장할 임시 자동 생성 변수
	int iIterator = AddLocalVar(ar, funs, vars);
	if (iIterator == -1)
		return false;

	if (iKey + 2 != iIterator)
	{
		SetParserCompileError(ar, PCE_INVALID_LOOP_VARIABLE_LAYOUT);
		return false;
	}
	// foreach의 구조 변경 감지용 숨김 로컬. key/value/iterator 뒤에 연속 배치되어
	// VM은 OP 크기 변경 없이 key 슬롯 + 3으로 버전 슬롯을 찾는다.
	int iMutationVersion = AddLocalVar(ar, funs, vars);
	if (iMutationVersion == -1 || iKey + 3 != iMutationVersion)
	{
		SetParserCompileError(ar, PCE_INVALID_LOOP_VARIABLE_LAYOUT);
		return false;
	}

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_STRING || tk1 != "in") // in
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'in' in foreach declaration", tk1.c_str());
		return false;
	}
	/*
	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_STRING) // Table Name
	{
		SetCompileError(ar, "Error (%d, %d): foreach 'table_name' != %s", ar.CurLine(), ar.CurCol(), tk1.c_str());
		return false;
	}
	int iTable = vars.FindVar(tk1);
	if(iTable == -1)
	{
		SetCompileError(ar, "Error (%d, %d): foreach 'talbe' Not Found %s", ar.CurLine(), ar.CurCol(), tk1.c_str());
		return false;
	}
	*/
	int iTable = -1;
	SOperand operand;
	r = ParseShortCircuitLogic(true, operand, ar, funs, vars);
	if (r != TK_R_SMALL) // )
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "')' after foreach source expression", tk1.c_str());
		return false;
	}
	if (operand.IsInvalidValue())
	{
		SetParserCompileError(ar, PCE_EXPECTED_EXPRESSION);
		return false;
	}
	if (operand.IsFun()) MaterializeValue(operand, ar, funs);
	// Retain every source once, including plain locals/globals/parameters. The
	// body may rebind that variable or reuse an expression's temporary slot.
	// This copies the reference, preserving element updates and mutation checks.
	iTable = AddLocalVar(ar, funs, vars);
	if (iTable == -1)
		return false;
	ReadElement(operand, iTable, ar, funs);


	funs._cur->Push_OP1(ar, NOP_VAR_CLEAR, iKey);
	// 반복자 슬롯도 진입 시 반드시 비운다. ForEach 런타임은 "이 슬롯이 VAR_ITERATOR 인가"로
	// 순회 시작/계속을 판별하는데, break/return 으로 루프를 빠져나가면 슬롯이 VAR_ITERATOR 인
	// 채로 남는다. 로컬 슬롯은 호출 간에 정리되지 않으므로, 같은 슬롯 번호를 쓰는 다음 foreach
	// 가 이를 "순회 중"으로 오인해 남은 offset 부터 이어가 버린다(같은 입력에 다른 결과).
	// 루프당 1회 비용이라 반복 비용에는 영향이 없다.
	funs._cur->Push_MOVI(ar, iMutationVersion, -1);

	funs._cur->Push_JMP(ar, 0); // for Check 위치로 JMP(일단은 위치만 확보)
	SJumpValue jmp1(funs._cur->_code->GetBufferOffset() - (sizeof(short) * 3), funs._cur->_code->GetBufferOffset());

	int PosLoopTop = funs._cur->_code->GetBufferOffset(); // Loop 의 맨위


	//	foreach {} Process
	std::vector<SJumpValue> sJumps;
	std::vector<SJumpValue> sContinueJumps;
	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_MIDDLE) // {
	{
        ar.PushToken(tkType1, tk1);
        if (!ParseSingleStatement(&sJumps, ar, funs, vars, NULL, &sContinueJumps)) return false;
        ClearTempVars(funs);
    }
	else
	{
		if (false == ParseMiddleArea(&sJumps, ar, funs, vars, NULL, &sContinueJumps))
			return false;
	}

	funs._cur->Set_JumpOffet(jmp1, funs._cur->_code->GetBufferOffset());
	int continuePos = funs._cur->_code->GetBufferOffset();
	for (int i = 0; i < (int)sContinueJumps.size(); i++)
	{
		funs._cur->Set_JumpOffet(sContinueJumps[i], continuePos);
	}
	funs._cur->Push_JMPForEach(ar, PosLoopTop, iTable, iKey, iDebugLoopLine, bTwoVar);

	funs._cur->ClearLastOP();


	int forEndPos = funs._cur->_code->GetBufferOffset();

	for (int i = 0; i < (int)sJumps.size(); i++)
	{
		funs._cur->Set_JumpOffet(sJumps[i], forEndPos);
	}

	// Normal completion and break release the retained source. At file scope
	// this hidden slot is global, so scope-name cleanup alone would retain it.
	funs._cur->Push_OP1(ar, NOP_VAR_CLEAR, iTable);

	DelLocalVar(vars.GetCurrentLayer());

	return true;
}
bool ParseWhile(CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
    ParserDepthGuard depth(ar);
	if (funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME && false == ar._allowGlobalInitLogic)
	{
		SetParserCompileError(ar, PCE_LOGIC_NOT_ALLOWED_GLOBAL, "while");
		return false;
	}

	std::string tk1;
	TK_TYPE tkType1;

	SOperand iTempOffset;
	std::vector<u8> byTempCheck;
	std::vector<debug_info> DebugCheck;

	int iDebugLoopLine = ar.CurLine();

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_SMALL) // (
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'(' after 'while'", tk1.c_str());
		return false;
	}

	AddLocalVar(vars.GetCurrentLayer());


	funs._cur->Push_JMP(ar, 0); // for Check 위치로 JMP(일단은 위치만 확보)
	SJumpValue jmp1(funs._cur->_code->GetBufferOffset() - (sizeof(short) * 3), funs._cur->_code->GetBufferOffset());

	int PosLoopTop = funs._cur->_code->GetBufferOffset(); // Loop 의 맨위

	// While Check
	int Pos1 = PosLoopTop;
	const std::size_t checkPlansBegin = funs._cur->_calls.size();
	iTempOffset.Reset();
	if (TK_R_SMALL != ParseShortCircuitLogic(true, iTempOffset, ar, funs, vars))
	{
		return false;
	}
	if (iTempOffset.IsInvalidValue())
	{
		SetParserCompileError(ar, PCE_EXPECTED_EXPRESSION);
		return false;
	}
	// 조건이 컨테이너 읽기(a[i]) 하나뿐이면 값을 꺼내야 한다. 그대로 점프에
	// 넘기면 배열 객체 자체를 검사하게 되는데, VAR_ARRAY 는 IsTrue 가 늘 거짓이라
	// while (bools[i]) 가 한 번도 참이 되지 않는다. 이 명령은 조건 코드 구간
	// (Pos1~Pos2) 안에 들어가야 루프마다 다시 읽는다.
	MaterializeValue(iTempOffset, ar, funs);
	int iStackCheckVar = iTempOffset._iVar;
	int Pos2 = funs._cur->_code->GetBufferOffset();
	const std::size_t checkPlansEnd = funs._cur->_calls.size();
	OperationCode opCheck = funs._cur->GetLastOP();
	bool isCheckOPOpt = false;
	OperationCode opOpz = NOP_NONE;
	if (IsTempVar(iStackCheckVar))
	{
		opOpz = ConvertCheckOPToOptimize(opCheck);
		if (opOpz != NOP_NONE)
		{
			isCheckOPOpt = true;
		}
	}
	funs._cur->_code->SetPointer(Pos1, SEEK_SET);
	int iCheckCodeSize = (int)Pos2 - Pos1;
	byTempCheck.resize(iCheckCodeSize);
	if (ar._debug) DebugCheck.resize(iCheckCodeSize / 8);
	funs._cur->_code->Read(byTempCheck.data(), iCheckCodeSize);
	funs._cur->_code->SetPointer(Pos1, SEEK_SET);
	if (ar._debug)
	{
		for (int i = 0; i < iCheckCodeSize / 8; i++)
			DebugCheck[i] = (*funs._cur->_pDebugData)[Pos1 / 8 + i];
	}


	//	while {} Process
	std::vector<SJumpValue> sJumps;
	std::vector<SJumpValue> sContinueJumps;
	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_MIDDLE) // {
	{
        ar.PushToken(tkType1, tk1);
        if (!ParseSingleStatement(&sJumps, ar, funs, vars, NULL, &sContinueJumps)) return false;
        ClearTempVars(funs);
    }
	else
	{
		if (false == ParseMiddleArea(&sJumps, ar, funs, vars, NULL, &sContinueJumps))
			return false;
	}

	funs._cur->Set_JumpOffet(jmp1, funs._cur->_code->GetBufferOffset());
	int continuePos = funs._cur->_code->GetBufferOffset();
	for (int i = 0; i < (int)sContinueJumps.size(); i++)
	{
		funs._cur->Set_JumpOffet(sContinueJumps[i], continuePos);
	}
	funs._cur->_code->Write(byTempCheck.data(), iCheckCodeSize);
	// 조건은 본문 뒤로 옮겨졌다. 그 안의 호출 계획 활성 위치도 같은 거리만큼 옮긴다.
	funs._cur->ShiftCallPlans(checkPlansBegin, checkPlansEnd, continuePos - Pos1);
	if (false == isCheckOPOpt)
		funs._cur->Push_JMPTrue(ar, iStackCheckVar, PosLoopTop, iDebugLoopLine);
	else
	{
		int argLen = sizeof(short) * 3;
		funs._cur->_code->SetPointer(-((int)sizeof(OpType) + (int)sizeof(ArgFlag) + argLen), SEEK_CUR); // OP + n1, n2, n3
		OpType optype = PrimaryOperation(opOpz);
		funs._cur->_code->Write(&optype, sizeof(optype));
		funs._cur->GetOPPointer(funs._cur->_code->GetBufferOffset() - sizeof(OpType))->SetCode(opOpz);
		// 부정 비교를 복사해 온 경우에도 결과 반전 비트를 그대로 보존한다.
		funs._cur->_code->SetPointer(static_cast<int>(sizeof(ArgFlag)), SEEK_CUR);
		int cur = funs._cur->_code->GetBufferOffset();
		funs._cur->Set_JumpOffet(SJumpValue(cur, cur + argLen), PosLoopTop);
		funs._cur->_code->SetPointer(argLen, SEEK_CUR);
	}
	funs._cur->ClearLastOP();

	int forEndPos = funs._cur->_code->GetBufferOffset();

	for (int i = 0; i < (int)sJumps.size(); i++)
	{
		funs._cur->Set_JumpOffet(sJumps[i], forEndPos);
	}

	DelLocalVar(vars.GetCurrentLayer());

	// Debug info Inc, check restore
	if(ar._debug)
	{
		int iAddDebugCnt = iCheckCodeSize / 8;
		int iOffDebug = forEndPos / 8 - iAddDebugCnt;
		funs._cur->_pDebugData->resize(iOffDebug + iAddDebugCnt);

		for (int i = 0; i < iCheckCodeSize / 8; i++)
			(*funs._cur->_pDebugData)[iOffDebug + i] = DebugCheck[i];
	}
	return true;
}
bool ParseIF(std::vector<SJumpValue>* pJumps, CArchiveRdWC& ar, SFunctions& funs, SVars& vars, bool* alwaysReturns, std::vector<SJumpValue>* pContinueJumps = NULL)
{
    ParserDepthGuard depth(ar);
	if (funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME && false == ar._allowGlobalInitLogic)
	{
		SetParserCompileError(ar, PCE_LOGIC_NOT_ALLOWED_GLOBAL, "if");
		return false;
	}
	if(alwaysReturns) *alwaysReturns = false;

	std::string tk1;
	TK_TYPE tkType1;

	SOperand iTempOffset;

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_SMALL) // (
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'(' after 'if'", tk1.c_str());
		return false;
	}

	//==> if( xxx )
	iTempOffset.Reset();
	// 조건식은 분기 컨텍스트로 파싱한다. &&/|| 가 있으면 불린 값을 만들지 않고
	// false 점프 목록(condFalseJumps)만 받아 아래에서 직접 패치한다.
	std::vector<SJumpValue> condFalseJumps;
	if (TK_R_SMALL != ParseShortCircuitLogic(true, iTempOffset, ar, funs, vars,
		TK_SEMICOLON, TK_COMMA, TK_R_SMALL, TK_R_ARRAY, &condFalseJumps))
	{
		return false;
	}
	if (iTempOffset.IsInvalidValue())
	{
		SetParserCompileError(ar, PCE_EXPECTED_EXPRESSION);
		return false;
	}

	// 조건이 컨테이너 읽기(a[i]) 하나뿐이면 값을 꺼내야 한다. 여기까지 오는
	// 피연산자는 베이스 슬롯과 키를 따로 들고 있어서, 그대로 점프에 넘기면
	// 배열 객체 자체를 검사하게 된다 - VAR_ARRAY 는 IsTrue 가 늘 거짓이라
	// if (bools[i]) 가 한 번도 참이 되지 않았다. &&/|| 가 있는 조건은
	// ParseShortCircuitLogic 안에서 이미 같은 처리를 한다.
	MaterializeValue(iTempOffset, ar, funs);
	SJumpValue jmp1;
	SJumpValue jmp2;

	// 논리 연산자가 있어 분기 목록을 받은 경우 — jmp1 대신 그 목록 전체를 패치한다.
	const bool blCondBranch = !condFalseJumps.empty();
	auto SetCondFalseTarget = [&](int iPos)
	{
		if (blCondBranch)
		{
			for (const SJumpValue& jump : condFalseJumps)
				funs._cur->Set_JumpOffet(jump, iPos);
		}
		else
		{
			funs._cur->Set_JumpOffet(jmp1, iPos);
		}
	};

	OperationCode opCheck = funs._cur->GetLastOP();
	bool isCheckOPOpt = false;
	OperationCode opOpz = NOP_NONE;
	if (false == blCondBranch && IsTempVar(iTempOffset._iVar))
	{
		opOpz = ConvertCheckOPToOptimizeInv(opCheck);
		if (opOpz != NOP_NONE)
		{
			isCheckOPOpt = true;
		}
	}
	if (blCondBranch)
	{
		// 분기 코드는 ParseShortCircuitLogic 이 이미 생성했다. 여기서 추가할 점프가 없다.
	}
	else if (false == isCheckOPOpt)
	{
		funs._cur->Push_JMPFalse(ar, iTempOffset._iVar, 0);
		jmp1.Set(funs._cur->_code->GetBufferOffset() - 6, funs._cur->_code->GetBufferOffset()); // before "- 2"
	}
	else
	{
		int argLen = sizeof(short) * 3;
		funs._cur->_code->SetPointer(-((int)sizeof(OpType) + (int)sizeof(ArgFlag) + argLen), SEEK_CUR); // OP + flag +  n1, n2, n3
		
		OpType optype = PrimaryOperation(opOpz);
		funs._cur->_code->Write(&optype, sizeof(optype));
		funs._cur->GetOPPointer(funs._cur->_code->GetBufferOffset() - sizeof(OpType))->SetCode(opOpz);
		if (UsesCompareInvert(opCheck))
			funs._cur->GetOPPointer(funs._cur->_code->GetBufferOffset() - sizeof(OpType))->argFlag ^= NEOS_OP_COMPARE_INVERT;
		funs._cur->_code->SetPointer((int)sizeof(ArgFlag), SEEK_CUR);

		int cur = funs._cur->_code->GetBufferOffset();
		funs._cur->SetN(cur - (int)sizeof(OpType) - (int)sizeof(ArgFlag), 0, 0);
		funs._cur->_code->SetPointer(argLen, SEEK_CUR);

		jmp1.Set(funs._cur->_code->GetBufferOffset() - 6, funs._cur->_code->GetBufferOffset());
	}

	bool thenAlwaysReturns = false;
	tkType1 = GetToken(ar, tk1);
	if (tkType1 == TK_L_MIDDLE)
	{
		AddLocalVar(vars.GetCurrentLayer());

		if (false == ParseMiddleArea(pJumps, ar, funs, vars, &thenAlwaysReturns, pContinueJumps))
			return false;

		DelLocalVar(vars.GetCurrentLayer());
	}
	else
	{
            ar.PushToken(tkType1, tk1);
            if (!ParseSingleStatement(pJumps, ar, funs, vars, &thenAlwaysReturns, pContinueJumps)) return false;
        }

	ClearTempVars(funs);

	tkType1 = GetToken(ar, tk1);
	// else 바로 뒤의 if 는 조건 분기 체인으로 처리한다.
	bool blElseIf = false;
	if (tkType1 == TK_ELSE)
	{
		std::string tkNext;
		TK_TYPE tkNextType = GetToken(ar, tkNext);
		if (tkNextType == TK_IF)
			blElseIf = true;
		else
			ar.PushToken(tkNextType, tkNext);
	}
	if (blElseIf)
	{
		// 마지막 OP 가 return 인 것과 분기의 모든 경로가 반환하는 것은 다르다.
		// 중첩 if 의 거짓 경로는 내려올 수 있으므로, 구문으로 반환을 증명할 때만 점프를 뺀다.
		if (!thenAlwaysReturns)
		{
			funs._cur->Push_JMP(ar, 0);
			jmp2.Set(funs._cur->_code->GetBufferOffset() - 6, funs._cur->_code->GetBufferOffset());
		}

		SetCondFalseTarget(funs._cur->_code->GetBufferOffset());

		bool elseAlwaysReturns = false;
		if (false == ParseIF(pJumps, ar, funs, vars, &elseAlwaysReturns, pContinueJumps))
			return false;

		ClearTempVars(funs);
		if (!thenAlwaysReturns)
			funs._cur->Set_JumpOffet(jmp2, funs._cur->_code->GetBufferOffset());
		// 뒤쪽 체인만 반환한다고 함수의 기본 반환까지 생략해서는 안 된다.
		if (alwaysReturns) *alwaysReturns = thenAlwaysReturns && elseAlwaysReturns;
	}
	else if (tkType1 == TK_ELSE)
	{
		// then 전체가 반환할 때만 실행되지 않을 점프를 생략한다.
		if (!thenAlwaysReturns)
		{
			funs._cur->Push_JMP(ar, 0);
			jmp2.Set(funs._cur->_code->GetBufferOffset() - 6, funs._cur->_code->GetBufferOffset());
		}
		SetCondFalseTarget(funs._cur->_code->GetBufferOffset());

		bool elseAlwaysReturns = false;
		tkType1 = GetToken(ar, tk1);
		if (tkType1 == TK_L_MIDDLE)
		{
			AddLocalVar(vars.GetCurrentLayer());

			if (false == ParseMiddleArea(pJumps, ar, funs, vars, &elseAlwaysReturns, pContinueJumps))
				return false;

			DelLocalVar(vars.GetCurrentLayer());
		}
		else
		{
            ar.PushToken(tkType1, tk1);
            if (!ParseSingleStatement(pJumps, ar, funs, vars, &elseAlwaysReturns, pContinueJumps)) return false;
        }
		ClearTempVars(funs);
		if (!thenAlwaysReturns)
			funs._cur->Set_JumpOffet(jmp2, funs._cur->_code->GetBufferOffset());
		if (alwaysReturns) *alwaysReturns = thenAlwaysReturns && elseAlwaysReturns;
	}
	else
	{
		ar.PushToken(tkType1, tk1);
		SetCondFalseTarget(funs._cur->_code->GetBufferOffset());
	}
	return true;
}

bool ParseVarDef(CArchiveRdWC& ar, SFunctions& funs, SVars& vars, bool blExport, bool blPublic)
{
    std::string name, token;
    if (GetToken(ar, name) != TK_STRING || !UseableName(ar, funs, vars, name, true))
    {
        SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "variable name after 'var'", name.c_str());
        return false;
    }
    const TK_TYPE next = GetToken(ar, token);
    if (next == TK_SEMICOLON)
    {
        const int slot = AddLocalVarName(ar, funs, vars, blExport, blPublic, name);
        if (slot == -1) return false;
        funs._cur->Push_OP1(ar, NOP_VAR_CLEAR, slot);
        return true;
    }
    if (next != TK_EQUAL)
    {
        SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'=' or ';' after variable name", token.c_str());
        return false;
    }
    SOperand value;
    if (ParseShortCircuitLogic(true, value, ar, funs, vars) != TK_SEMICOLON || value.IsInvalidValue())
    {
        SetParserCompileError(ar, PCE_EXPECTED_EXPRESSION);
        return false;
    }
    const int slot = AddLocalVarName(ar, funs, vars, blExport, blPublic, name);
    if (slot == -1) return false;
    if (value.IsFun())
        funs._cur->Push_OP2(ar, NOP_FMOV1, slot, value._iVar, false);
    else if (value.IsArray())
        ReadElement(value, slot, ar, funs);
    else if (!funs._cur->RetargetConditional(slot, value._iVar) &&
        !TryStoreCallResult(ar, funs, slot, value) && !TryPushImmediate(ar, funs, slot, value))
        funs._cur->Push_OP2(ar, NOP_MOV, slot, value._iVar, value.IsShort());
    return true;
}

// ---------------- 스크립트 const (컴파일타임 상수 선언) ----------------
// const NAME = <상수 표현식>;  →  모듈-로컬 define 으로 등록되어 이후 토큰 치환.
// 표현식은 컴파일 타임에 평가 (리터럴, 기존 define/const, 산술/비트 연산, 괄호).
struct SConstValue
{
	NeoCompileDefineTokenType type = NEO_DEFINE_TOKEN_NULL;
	NS_INT i = 0;
	double f = 0;
	std::string s;

	bool IsNum() const { return type == NEO_DEFINE_TOKEN_INT || type == NEO_DEFINE_TOKEN_FLOAT; }
	double Num() const { return type == NEO_DEFINE_TOKEN_INT ? (double)i : f; }
};

static bool ParseConstExpr(SConstValue& out, int minPrec, CArchiveRdWC& ar, SFunctions& funs);

static bool ParseConstPrimary(SConstValue& out, CArchiveRdWC& ar, SFunctions& funs, bool allowMinMagnitude = false)
{
	ParserDepthGuard depth(ar);
	std::string tk;
	TK_TYPE tkType = GetToken(ar, tk);
	if (tkType == TK_STRING)
	{
		const auto module = funs._curModule->_defModules.find(tk);
		if (module != funs._curModule->_defModules.end())
		{
			const std::string alias = tk;
			if (!ReadModuleMember(ar, tk)) return false;
			const auto& values = module->second->_exportedConsts.values;
			const auto constant = values.find(tk);
			if (constant == values.end())
			{
				SetParserCompileError(ar, PCE_MODULE_CONST, alias.c_str(), tk.c_str());
				return false;
			}
			tkType = CompileDefineTokenToToken(constant->second, tk);
		}
	}

	switch (tkType)
	{
	case TK_STRING:
	{
		ParsedNumber num;
		if (false == StringToNumber(num, tk.c_str(), allowMinMagnitude))
		{
			// define/const 로 치환되지 않은 식별자 → 컴파일 타임 상수가 아님
			if (!tk.empty() && tk[0] >= '0' && tk[0] <= '9')
                SetCompileError(ar, "invalid or out-of-range integer literal '%s'", tk.c_str());
            else
                SetParserCompileError(ar, PCE_CONST_INVALID_VALUE, tk.c_str());
			return false;
		}
		bool isFloat = (num.type == ParsedNumber::Type::Float); // 통째로 들어온 "1.5", "1e+20" 등
		double fval = num.floatValue;
		if (false == isFloat && ar.GetData(false) == '.')
		{
			// 토크나이저는 "1.5" 를 "1" '.' "5" 로 쪼갠다 (ParseNum 과 동일 처리)
			ar.GetData(true);
			std::string tk2;
			GetToken(ar, tk2);
			double num2 = 0;
			if (false == StringToDoubleLow(num2, tk2.c_str()))
			{
				SetParserCompileError(ar, PCE_INVALID_NUMBER_LITERAL);
				return false;
			}
			fval += num2;
			isFloat = true;
		}
		if (isFloat) { out.type = NEO_DEFINE_TOKEN_FLOAT; out.f = fval; }
		else         { out.type = NEO_DEFINE_TOKEN_INT;   out.i = num.intValue; } // 정수 리터럴의 64비트 값을 보존한다.
		return true;
	}
	case TK_TRUE:  out.type = NEO_DEFINE_TOKEN_TRUE;  return true;
	case TK_FALSE: out.type = NEO_DEFINE_TOKEN_FALSE; return true;
	case TK_NULL:  out.type = NEO_DEFINE_TOKEN_NULL;  return true;
	case TK_PLUS:
		if (false == ParseConstPrimary(out, ar, funs))
			return false;
		if (false == out.IsNum())
		{
			SetParserCompileError(ar, PCE_CONST_INVALID_OP, "+");
			return false;
		}
		return true;
	case TK_MINUS:
		if (false == ParseConstPrimary(out, ar, funs, true))
			return false;
		if (out.type == NEO_DEFINE_TOKEN_INT) { out.i = NegateParsedInteger(out.i); return true; }
		if (out.type == NEO_DEFINE_TOKEN_FLOAT) { out.f = -out.f; return true; }
		SetParserCompileError(ar, PCE_CONST_INVALID_OP, "-");
		return false;
	case TK_NOT: // ~
		if (false == ParseConstPrimary(out, ar, funs))
			return false;
		if (out.type != NEO_DEFINE_TOKEN_INT)
		{
			SetParserCompileError(ar, PCE_CONST_INVALID_OP, "~");
			return false;
		}
		out.i = ~out.i;
		return true;
	case TK_L_SMALL:
		if (false == ParseConstExpr(out, 0, ar, funs))
			return false;
		tkType = GetToken(ar, tk);
		if (tkType != TK_R_SMALL)
		{
			SetParserCompileError(ar, PCE_EXPECTED_RIGHT_PAREN);
			return false;
		}
		return true;
	case TK_QUOTE2:
	case TK_QUOTE1:
		if (false == GetQuotationString(ar, out.s, tkType == TK_QUOTE2 ? '"' : '\''))
		{
			SetParserCompileError(ar, PCE_UNTERMINATED_STRING);
			return false;
		}
		out.type = NEO_DEFINE_TOKEN_STRING;
		return true;
	case TK_STRING_LITERAL: // define/const 치환으로 완성된 문자열
		out.type = NEO_DEFINE_TOKEN_STRING;
		out.s = tk;
		return true;
	default:
		SetParserCompileError(ar, PCE_CONST_INVALID_VALUE, tk.c_str());
		return false;
	}
}

static int ConstBinOpPrec(TK_TYPE t)
{
	switch (t)
	{
	case TK_PLUS: case TK_MINUS: return 5;
	case TK_MUL: case TK_DIV: case TK_PERCENT: return 6;
	case TK_XOR: return 2;
	case TK_LSHIFT: case TK_RSHIFT: return 4;
	case TK_AND: return 3;
	case TK_OR: return 1;
	default: return 0;
	}
}

static bool EvalConstBinOp(SConstValue& a, TK_TYPE op, const SConstValue& b, CArchiveRdWC& ar)
{
	if (false == a.IsNum() || false == b.IsNum())
	{
		SetParserCompileError(ar, PCE_CONST_INVALID_OP, GetTokenString(op).c_str());
		return false;
	}
	bool bothInt = (a.type == NEO_DEFINE_TOKEN_INT && b.type == NEO_DEFINE_TOKEN_INT);
	switch (op)
	{
	case TK_PLUS:
	case TK_MINUS:
	case TK_MUL:
		if (bothInt)
			a.i = (op == TK_PLUS) ? (NeoIntAdd(a.i, b.i)) : (op == TK_MINUS) ? (NeoIntSub(a.i, b.i)) : (NeoIntMul(a.i, b.i));
		else
		{
			double x = a.Num(), y = b.Num();
			a.type = NEO_DEFINE_TOKEN_FLOAT;
			a.f = (op == TK_PLUS) ? (x + y) : (op == TK_MINUS) ? (x - y) : (x * y);
		}
		return true;
	case TK_DIV:
	case TK_PERCENT:
		if (op == TK_PERCENT && !bothInt)
		{
			SetParserCompileError(ar, PCE_CONST_INVALID_OP, GetTokenString(op).c_str());
			return false;
		}
		if ((bothInt && b.i == 0) || (false == bothInt && b.Num() == 0))
		{
			SetParserCompileError(ar, PCE_CONST_DIV_ZERO);
			return false;
		}
		if (bothInt)
		{
			if (a.i == NS_INT_MIN && b.i == -1)
			{
				SetParserCompileError(ar, PCE_CONST_DIV_OVERFLOW);
				return false;
			}
			a.i = (op == TK_DIV) ? (a.i / b.i) : (a.i % b.i);
		}
		else
		{
			double x = a.Num(), y = b.Num();
			a.type = NEO_DEFINE_TOKEN_FLOAT;
			a.f = x / y;
		}
		return true;
	case TK_XOR: case TK_LSHIFT: case TK_RSHIFT: case TK_AND: case TK_OR:
		if (false == bothInt)
		{
			SetParserCompileError(ar, PCE_CONST_INVALID_OP, GetTokenString(op).c_str());
			return false;
		}
		if ((op == TK_LSHIFT || op == TK_RSHIFT) && static_cast<NS_UINT>(b.i) >= 64)
		{
			SetParserCompileError(ar, PCE_CONST_SHIFT_RANGE);
			return false;
		}
		switch (op)
		{
		case TK_XOR:    a.i ^= b.i;  break;
		case TK_LSHIFT: a.i = NeoShiftLeft(a.i, b.i); break;
		case TK_RSHIFT: a.i = NeoShiftRight(a.i, b.i); break;
		case TK_AND:    a.i &= b.i;  break;
		default:        a.i |= b.i;  break;
		}
		return true;
	default:
		SetParserCompileError(ar, PCE_CONST_INVALID_OP, GetTokenString(op).c_str());
		return false;
	}
}

static bool ParseConstExpr(SConstValue& out, int minPrec, CArchiveRdWC& ar, SFunctions& funs)
{
	if (false == ParseConstPrimary(out, ar, funs))
		return false;

	while (true)
	{
		std::string tk;
		TK_TYPE tkType = GetToken(ar, tk);
		int prec = ConstBinOpPrec(tkType);
		if (prec == 0 || prec < minPrec)
		{
			ar.PushToken(tkType, tk); // 종결자(';', ')')는 호출자가 소비
			return true;
		}
		SConstValue rhs;
		if (false == ParseConstExpr(rhs, prec + 1, ar, funs))
			return false;
		if (false == EvalConstBinOp(out, tkType, rhs, ar))
			return false;
	}
}

bool ParseConstDef(CArchiveRdWC& ar, SFunctions& funs, SVars& vars, bool exported /* public const: 다른 모듈에 보임 */)
{
	std::string name, tk;
	TK_TYPE tkType;

	tkType = GetRawToken(ar, name); // Keep the declaration name for duplicate checks.

	if (tkType != TK_STRING || false == AbleBackendName(ar, name))
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "const name", name.c_str());
		return false;
	}
	if (ar.FindScriptDefine(name) != nullptr ||
		(ar.m_pDefines != nullptr && ar.m_pDefines->values.find(name) != ar.m_pDefines->values.end()) ||
		vars.FindVar(name) != -1 ||
		funs.FindFun(name) != NULL || funs._curModule->_defModules.count(name) != 0)
	{
		SetParserCompileError(ar, PCE_CONST_DUPLICATE, name.c_str());
		return false;
	}

	tkType = GetToken(ar, tk);
	if (tkType != TK_EQUAL)
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'=' after const name", tk.c_str());
		return false;
	}

	SConstValue v;
	if (false == ParseConstExpr(v, 0, ar, funs))
		return false;

	tkType = GetToken(ar, tk);
	if (tkType != TK_SEMICOLON)
	{
		SetParserCompileError(ar, PCE_INVALID_STATEMENT_END);
		return false;
	}

	NeoCompileDefineToken d;
	switch (v.type)
	{
	case NEO_DEFINE_TOKEN_INT:
		d.type = NEO_DEFINE_TOKEN_INT;
		d.text = std::to_string(v.i);
		break;
	case NEO_DEFINE_TOKEN_FLOAT:
	{
		if (!std::isfinite(v.f))
		{
			SetCompileError(ar, "non-finite const result cannot be represented");
			return false;
		}
		char buf[64];
		snprintf(buf, sizeof(buf), "%.17g", v.f);
		d.type = NEO_DEFINE_TOKEN_FLOAT;
		d.text = buf;
		// "2" 처럼 정수 형태로 떨어지면 int 로 재해석되지 않게 소수점을 보존
		if (d.text.find('.') == std::string::npos &&
			d.text.find('e') == std::string::npos && d.text.find('E') == std::string::npos)
			d.text += ".0";
		break;
	}
	case NEO_DEFINE_TOKEN_STRING:
		d.type = NEO_DEFINE_TOKEN_STRING;
		d.text = v.s;
		break;
	case NEO_DEFINE_TOKEN_TRUE:  d.type = NEO_DEFINE_TOKEN_TRUE;  d.text = "true";  break;
	case NEO_DEFINE_TOKEN_FALSE: d.type = NEO_DEFINE_TOKEN_FALSE; d.text = "false"; break;
	default:                     d.type = NEO_DEFINE_TOKEN_NULL;  d.text = "null";  break;
	}
	ar.m_sScriptDefines.values[name] = d;
	if (exported) funs._curModule->_exportedConsts.values[name] = d;
	return true;
}
bool ParseSleep(CArchiveRdWC& ar, SFunctions& funs, SVars& vars)
{
	std::string tk1;
	TK_TYPE tkType1;
	TK_TYPE r;

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_SMALL)
	{
		SetParserCompileError(ar, PCE_EXPECTED_LEFT_PAREN);
		return false;
	}
	SOperand operand;
	r = ParseShortCircuitLogic(true, operand, ar, funs, vars);
	if (TK_R_SMALL != r)
	{
		SetParserCompileError(ar, PCE_EXPECTED_RIGHT_PAREN);
		return false;
	}

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_SEMICOLON)
	{
		SetParserCompileError(ar, PCE_INVALID_STATEMENT_END);
		return false;
	}

	if (operand.IsShort())
		funs._cur->Push_OP2(ar, NOP_SLEEP, 0, operand._iVar, true);
	else
	{
		// sleep(delays[0]) : 읽은 값을 넘겨야 한다. 컨테이너를 그대로 넘기면 런타임에
		// invalid sleep value 로 죽는다.
		MaterializeValue(operand, ar, funs);
		funs._cur->Push_OP2(ar, NOP_SLEEP, 0, operand._iVar, false);
	}
	return true;
}



// switch (expr) { case <const>[, <const>]* : ... default: ... }
// - case 값은 컴파일 타임 상수(bool/int/string), strict type 비교. float 는 허용하지 않는다.
// - fallthrough 없음: 각 case 본문 끝에서 switch 끝으로 점프
// - break 는 switch 만 탈출, continue 는 바깥 loop 로 그대로 전달
bool ParseSwitch(CArchiveRdWC& ar, SFunctions& funs, SVars& vars, bool* alwaysReturns, std::vector<SJumpValue>* pContinueJumps)
{
	std::string tk1;
	TK_TYPE tkType1;

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_SMALL)
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'(' after 'switch'", tk1.c_str());
		return false;
	}

	SOperand cond;
	if (TK_R_SMALL != ParseShortCircuitLogic(true, cond, ar, funs, vars))
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "')' after switch condition", tk1.c_str());
		return false;
	}
	if (cond.IsInvalidValue())
	{
		SetParserCompileError(ar, PCE_EXPECTED_EXPRESSION);
		return false;
	}

	// Resolve selectors and function references once before dispatch.
	MaterializeValue(cond, ar, funs);
	const int iKeyVar = cond._iVar;

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_MIDDLE)
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'{' after switch(...)", tk1.c_str());
		return false;
	}

	// n1 은 short 슬롯이지만 런타임이 부호 없이(u16) 해석하므로 0~65535 까지 쓸 수 있다.
	if ((int)funs._switchTables.size() > USHRT_MAX)
	{
		SetParserCompileError(ar, PCE_SWITCH_TOO_MANY);
		return false;
	}
	const int iTableIndex = (int)funs._switchTables.size();
	funs._switchTables.push_back(SSwitchTableCompile());

	funs._cur->Push_Switch(ar, (u16)iTableIndex, iKeyVar);
	const int iBaseOffset = funs._cur->_code->GetBufferOffset(); // NOP_SWITCH 다음 op 기준

	std::vector<SJumpValue> sBreakJumps;   // break + 각 case 본문 끝 → switch 끝
	bool bHasDefault = false;
	int  iDefaultOffset = 0;

	while (true)
	{
		tkType1 = GetToken(ar, tk1);
		if (tkType1 == TK_R_MIDDLE)
			break;

		if (tkType1 == TK_CASE)
		{
			std::vector<SSwitchCaseCompile> keys;
			while (true)   // <const> [, <const>]* ':'
			{
				SConstValue cv;
				if (false == ParseConstExpr(cv, 0, ar, funs))
					return false;

				SSwitchCaseCompile kc;
				switch (cv.type)
				{
				case NEO_DEFINE_TOKEN_INT:    kc._type = VAR_INT; kc._int = cv.i; break;
				case NEO_DEFINE_TOKEN_FLOAT:
					// float 는 정확 비교에 의존해야 해서 case key 로 허용하지 않는다.
					SetParserCompileError(ar, PCE_SWITCH_FLOAT_CASE);
					return false;
				case NEO_DEFINE_TOKEN_STRING: kc._type = VAR_STRING; kc._str = cv.s; break;
				default:
					SetParserCompileError(ar, PCE_SWITCH_INVALID_CASE_VALUE);
					return false;
				}
				keys.push_back(kc);

				tkType1 = GetToken(ar, tk1);
				if (tkType1 == TK_COMMA)
					continue;
				if (tkType1 == TK_COLON)
					break;
				SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "',' or ':' after case value", tk1.c_str());
				return false;
			}

			const int iRel = (funs._cur->_code->GetBufferOffset() - iBaseOffset) / (int)sizeof(SVMOperation);
			SSwitchTableCompile& tbl = funs._switchTables[iTableIndex];
			for (size_t i = 0; i < keys.size(); ++i)
			{
				for (size_t j = 0; j < tbl._cases.size(); ++j)   // 중복 검사 (strict type)
				{
					const SSwitchCaseCompile& e = tbl._cases[j];
					if (e._type != keys[i]._type)
						continue;
					bool same = false;
					switch (e._type)
					{
					case VAR_INT:    same = (e._int == keys[i]._int); break;
					case VAR_STRING: same = (e._str == keys[i]._str); break;
					default: break;
					}
					if (same)
					{
						SetParserCompileError(ar, PCE_SWITCH_DUPLICATE_CASE);
						return false;
					}
				}
				keys[i]._jumpOffset = iRel;
				tbl._cases.push_back(keys[i]);
			}
		}
		else if (tkType1 == TK_DEFAULT)
		{
			tkType1 = GetToken(ar, tk1);
			if (tkType1 != TK_COLON)
			{
				SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "':' after 'default'", tk1.c_str());
				return false;
			}
			if (bHasDefault)
			{
				SetParserCompileError(ar, PCE_SWITCH_DUPLICATE_DEFAULT);
				return false;
			}
			bHasDefault = true;
			iDefaultOffset = (funs._cur->_code->GetBufferOffset() - iBaseOffset) / (int)sizeof(SVMOperation);
		}
		else
		{
			SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'case' or 'default'", tk1.c_str());
			return false;
		}

		// 본문: 다음 case/default/'}' 까지. break 는 sBreakJumps, continue 는 바깥 loop 로.
		AddLocalVar(vars.GetCurrentLayer());
		if (false == ParseMiddleArea(&sBreakJumps, ar, funs, vars, NULL, pContinueJumps, true))
			return false;
		DelLocalVar(vars.GetCurrentLayer());
		ClearTempVars(funs);

		// fallthrough 없음: 본문 끝에서 switch 끝으로
		funs._cur->Push_JMP(ar, 0);
		sBreakJumps.push_back(SJumpValue(funs._cur->_code->GetBufferOffset() - (int)(sizeof(short) * 3), funs._cur->_code->GetBufferOffset()));
		funs._cur->ClearLastOP();
	}

	const int iEndOffset = funs._cur->_code->GetBufferOffset();
	if (false == bHasDefault)   // default 없으면 switch 끝으로
		iDefaultOffset = (iEndOffset - iBaseOffset) / (int)sizeof(SVMOperation);
	funs._switchTables[iTableIndex]._defaultOffset = iDefaultOffset;

	for (size_t i = 0; i < sBreakJumps.size(); ++i)
		funs._cur->Set_JumpOffet(sBreakJumps[i], iEndOffset);

	funs._cur->ClearLastOP();
	if (alwaysReturns) *alwaysReturns = false;
	return true;
}

bool ParseMiddleArea(std::vector<SJumpValue>* pJumps, CArchiveRdWC& ar, SFunctions& funs, SVars& vars, bool* alwaysReturns, std::vector<SJumpValue>* pContinueJumps, bool bStopAtCase, bool allowEof, bool singleStatement)
{
	ParserDepthGuard depth(ar);
	// 마지막 OP 대신 문과 분기의 구조로 블록 전체의 반환을 추적한다.
	bool blockAlwaysReturns = false;
	const std::size_t initialBreakJumps = pJumps ? pJumps->size() : 0;
	const std::size_t initialContinueJumps = pContinueJumps ? pContinueJumps->size() : 0;
	if (alwaysReturns) *alwaysReturns = false;

	std::string tk1, tk2;
	TK_TYPE tkType1, tkType2;
	TK_TYPE r;

	SOperand iTempOffset;
	FUNCTION_TYPE funType = FUNT_NORMAL;   // export: 호스트에 보임
	bool blPublic = false;                 // public: 다른 모듈에 보임. 둘은 별개이며 각각 한 번만 쓴다.

	bool bGlobalLocal;
	bool blEnd = false;
	while (blEnd == false)
	{
		ClearTempVars(funs);
		bool statementAlwaysReturns = false;

		bGlobalLocal = false;
		tkType1 = GetToken(ar, tk1);
		if ((funType == FUNT_EXPORT || blPublic) && tkType1 != TK_VAR && tkType1 != TK_CONST && tkType1 != TK_FUN &&
			tkType1 != TK_EXPORT && tkType1 != TK_PUBLIC)
		{
			SetParserCompileError(ar, PCE_EXPECTED_DECLARATION, funType == FUNT_EXPORT ? "export" : "public", tk1.c_str());
			return false;
		}
		if ((tkType1 == TK_EXPORT || tkType1 == TK_PUBLIC || tkType1 == TK_IMPORT || tkType1 == TK_CONST) && !AtFileTopLevel(funs, vars))
		{
			SetParserCompileError(ar, PCE_NOT_FILE_SCOPE, tk1.c_str());
			return false;
		}
		switch (tkType1)
		{
		case TK_NONE:
			if (!allowEof)
			{
				SetParserCompileError(ar, PCE_UNEXPECTED_FUNCTION_END, funs.GetCurFunName().c_str());
				return false;
			}
			blEnd = true;
			break;
		case TK_L_MIDDLE:
			AddLocalVar(vars.GetCurrentLayer());

			if (false == ParseMiddleArea(pJumps, ar, funs, vars, &statementAlwaysReturns, pContinueJumps))
				return false;

			DelLocalVar(vars.GetCurrentLayer());
			break;
		case TK_R_MIDDLE:
			if (bStopAtCase)
			{	// switch 본문: '}' 는 ParseSwitch 가 소비한다
				ar.PushToken(tkType1, tk1);
				blEnd = true;
				break;
			}
			if (allowEof || singleStatement)
			{
				SetParserCompileError(ar, PCE_UNEXPECTED_GLOBAL_BLOCK_END);
				return false;
			}
			blEnd = true;
			break;
		case TK_CASE:
		case TK_DEFAULT:
			if (bStopAtCase)
			{	// 다음 case/default 로 넘어간다. 소비하지 않고 ParseSwitch 로 반환
				ar.PushToken(tkType1, tk1);
				blEnd = true;
				break;
			}
			SetParserCompileError(ar, PCE_CASE_OUTSIDE_SWITCH);
			return false;
		case TK_RETURN:
			ar.PushToken(tkType1, tk1);

			iTempOffset = INVALID_ERROR_PARSEJOB;
			r = ParseJob(false, iTempOffset, NULL, ar, funs, vars);
			if (TK_SEMICOLON != r)
			{
				SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "';' after 'return'", tk1.c_str());
				return false;
			}
			statementAlwaysReturns = true;
			break;
		case TK_VAR:
			if (false == ParseVarDef(ar, funs, vars, funType == FUNT_EXPORT, blPublic))
				return false;
			break;
		case TK_CONST:
			if (funType == FUNT_EXPORT)
			{
				// 상수는 실행 중 슬롯이 없어 호스트가 읽을 것이 없다. 모듈 공개는 public const.
				SetParserCompileError(ar, PCE_EXPORT_CONST);
				return false;
			}
			if (false == ParseConstDef(ar, funs, vars, blPublic))
				return false;
			break;
		case TK_BREAK:
			if (pJumps == NULL)
			{
				SetParserCompileError(ar, PCE_BREAK_OUTSIDE_LOOP);
				return TK_NONE;
			}
			funs._cur->Push_JMP(ar, 0);
			pJumps->push_back(SJumpValue(funs._cur->_code->GetBufferOffset() - (sizeof(short) * 3), funs._cur->_code->GetBufferOffset()));
			tkType2 = GetToken(ar, tk2);
			if (tkType2 != TK_SEMICOLON)
			{
				SetParserCompileError(ar, PCE_EXPECTED_BREAK_SEMICOLON);
				return false;
			}
			break;
		case TK_CONTINUE:
			if (pContinueJumps == NULL)
			{
				SetParserCompileError(ar, PCE_CONTINUE_OUTSIDE_LOOP);
				return TK_NONE;
			}
			funs._cur->Push_JMP(ar, 0);
			pContinueJumps->push_back(SJumpValue(funs._cur->_code->GetBufferOffset() - (sizeof(short) * 3), funs._cur->_code->GetBufferOffset()));
			tkType2 = GetToken(ar, tk2);
			if (tkType2 != TK_SEMICOLON)
			{
				SetParserCompileError(ar, PCE_EXPECTED_CONTINUE_SEMICOLON);
				return false;
			}
			break;
		case TK_IMPORT:
			if (ParseImport(ar, funs, vars) == false)
				return false;
			break;
		case TK_EXPORT:
			if (funType == FUNT_EXPORT)
			{
				SetParserCompileError(ar, PCE_DUPLICATE_VISIBILITY, tk1.c_str());
				return false;
			}
			funType = FUNT_EXPORT;
			break;
		case TK_PUBLIC:
			if (blPublic)
			{
				SetParserCompileError(ar, PCE_DUPLICATE_VISIBILITY, tk1.c_str());
				return false;
			}
			blPublic = true;
			break;
		case TK_FUN:
			tkType2 = GetToken(ar, tk2);
			if (false == AbleBackendName(ar, tk2))
			{
				SetParserCompileError(ar, PCE_INVALID_FUNCTION_NAME, tk2.c_str());
				return false;
			}
			if (tkType2 == TK_STRING)
			{
				if (-1 == ParseFunctionBase(ar, funs, vars, tk2, funType))
					return false;
				if (blPublic)
				{
					SFunctionInfo* pPublic = funs.FindFun(tk2);
					if (pPublic != NULL) pPublic->_blPublic = true;
				}
			}
			else
			{
				SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "function name", tk2.c_str());
				return false;
			}
			break;
		case TK_SLEEP:
			if (false == ParseSleep(ar, funs, vars))
				return false;
			break;
		case TK_STRING:
        case TK_TOSTRING:
        case TK_TOINT:
        case TK_TOFLOAT:
        case TK_TRUE:
        case TK_FALSE:
        case TK_NULL:
		case TK_PLUS2:
		case TK_MINUS2:
        case TK_PLUS:
        case TK_MINUS:
        case TK_NOT:
		case TK_L_SMALL:
        case TK_L_ARRAY:
		case TK_QUOTE2:
		case TK_QUOTE1:
        case TK_LOGIC_NOT:
		case TK_YIELD:
		case TK_STRING_LITERAL:
			ar.PushToken(tkType1, tk1);
			iTempOffset = INVALID_ERROR_PARSEJOB;
			r = ParseJob(false, iTempOffset, NULL, ar, funs, vars);
			if (TK_NONE == r)
			{
				//SetCompileError(ar, "Error (%d, %d): ", ar.CurLine(), ar.CurCol());
				return false;
			}
			break;
		case TK_IF:
			bGlobalLocal = funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME;
			if(bGlobalLocal) funs._cur->_name = GLOBAL_INIT_FUN_NAME "IF";
			if (false == ParseIF(pJumps, ar, funs, vars, &statementAlwaysReturns, pContinueJumps))
				return false;
			if (bGlobalLocal) funs._cur->_name = GLOBAL_INIT_FUN_NAME;
			break;
		case TK_FOR:
			bGlobalLocal = funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME;
			if (bGlobalLocal) funs._cur->_name = GLOBAL_INIT_FUN_NAME "FOR";
			if (false == ParseFor(ar, funs, vars))
				return false;
			if (bGlobalLocal) funs._cur->_name = GLOBAL_INIT_FUN_NAME;
			break;
		case TK_FOREACH:
			bGlobalLocal = funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME;
			if (bGlobalLocal) funs._cur->_name = GLOBAL_INIT_FUN_NAME "FOREACH";
			if (false == ParseForEach(ar, funs, vars))
				return false;
			if (bGlobalLocal) funs._cur->_name = GLOBAL_INIT_FUN_NAME;
			break;
		case TK_WHILE:
			bGlobalLocal = funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME;
			if (bGlobalLocal) funs._cur->_name = GLOBAL_INIT_FUN_NAME "WHILE";
			if (false == ParseWhile(ar, funs, vars))
				return false;
			if (bGlobalLocal) funs._cur->_name = GLOBAL_INIT_FUN_NAME;
			break;
		case TK_SWITCH:
			bGlobalLocal = funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME;
			if (bGlobalLocal) funs._cur->_name = GLOBAL_INIT_FUN_NAME "SWITCH";
			if (false == ParseSwitch(ar, funs, vars, &statementAlwaysReturns, pContinueJumps))
				return false;
			if (bGlobalLocal) funs._cur->_name = GLOBAL_INIT_FUN_NAME;
			break;
		case TK_SEMICOLON:
			// 빈 문장. 정적 프런트엔드가 switch 절이나 if 본문의 "동작 없음"으로 받아들이는 ';' 를 코드 없이 통과시킨다.
			break;
		default:
			SetParserCompileError(ar, PCE_UNEXPECTED_TOKEN, tk1.c_str(), funs.GetCurFunName().c_str());
			return false;
			break;
		}

		// 모든 경로가 반환한 뒤의 도달 불가능한 문장은 반환 판정을 취소하지 않는다.
		blockAlwaysReturns = blockAlwaysReturns || statementAlwaysReturns;

		if (tkType1 != TK_EXPORT && tkType1 != TK_PUBLIC)
		{
			funType = FUNT_NORMAL;
			blPublic = false;
			if (singleStatement) break;
		}
	}
	// 바깥 루프의 break/continue 는 뒤의 return 을 건너뛸 수 있으므로 반환을 단정하지 않는다.
	// 안쪽 루프와 switch 의 break 는 각자의 목록에 들어가므로 이 판정에 영향을 주지 않는다.
	if (alwaysReturns)
		*alwaysReturns = blockAlwaysReturns &&
			(!pJumps || pJumps->size() == initialBreakJumps) &&
			(!pContinueJumps || pContinueJumps->size() == initialContinueJumps);
	return true;
}

static bool ParseSingleStatement(std::vector<SJumpValue>* breaks, CArchiveRdWC& ar,
    SFunctions& funs, SVars& vars, bool* returns, std::vector<SJumpValue>* continues)
{
    AddLocalVar(vars.GetCurrentLayer());
    const bool ok = ParseMiddleArea(breaks, ar, funs, vars, returns, continues, false, false, true);
    DelLocalVar(vars.GetCurrentLayer());
    return ok;
}

// ---------------------------------------------------------------------------
// 루프 상수 hoisting
//
// 루프 안에서 쓰이는 숫자 static 상수(리터럴/const)를 함수 프롤로그에서 hidden 지역변수로
// 한 번 로드해두고, 함수 내 해당 상수 참조를 전부 그 슬롯 참조로 바꾼다.
// 목적은 로드 시 PatchLocalOps 가 그 op 들을 _L(전 오퍼랜드 로컬)로 승격시킬 수 있게
// 만드는 것 — 새 opcode 를 늘리지 않고 승격률을 올리는 방식이다.
// (상수는 static 풀 = 전역 배열에 있어서 'x * 1.5' 는 절대 전 오퍼랜드 로컬이 될 수 없다)
//
//  - 대상 : static 숫자/bool. script-global 은 루프 안에서 값이 바뀔 수 있으므로 제외.
//           문자열은 로드 시 READ_STATIC_STRING opcode가 static 슬롯을 직접 사용하므로
//           hidden 지역변수로 올리지 않는다.
//  - 게이팅 : 루프 구간 안에서 1회 이상 쓰인 상수만. 루프가 없는 함수(fib 류)가 호출당
//           MOV 만 무는 순손실을 막는다.
//  - 배치 : 함수 프롤로그. 모든 제어흐름을 지배하므로 다중/조건부 루프군에서도 안전하다.
//           특정 루프 앞에 두면 그 루프를 건너뛴 경로에서 미초기화 슬롯을 읽게 된다
//           (Call 은 새 프레임 슬롯을 클리어하지 않는다).
//  - 슬롯 : AllocLocalVar 와 같은 append 경로. _localVarCount 는 감소하지 않으므로 기존
//           지역변수와 절대 겹치지 않고, temp 는 export 의 localCount 보정으로 함께 밀린다.
// ---------------------------------------------------------------------------
#define NEOS_HOIST_MAX	32	// 함수당 상한. 프레임이 커지면 재귀 가능 깊이가 줄어든다.

// n1 에 op 단위 상대 점프 offset 을 담는 op. 뒤로 가는 offset = 루프의 back edge.
static bool IsRelativeJumpOp(OperationCode op)
{
	const TypedOpInfo* typed = GetTypedOpInfo(op);
	if (typed != nullptr) return typed->jump;
	switch (op)
	{
	case NOP_JMP:			case NOP_JMP_FALSE:		case NOP_JMP_TRUE:
	case OP_JMP_EQUAL2:		case NOP_JMP_FOR:
	case NOP_JMP_FOREACH:		case NOP_JMP_RANGE_INSIDE:
	case NOP_JMP_RANGE_OUTSIDE:	case OP_JMP_LESS:
	case OP_JMP_LESS_EQ:		case OP_JMP_NEQUAL:
	case OP_JMP_GREAT:	case OP_JMP_GREAT_EQ:
		return true;
	default:
		return false;
	}
	// NOP_SWITCH 는 offset 이 n1 이 아니라 테이블에 있으므로 제외.
}

// 값을 읽는 오퍼랜드 중 (a) static 상수가 올 수 있고 (b) _L 변형이 있어 승격 이득이 나는
// 슬롯만 표시한다. 즉값(n23) / 점프 offset / 인자 수 / 함수 인덱스는 절대 포함하지 않는다 —
// 그런 필드를 슬롯 인덱스로 재작성하면 코드가 조용히 깨진다.
// bit1 = n2, bit2 = n3.
// 로드 시 즉값 형식이 될 오른쪽 피연산자인가(NEOCORE_RUNTIME_I64_IMM_ARITH, 정수 비교 점프).
// 이런 상수는 명령 안으로 들어가므로 hidden 지역으로 올리면 오히려 슬롯 읽기가 생긴다.
static bool ImmediateRightOperand(const SVMOperation& v, const SFunctions& funs)
{
	const int index = v.n3 - COMPILE_STATIC_VAR_BEGIN;
	if (index < 0 || index >= (int)funs._staticVars.size() || funs._staticVars[index].GetType() != VAR_INT)
		return false;
	const OperationCode meaning = ArithmeticMeaning(v.Code());
	const eNOperation runtime = RuntimeOpcode(meaning, IsArithmeticUpdate(v));
	const eNOperation immediate = runtime == NOP_NONE ? NOP_NONE : ImmediateRuntimeOpcode(runtime);
	return immediate != NOP_NONE && FitsRuntimeImmediate(immediate, funs._staticVars[index]._int);
}

static int HoistableOperandMask(const SVMOperation& v, const SFunctions& funs)
{
	const OperationCode op = v.Code();
	// 타입 있는 숫자 명령(산술·비교·비교 점프·변환)은 메타데이터의 읽기 피연산자가 곧 슬롯이다.
	// 비교 점프의 n1은 점프 거리라 slots에 없다. 제자리 산술은 n3를 쓰지 않으므로 뺀다.
	// 모두 지역이 된 명령은 실행 시 프레임 기준 하나로 피연산자를 읽는다.
	if (const TypedOpInfo* typed = GetTypedOpInfo(op))
	{
		int mask = (typed->slots & ~typed->writes) & 0x6;
		if (!typed->comparison && (v.argFlag & NEOS_OP_ARITH_UPDATE)) mask &= ~0x4;
		if ((mask & 0x4) && ImmediateRightOperand(v, funs)) mask &= ~0x4;
		return mask;
	}
	switch (op)
	{
	case NOP_MOV:			// n1 = dest
		return 0x2;
	case OP_JMP_EQUAL2:		case NOP_STR_ADD:
	case OP_JMP_LESS:		case OP_JMP_LESS_EQ:
	case OP_JMP_NEQUAL:	case OP_JMP_GREAT:
	case OP_JMP_GREAT_EQ:
		return 0x2 | 0x4;
	case NOP_TOINT:			// n1 = dest
		return 0x2;
	case NOP_CLT_READ:		// n1 = 컨테이너, n2 = key, n3 = dest
		return 0x2;
	case NOP_CLT_MOV:		// n1 = 컨테이너, n2 = key, n3 = value
		return 0x2 | 0x4;
	default:
		return 0;
	}
}

static bool IsStaticOperand(short v)
{
	return (v >= COMPILE_STATIC_VAR_BEGIN && v < COMPILE_CALLARG_VAR_BEGIN);
}

// 반환: 프롤로그에 넣을 (hidden 슬롯, static 오퍼랜드) 쌍. 코드(pCode)는 제자리에서 재작성된다.
static void HoistLoopConstants(SFunctions& funs, u8* pCode, int codeSize,
	std::vector<std::pair<short, short> >& outPrologue)
{
	outPrologue.clear();
	const int n = codeSize / (int)sizeof(SVMOperation);
	if (n <= 0)
		return;
	SVMOperation* ops = (SVMOperation*)pCode;

	// 1) back edge 로 루프 구간을 찾는다. 중첩 루프는 구간이 포함관계라 자연히 처리된다.
	std::vector<u8> inLoop(n, 0);
	bool anyLoop = false;
	for (int i = 0; i < n; i++)
	{
		if (false == IsRelativeJumpOp(ops[i].Code()))
			continue;
		const int off = ops[i].n1;
		if (off >= 0)
			continue;						// 전방 점프(if/break 등)는 루프가 아니다
		int target = i + 1 + off;			// offset 은 다음 op 기준 op 단위 상대
		if (target < 0) target = 0;
		for (int k = target; k <= i && k < n; k++)
			inLoop[k] = 1;
		anyLoop = true;
	}
	if (false == anyLoop)
		return;								// 루프 없는 함수는 대상 아님

	// 2) 루프 안에서 쓰인 static 상수를 센다.
	std::map<short, int> useInLoop;
	for (int i = 0; i < n; i++)
	{
		if (inLoop[i] == 0)
			continue;
		const int mask = HoistableOperandMask(ops[i], funs);
		if ((mask & 0x2) && IsStaticOperand(ops[i].n2)) useInLoop[ops[i].n2]++;
		if ((mask & 0x4) && IsStaticOperand(ops[i].n3)) useInLoop[ops[i].n3]++;

		// range descriptor의 lower/upper는 opcode n3가 아니라 별도 테이블에 있다.
		// 여기서 함께 hidden local로 올려야 load-time PatchLocalOps가 range _L을 고를 수 있다.
		if (ops[i].op == NOP_JMP_RANGE_INSIDE || ops[i].op == NOP_JMP_RANGE_OUTSIDE)
		{
			const u16 rangeIndex = (u16)ops[i].n3;
			if ((size_t)rangeIndex >= funs._rangeJumps.size())
				continue;
			const SRangeJumpCompile& range = funs._rangeJumps[rangeIndex];
			if (range._ownerFunction != funs._cur->_funID)
				continue;
			if (IsStaticOperand(range._lower)) useInLoop[range._lower]++;
			if (IsStaticOperand(range._upper)) useInLoop[range._upper]++;
		}
	}
	if (useInLoop.empty())
		return;

	// 3) 숫자/bool만 남기고 사용 횟수 순으로 상위 NEOS_HOIST_MAX 개 선택.
	std::vector<std::pair<int, short> > cand;	// (사용횟수, static 오퍼랜드)
	for (std::map<short, int>::iterator it = useInLoop.begin(); it != useInLoop.end(); ++it)
	{
		const int idx = it->first - COMPILE_STATIC_VAR_BEGIN;
		if (idx < 0 || idx >= (int)funs._staticVars.size())
			continue;
		const VAR_TYPE t = funs._staticVars[idx].GetType();
		if (t != VAR_INT && t != VAR_FLOAT && t != VAR_BOOL)
			continue;
		cand.push_back(std::make_pair(it->second, it->first));
	}
	if (cand.empty())
		return;
	std::sort(cand.begin(), cand.end(), std::greater<std::pair<int, short> >());
	if ((int)cand.size() > NEOS_HOIST_MAX)
		cand.resize(NEOS_HOIST_MAX);

	// 4) hidden 지역변수 슬롯 할당 + 함수 전체 참조 재작성.
	//    루프 밖 사용까지 함께 바꾼다 — 프롤로그가 모든 사용을 지배하므로 안전하고,
	//    MOV 비용은 이미 지불했으므로 추가 이득이다.
	std::map<short, short> remap;
	for (int c = 0; c < (int)cand.size(); c++)
	{
		const short staticVar = cand[c].second;
		if (1 + (int)funs._cur->_args.size() + funs._cur->_localVarCount >= COMPILE_LOCALTMP_VAR_BEGIN) break;
		const short slot = (short)funs._cur->AllocLocalVar();
		remap[staticVar] = slot;
		outPrologue.push_back(std::make_pair(slot, staticVar));
	}

	for (int i = 0; i < n; i++)
	{
		const int mask = HoistableOperandMask(ops[i], funs);
		if (mask & 0x2)
		{
			std::map<short, short>::iterator it = remap.find(ops[i].n2);
			if (it != remap.end()) ops[i].n2 = it->second;
		}
		if (mask & 0x4)
		{
			std::map<short, short>::iterator it = remap.find(ops[i].n3);
			if (it != remap.end()) ops[i].n3 = it->second;
		}

		if (ops[i].op == NOP_JMP_RANGE_INSIDE || ops[i].op == NOP_JMP_RANGE_OUTSIDE)
		{
			const u16 rangeIndex = (u16)ops[i].n3;
			if ((size_t)rangeIndex >= funs._rangeJumps.size())
				continue;
			SRangeJumpCompile& range = funs._rangeJumps[rangeIndex];
			if (range._ownerFunction != funs._cur->_funID)
				continue;
			std::map<short, short>::iterator lower = remap.find(range._lower);
			if (lower != remap.end()) range._lower = lower->second;
			std::map<short, short>::iterator upper = remap.find(range._upper);
			if (upper != remap.end()) range._upper = upper->second;
		}
	}
}

void FinalizeFuction(SFunctions& funs)
{
	int iCode_Begin = funs._cur->_iCode_Begin;
	funs._cur->_iCode_Begin = funs._codeFinal.GetBufferOffset();

	funs._codeTemp.SetBufferOffset(iCode_Begin);
	u8* pSrc = (u8*)funs._codeTemp.GetDataCurrent();

	// 루프 상수를 hidden 지역변수로 올리고 본문 참조를 재작성한다(제자리 수정).
	// 점프가 op 단위 상대라 앞에 프롤로그를 붙여도 함수 내부 점프는 보정이 필요 없다.
	std::vector<std::pair<short, short> > prologue;
	HoistLoopConstants(funs, pSrc, funs._cur->_iCode_Size, prologue);
	// 재작성된 키 오퍼랜드는 더 이상 static 상수가 아니다. 저장 종류 추론이 되짚도록 쌍을 남긴다.
	funs._cur->_hoisted = prologue;

	for (int i = 0; i < (int)prologue.size(); i++)
	{
		SVMOperation mv;
		memset(&mv, 0, sizeof(mv));
		mv.op = NOP_MOV;
		mv.argFlag = 0;					// 플래그는 export 의 ChangeIndex 가 채운다
		mv.n1 = prologue[i].first;		// hidden 지역변수 슬롯
		mv.n2 = prologue[i].second;		// static 상수 오퍼랜드
		mv.n3 = 0;
		funs._codeFinal.Write(&mv, sizeof(mv));
	}

	funs._codeFinal.Write(pSrc, funs._cur->_iCode_Size);

	if (funs._cur->_pDebugData)
	{
		int iSrcBeginDebugOff = iCode_Begin / 8;
		int base = (int)funs.m_sDebugFinal.size();
		int iPrologue = (int)prologue.size();
		funs.m_sDebugFinal.resize(funs._codeFinal.GetBufferOffset() / 8);
		// 프롤로그 op 에는 함수 본문 첫 줄의 디버그 정보를 그대로 붙인다.
		for (int i = 0; i < iPrologue; i++)
			funs.m_sDebugFinal[base + i] = (*funs._cur->_pDebugData)[iSrcBeginDebugOff];
		for (int i = base + iPrologue; i < (int)funs.m_sDebugFinal.size(); i++)
			funs.m_sDebugFinal[i] = (*funs._cur->_pDebugData)[i + iSrcBeginDebugOff - base - iPrologue];

		funs._cur->_pDebugData->resize(iSrcBeginDebugOff);
	}
	funs._cur->_iCode_Size += (int)prologue.size() * (int)sizeof(SVMOperation);
	// 호출 계획의 활성 위치도 프롤로그만큼 뒤로 민다.
	for (CompileCallStorage& call : funs._cur->_calls)
		call.prepareOffset += (int)prologue.size() * (int)sizeof(SVMOperation);
	// 모듈 경계를 지키려고 둔 `int Get() { return value; }`는 호출 비용이 필요 없다. 몸통이 전역 RETURN 하나이고
	// 인자·캡처가 없으면 기억해 두고, 뒤에 오는 호출 자리에서 전역 복사로 접는다. 함수값 호출과 부작용은 제외다.
	{
		SFunctionInfo* cur = funs._cur;
		cur->_globalGetterSlot = 0;
		if ((cur->_funType == FUNT_NORMAL || cur->_funType == FUNT_EXPORT) && cur->_args.empty() && cur->_captures.empty() &&
			cur->_iCode_Size == (int)sizeof(SVMOperation))
		{
			SVMOperation op;
			memcpy(&op, (u8*)funs._codeFinal.GetData() + cur->_iCode_Begin, sizeof(op));
			if (op.Code() == NOP_RETURN && op.argFlag == 0 && op.n1 <= COMPILE_GLOBAL_VAR_BEGIN)
				cur->_globalGetterSlot = op.n1;
		}
	}

	funs._funSequence.push_back(funs.FindFun(funs.GetCurFunName()));
}

bool ParseFunctionBody(CArchiveRdWC& ar, SFunctions& funs, SVars& vars, bool addOPFunEnd)
{
	bool bodyAlwaysReturns = false;
	try
	{
		if (false == ParseMiddleArea(NULL, ar, funs, vars, &bodyAlwaysReturns, NULL, false, funs.GetCurFunName() == GLOBAL_INIT_FUN_NAME))
			return false;
	}
	catch (const CompileLimitError& e)
	{
		// 임포트 안의 한도 초과도 해당 소스의 위치에서 컴파일 오류로 남긴다.
		SetCompileError(ar, "Error (%d, %d): %s", ar.CurLine(), ar.CurCol(), e.what());
		return false;
	}

	if (ar.m_sErrorString.empty() == false)
		return false;

	// 블록의 반환 판정은 문장 구조를 따르므로 `return; print(1);`처럼 도달하지 못하는 문장이 뒤에 있어도
	// "항상 반환"으로 본다. 그 문장들은 그대로 명령이 되어 함수의 마지막 명령이 RET가 아니게 되고, 로더는
	// 함수 끝이 종결 명령이기를 요구한다. 그래서 실제 마지막 명령을 보고 RET를 붙인다.
	const int bodySize = funs._cur->_code->GetBufferOffset() - funs._cur->_iCode_Begin;
	const bool endsWithReturn = bodySize >= (int)sizeof(SVMOperation) &&
		PrimaryOperation(funs._cur->GetOP(funs._cur->_code->GetBufferOffset() - (int)sizeof(SVMOperation))) == NOP_RETURN;
	if (addOPFunEnd && (bodyAlwaysReturns == false || endsWithReturn == false))
		funs._cur->Push_RETURN(ar, 0, true);

	return true;
}
bool ParseFunction(CArchiveRdWC& ar, SFunctions& funs, SVars& vars, std::string& fname, FUNCTION_TYPE funType)
{
    if (funType == FUNT_EXPORT && !funs._exportNames.insert(fname).second)
    {
        SetCompileError(ar, "duplicate exported name '%s'", fname.c_str());
        return false;
    }

	//funs._cur->Clear();
	//funs._cur->_name = fname;
	SFunctionInfo* pF = funs.FindFun(fname);
	if(pF == nullptr)
	{
		int funID = funs.GetFunCountAll();
		CheckCompileRange("function id", funID, 0, SHRT_MAX);

		if (funType == FUNT_ANONYMOUS)
		{
			char ch[128];
			snprintf(ch, _countof(ch), "#@_%d", funID);
			fname = ch;
		}

		pF = funs.NewFun(fname, funs._cur->_code, funs._cur->_pDebugData);
		pF->_funType = funType;
		pF->_moduleName = ar.m_sModuleName;

		pF->_funID = funID;
		funs._funIDs[pF->_funID] = pF;
	}
	else
	{
		if (pF->_bodyParsed)
		{
			SetParserCompileError(ar, PCE_DUPLICATE_FUNCTION, fname.c_str());
			return false;
		}
		pF->_args.clear();
	}
    if (funType == FUNT_ANONYMOUS && ar.m_coreTypedIR && funs._cur)
    {
        const NeoCore::TypeDesc& signature = ar.m_coreTypedIR->types.Get(funs._cur->_returnStorage.typeId);
        if (signature.kind == NeoCore::TypeKind::Function)
        {
            pF->_returnStorage = CoreStorageType(ar.m_coreTypedIR->types, signature.returnType);
            for (NeoCore::TypeId parameter : signature.parameters)
                pF->_parameterStorage.push_back(CoreStorageType(ar.m_coreTypedIR->types, parameter));
        }
    }
	pF->_bodyParsed = true;
	pF->_funType = funType;
	funs._cur = pF;

	std::string tk1;
	TK_TYPE tkType1;

	SLayerVar* pCurLayer = AddVarsFunction(vars);
	pCurLayer->_function = pF;
	if (false == ParseFunctionArg(ar, funs, pCurLayer))
		return false;

	tkType1 = GetToken(ar, tk1);
	if (tkType1 != TK_L_MIDDLE)
	{
		if (funs._cur->_funType == FUNT_ANONYMOUS)
		{
			SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'{' to start anonymous function body", tk1.c_str());
			return false;
		}
		if (tkType1 == TK_SEMICOLON)
		{
			SetParserCompileError(ar, PCE_FUNCTION_PROTOTYPE);
			return false;
		}
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'{' to start function body", tk1.c_str());
		return false;
	}

	funs._cur->_iCode_Begin = funs._cur->_code->GetBufferOffset();
	if (false == ParseFunctionBody(ar, funs, vars))
		return false;

	funs._cur->_iCode_Size = funs._cur->_code->GetBufferOffset() - funs._cur->_iCode_Begin;
	if (funs._cur->_iCode_Size)
	{
		FinalizeFuction(funs);
	}

//	SFunctionInfo* pF = funs.FindFun(funs.GetCurFunName());
//	*pF = funs._cur;

	DelVarsFunction(vars);

	return true;
}
int ParseFunctionBase(CArchiveRdWC& ar, SFunctions& funs, SVars& vars, std::string fname, FUNCTION_TYPE funType)
{
	ParserDepthGuard depth(ar);
	std::string tk3;
	auto tkType3 = GetToken(ar, tk3);
	if (tkType3 == TK_L_SMALL) // 함수
	{
		SFunctionInfo* save = funs._cur;
//		funs._cur->Clear();

		//funs._cur->_name = fname;
		//funs._cur->_funType = funType;
		if (false == ParseFunction(ar, funs, vars, fname, funType))
			return -1;

		int r = funs._cur->_funID;
		funs._cur = save;
		return r;
	}
	else
	{
		SetParserCompileError(ar, PCE_EXPECTED_TOKEN, "'(' after function name", tk3.c_str());
	}
	return -1;
}
bool Parse(CArchiveRdWC& ar, CNArchive&arw, bool putASM)
{
	if(g_bInitVM == false)
	{
		SetCompileError(ar, g_sParserCompileErrors[PCE_VM_NOT_INITIALIZED]);
		return false;
	}
//	ar.m_sTokenQueue.clear();

	SVars	vars;
	SFunctions funs;
	funs._typedIR = ar.m_coreTypedIR;
	if (ar.m_coreTypedIR)
	{
		using NeoCore::TypeKind;
        for (const std::string& name : ar.m_coreTypedIR->nativeGlobals)
        {
            SlotType native; native.kind = SlotKind::Native;
            funs._declaredStorage.emplace(name, native);
        }
		for (const NeoCore::IRStorageSymbol& symbol : ar.m_coreTypedIR->storageSymbols)
		{
			SlotType storage;
			storage.typeId = symbol.type;
			switch (ar.m_coreTypedIR->types.Get(symbol.type).kind)
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
			case TypeKind::BuiltinObject:
				storage.kind = ar.m_coreTypedIR->types.Get(symbol.type).name == "AsyncRequest" ? SlotKind::Async : SlotKind::Native;
				break;
			case TypeKind::Coroutine: storage.kind = SlotKind::Coroutine; break;
			default: break;
			}
			if (symbol.boxed) storage.kind = symbol.parameter ? SlotKind::Boxed : SlotKind::List;
			if (symbol.ownerFunction.empty()) funs._declaredStorage.emplace(symbol.backendName, storage);
			else funs._helperStorage[symbol.ownerFunction].emplace(symbol.backendName, storage);
		}
	}

	funs._curModule = funs.NewLayer();

	funs._cur = funs.NewFun(GLOBAL_INIT_FUN_NAME, &funs._codeTemp, ar._debug ? &funs.m_sDebugTemp : nullptr);
/*	funs._cur->_funID = 0;
	funs._cur->_name = GLOBAL_INIT_FUN_NAME;
	funs._cur->_code = &funs._codeTemp;
	if (ar._debug)
		funs._cur->_pDebugData = &funs.m_sDebugTemp;
	else
		funs._cur->_pDebugData = NULL;
	funs._cur->Clear();*/

	SLayerVar* pCurLayer = AddVarsFunction(vars);
	pCurLayer->_function = funs._cur;

//	funs.AddStaticString("system");

	// 호스트가 넘긴 네이티브 전역 심볼을 본문 파싱 전에 사전 선언한다.
	// (기존 preCompileHeader "export var X;" 텍스트 주입을 구조화 테이블로 대체)
	if (ar.m_pGlobalSymbols && ar.m_pGlobalSymbols->symbols)
	{
		for (int i = 0; i < ar.m_pGlobalSymbols->count; i++)
		{
			const NeoGlobalSymbol& sym = ar.m_pGlobalSymbols->symbols[i];
			if (sym.name == nullptr || sym.name[0] == 0)
				continue;
            // 이 테이블의 선언은 런타임에 등록한 네이티브 호출 객체다.
            if (funs.FindDeclaredStorage(sym.name).kind == SlotKind::Boxed)
            {
                SlotType native; native.kind = SlotKind::Native;
                funs._declaredStorage.emplace(sym.name, native);
            }
			if (-1 == AddLocalVarName(ar, funs, vars, sym.exported, false, sym.name, true))
				return false; // 중복/예약어 등은 AddLocalVarName 이 에러 세팅
			vars._hostGlobals.insert(sym.name);
		}
	}

	bool r = CollectFunctionDeclarations(ar, funs) && ParseFunctionBody(ar, funs, vars);
	if (true == r)
	{
		funs._cur->_iCode_Size = funs._cur->_code->GetBufferOffset();

		FinalizeFuction(funs);
//		SFunctionInfo* pF = funs.FindFun(funs.GetCurFunName());
//		*pF = funs._cur;

		if(false == Write(ar, arw, funs, vars))
			return false;
		if (putASM)
		{
			int off = arw.GetBufferOffset();
			WriteLog(ar, arw, funs, vars);
			arw.SetBufferOffset(off);
		}
	}

	DelVarsFunction(vars);

	while (vars._varsFunction.empty() == false)
		DelVarsFunction(vars);

	return r;
}

bool NeoVMSystem::Compile(CNArchive& arw, const NeoCompilerParam& param)
{
	//CNeoVM::InitLib();

	CArchiveRdWC ar2;
	ar2._allowGlobalInitLogic = param.allowGlobalInitLogic;
	ar2.m_coreIntrinsicPrefix = param.coreIntrinsicPrefix;
	ar2.m_coreTypedIR = param.coreTypedIR;
	ar2.m_sSourceName = !param.sourceName.empty() ? param.sourceName
		: (param.debugSourcePath != nullptr ? param.debugSourcePath : "<script>");
	if (param.diagnostic) *param.diagnostic = NeoCompileDiagnostic{};
	ar2._debug = param.debug;
	ar2.m_pDefines = param.defines;
	ar2.m_pGlobalSymbols = param.globalSymbols;
	ar2.m_pLoader = param.loader;                 // import 해석 loader(전역 g_NeoLoader 대체)
	if (param.debugSourceFiles != nullptr)
	{
		param.debugSourceFiles->clear();
		param.debugSourceFiles->push_back(param.debugSourcePath != nullptr ? param.debugSourcePath : "");
		ar2.m_pDebugSourceFiles = param.debugSourceFiles;
	}

	bool b = false;
	try
	{
		std::vector<unsigned char> irEffects;
		if (ar2.m_coreTypedIR)
		{
			CheckCompileRange("typed IR node count", static_cast<int64_t>(ar2.m_coreTypedIR->nodes.size()), 0, INT_MAX);
			if (!CoreIRValidator(ar2, *ar2.m_coreTypedIR, irEffects).Validate())
			{
				arw.SetBufferOffset(0);
				if (param.err) *param.err = ar2.m_sErrorString;
				if (param.diagnostic) *param.diagnostic = ar2.m_errorLocation;
				return false;
			}
			ar2.m_coreIREffects = &irEffects;
		}
		CheckCompileRange("source bytes", param.iLenSrc, 0, INT_MAX - 1);
		if (!ToArchiveRdWC((const char*)param.pBufferSrc, param.iLenSrc, ar2))
			SetCompileError(ar2, "Invalid script source encoding (expected UTF-8 or BOM-marked UTF-16)");
		else
			b = Parse(ar2, arw, param.putASM);
	}
	catch (const CompileLimitError& e)
	{ SetCompileError(ar2, "Error (%d, %d): %s", ar2.CurLine(), ar2.CurCol(), e.what()); }
	catch (const std::bad_alloc&)
	{ SetCompileError(ar2, "Error (%d, %d): compiler memory allocation failed", ar2.CurLine(), ar2.CurCol()); }
	// A failed parse must always carry a diagnostic; lexer errors must not be
	// mistaken for successful EOF, even in the declaration pre-pass.
	if (!ar2.m_sErrorString.empty()) b = false;
	if (!b && ar2.m_sErrorString.empty())
		SetParserCompileError(ar2, PCE_EXPECTED_EXPRESSION);
	if (!b) arw.SetBufferOffset(0);
	if(b == false && param.err)
		*(param.err) = ar2.m_sErrorString;
	if (!b && param.diagnostic) *param.diagnostic = ar2.m_errorLocation;

	return b;
}
bool NeoVMSystem::Initialize(INeoLoader* loader)
{
	(void)loader;   // import loader 는 컴파일마다 NeoCompilerParam.loader 로 전달(전역 아님)
	InitDefaultTokenString();
	CNeoVM::InitLib();
	g_bInitVM = true;
	return true;
}
bool	NeoVMSystem::Shutdown()
{
	g_bInitVM = false;
	return true;
}

CNeoVM* NeoVMSystem::CompileAndLoadVM(const NeoCompilerParam& param, const NeoLoadVMParam* vparam)
{
	if (vparam == nullptr || vparam->execPool == nullptr)
	{
		if (param.err != nullptr)
			*param.err = "NeoExecContextPool is required.\n";
		if (param.putASM && param.err != nullptr)
			printf("%s", (ANSI_COLOR_RED + *(param.err) + ANSI_RESET_ALL).c_str());
		return NULL;
	}

	CNArchive arCode;

	if (false == Compile(arCode, param))
	{
		if(param.putASM && param.err != nullptr)
			printf("%s", (ANSI_COLOR_RED + *(param.err) + ANSI_RESET_ALL).c_str());
		return NULL;
	}

	//if(putASM)
	//	SetCompileError(ar, "Comile Success. Code : %d bytes !!\n\n", arCode.GetBufferOffset());

	CNeoVM* pVM = NeoVMSystem::CreateVM();
	// (실행 스택은 더 이상 워커가 소유하지 않으므로 iStackSize 로 크기를 주지 않는다.)
	if (pVM->LoadVM(vparam, arCode.GetData(), arCode.GetBufferOffset()) == NULL)
	{
		NeoVMSystem::ReleaseVM(pVM);
		return NULL;
	}

	if(param.putASM)
		printf(ANSI_COLOR_GREEN "Compile Success. Code : %d bytes !!" ANSI_RESET_ALL "\n", pVM->GetBytesSize());

	return pVM;
}
CNeoVM* NeoVMSystem::CompileAndLoadRunVM(const NeoCompilerParam& param, const NeoLoadVMParam* vparam)
{
	auto pVM = CompileAndLoadVM(param, vparam);

	if(pVM == nullptr)
		return nullptr;

	std::vector<BoundaryValue> args;
	static_cast<INeoVMWorker*>(pVM->GetMainWorker())->ExecuteTop(0, args);

	return pVM;
}
};
