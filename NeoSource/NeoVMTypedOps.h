#pragma once

// 숫자 계열의 세부 연산을 한 번만 정의한다. 코드 상수와 검증·표시 메타데이터가 공유한다.
// arity는 입력 수이고 slots/writes는 n1부터 시작하는 피연산자 비트 마스크다.
#define NEOCORE_COMMON_ARITHMETIC(X, T, S) \
    X(OP_##T##_ADD, NOP_##T##_ARITH, ArithmeticOp::Add, #T "_ADD", S "add", 2, 7, 1, false, false, NOP_NONE) \
    X(OP_##T##_SUB, NOP_##T##_ARITH, ArithmeticOp::Sub, #T "_SUB", S "sub", 2, 7, 1, false, false, NOP_NONE) \
    X(OP_##T##_MUL, NOP_##T##_ARITH, ArithmeticOp::Mul, #T "_MUL", S "mul", 2, 7, 1, false, false, NOP_NONE) \
    X(OP_##T##_DIV, NOP_##T##_ARITH, ArithmeticOp::Div, #T "_DIV", S "div", 2, 7, 1, false, false, NOP_NONE) \
    X(OP_##T##_NEG, NOP_##T##_ARITH, ArithmeticOp::Neg, #T "_NEG", S "neg", 1, 3, 1, false, false, NOP_NONE)
#define NEOCORE_INTEGER_ARITHMETIC(X) \
    X(OP_I64_MOD, NOP_I64_ARITH, ArithmeticOp::Mod, "I64_MOD", "imod", 2, 7, 1, false, false, NOP_NONE) \
    X(OP_I64_AND, NOP_I64_ARITH, ArithmeticOp::BitAnd, "I64_AND", "iand", 2, 7, 1, false, false, NOP_NONE) \
    X(OP_I64_OR, NOP_I64_ARITH, ArithmeticOp::BitOr, "I64_OR", "ior", 2, 7, 1, false, false, NOP_NONE) \
    X(OP_I64_XOR, NOP_I64_ARITH, ArithmeticOp::BitXor, "I64_XOR", "ixor", 2, 7, 1, false, false, NOP_NONE) \
    X(OP_I64_SHL, NOP_I64_ARITH, ArithmeticOp::ShiftLeft, "I64_SHL", "ishl", 2, 7, 1, false, false, NOP_NONE) \
    X(OP_I64_SHR, NOP_I64_ARITH, ArithmeticOp::ShiftRight, "I64_SHR", "ishr", 2, 7, 1, false, false, NOP_NONE)
#define NEOCORE_COMPARISON_PAIR(X, T, S, REL, NAME, SUB) \
    X(OP_##T##_##REL, NOP_##T##_COMPARE, ComparisonOp::SUB, #T "_" #REL, S NAME, 2, 7, 1, true, false, OP_JMP_##T##_##REL) \
    X(OP_JMP_##T##_##REL, NOP_JMP_##T##_COMPARE, ComparisonOp::SUB, "JMP_" #T "_" #REL, "", 2, 6, 0, true, true, NOP_NONE)
#define NEOCORE_COMPARISONS(X, T, S) \
    NEOCORE_COMPARISON_PAIR(X, T, S, EQ, "eq", Equal) \
    NEOCORE_COMPARISON_PAIR(X, T, S, LT, "lt", Less) \
    NEOCORE_COMPARISON_PAIR(X, T, S, LE, "le", LessEqual) \
    NEOCORE_COMPARISON_PAIR(X, T, S, NE, "ne", NotEqual) \
    NEOCORE_COMPARISON_PAIR(X, T, S, GT, "gt", Greater) \
    NEOCORE_COMPARISON_PAIR(X, T, S, GE, "ge", GreaterEqual)
#define NEOCORE_TYPED_OPS(X) \
    NEOCORE_COMMON_ARITHMETIC(X, I64, "i") \
    NEOCORE_COMMON_ARITHMETIC(X, F64, "f") \
    NEOCORE_INTEGER_ARITHMETIC(X) \
    X(OP_I64_TO_F64, NOP_NUM_CONVERT, ConversionOp::IntToFloat, "I64_TO_F64", "itof", 1, 3, 1, false, false, NOP_NONE) \
    X(OP_F64_TO_I64, NOP_NUM_CONVERT, ConversionOp::FloatToInt, "F64_TO_I64", "ftoi", 1, 3, 1, false, false, NOP_NONE) \
    NEOCORE_COMPARISONS(X, I64, "i") \
    NEOCORE_COMPARISONS(X, F64, "f")

// 집합 명령의 물리 피연산자 모양은 숫자 명령과 분리한다. Window는 연속 지역 슬롯을 읽는다.
#define NEOCORE_VECTOR_PRIMARY_OPS(X, D, MAKE_SHAPE) \
    X(NOP_V##D##_MAKE, "V" #D "_MAKE", "v" #D "make", D, MAKE_SHAPE, D, 0) \
    X(NOP_V##D##_GET, "V" #D "_GET", "v" #D "get", 1, Unary, D, D - 1) \
    X(NOP_V##D##_SET, "V" #D "_SET", "v" #D "set", 2, Window, D, D - 1)
// 각 차원은 기본 opcode 하나를 공유하고 사칙연산은 ArithmeticOp으로 구별한다.
#define NEOCORE_VECTOR_ARITHMETIC_OPS(X, D) \
    X(OP_V##D##_ADD, NOP_V##D##_ARITH, ArithmeticOp::Add, "V" #D "_ADD", "v" #D "add", 2, Binary, D, 0) \
    X(OP_V##D##_SUB, NOP_V##D##_ARITH, ArithmeticOp::Sub, "V" #D "_SUB", "v" #D "sub", 2, Binary, D, 0) \
    X(OP_V##D##_MUL, NOP_V##D##_ARITH, ArithmeticOp::Mul, "V" #D "_MUL", "v" #D "mul", 2, Binary, D, 0) \
    X(OP_V##D##_DIV, NOP_V##D##_ARITH, ArithmeticOp::Div, "V" #D "_DIV", "v" #D "div", 2, Binary, D, 0)

#define NEOCORE_ARRAY_OPS(X, TYPE, SUFFIX, D) \
    X(NOP_ARRAY_##TYPE##_NEW, "ARRAY_" #TYPE "_NEW", "a" SUFFIX "new", 2, Binary, D, 0) \
    X(NOP_ARRAY_##TYPE##_GET, "ARRAY_" #TYPE "_GET", "a" SUFFIX "get", 2, Binary, D, 0) \
    X(NOP_ARRAY_##TYPE##_SET, "ARRAY_" #TYPE "_SET", "a" SUFFIX "set", 3, Window, D, 0) \
    X(NOP_ARRAY_##TYPE##_APPEND, "ARRAY_" #TYPE "_APPEND", "a" SUFFIX "append", 2, Binary, D, 0)

#define NEOCORE_SET_OPS(X, TYPE, SUFFIX) \
    X(NOP_SET_##TYPE##_CONTAINS, "SET_" #TYPE "_CONTAINS", "s" SUFFIX "contains", 2, Binary, 0, 0) \
    X(NOP_SET_##TYPE##_ADD, "SET_" #TYPE "_ADD", "s" SUFFIX "add", 2, Binary, 0, 0) \
    X(NOP_SET_##TYPE##_REMOVE, "SET_" #TYPE "_REMOVE", "s" SUFFIX "remove", 2, Binary, 0, 0)

// 맵·집합은 저장소를 공유한다. 뒤 인자의 부작용이 있을 때만 입력의 정체성을 보존한다.
#define NEOCORE_IDENTITY_OPS(X) \
    X(NOP_MAP_I64_READ, "MAP_I64_READ", "miread", 2, Binary, 0, 0) \
    X(NOP_MAP_STRING_READ, "MAP_STRING_READ", "msread", 2, Binary, 0, 0) \
    X(NOP_MAP_I64_CONTAINS, "MAP_I64_CONTAINS", "micontains", 2, Binary, 0, 0) \
    X(NOP_MAP_STRING_CONTAINS, "MAP_STRING_CONTAINS", "mscontains", 2, Binary, 0, 0) \
    NEOCORE_SET_OPS(X, I64, "i") \
    NEOCORE_SET_OPS(X, STRING, "s")

// 스칼라 접근은 저장소와 원소 타입을 컴파일 때 확정한다. Store는 n1[ n2 ] = n3이다.
#define NEOCORE_SCALAR_ACCESS_FOR(X, OWNER, VALUE, SUFFIX) \
    X(NOP_##OWNER##_##VALUE##_LOAD, #OWNER "_" #VALUE "_LOAD", SUFFIX "load", 2, Binary, 0, 0) \
    X(NOP_##OWNER##_##VALUE##_STORE, #OWNER "_" #VALUE "_STORE", "", 3, Store, 0, 0)

// 누적 읽기 n1 += n2[n3]. 컴파일러가 `x += c[k];` 문장의 읽기와 제자리 덧셈을 하나로 합칠 때 낸다.
// 접미사에 load가 들어 있어 저장 종류 추론이 읽기와 같은 규칙(n1 = 원소 종류)을 적용한다.
#define NEOCORE_SCALAR_ACCUMULATE_FOR(X, OWNER, VALUE, SUFFIX) \
    X(NOP_##OWNER##_##VALUE##_LOAD_ADD, #OWNER "_" #VALUE "_LOAD_ADD", SUFFIX "loadadd", 2, Binary, 0, 0)

#define NEOCORE_SCALAR_ACCESS_OPS(X) \
    NEOCORE_SCALAR_ACCESS_FOR(X, LIST, I64, "li") \
    NEOCORE_SCALAR_ACCESS_FOR(X, LIST, F64, "lf") \
    NEOCORE_SCALAR_ACCESS_FOR(X, LIST, BOOL, "lb") \
    NEOCORE_SCALAR_ACCESS_FOR(X, MAP_INT, I64, "mii") \
    NEOCORE_SCALAR_ACCESS_FOR(X, MAP_INT, F64, "mif") \
    NEOCORE_SCALAR_ACCESS_FOR(X, MAP_INT, BOOL, "mib") \
    NEOCORE_SCALAR_ACCESS_FOR(X, MAP_STR, I64, "msi") \
    NEOCORE_SCALAR_ACCESS_FOR(X, MAP_STR, F64, "msf") \
    NEOCORE_SCALAR_ACCESS_FOR(X, MAP_STR, BOOL, "msb") \
    NEOCORE_SCALAR_ACCUMULATE_FOR(X, LIST, I64, "li") \
    NEOCORE_SCALAR_ACCUMULATE_FOR(X, LIST, F64, "lf") \
    NEOCORE_SCALAR_ACCUMULATE_FOR(X, MAP_INT, I64, "mii") \
    NEOCORE_SCALAR_ACCUMULATE_FOR(X, MAP_INT, F64, "mif") \
    NEOCORE_SCALAR_ACCUMULATE_FOR(X, MAP_STR, I64, "msi") \
    NEOCORE_SCALAR_ACCUMULATE_FOR(X, MAP_STR, F64, "msf") \
    X(NOP_ARRAY_I64_STORE, "ARRAY_I64_STORE", "", 3, Store, 0, 0) \
    X(NOP_ARRAY_F64_STORE, "ARRAY_F64_STORE", "", 3, Store, 0, 0) \
    X(NOP_ARRAY_BOOL_STORE, "ARRAY_BOOL_STORE", "", 3, Store, 0, 0) \
    X(NOP_ARRAY_I32_STORE, "ARRAY_I32_STORE", "", 3, Store, 0, 0) \
    X(NOP_ARRAY_F32_STORE, "ARRAY_F32_STORE", "", 3, Store, 0, 0)

#define NEOCORE_AGGREGATE_PRIMARY_OPS(X) \
    NEOCORE_VECTOR_PRIMARY_OPS(X, 2, Binary) \
    NEOCORE_VECTOR_PRIMARY_OPS(X, 3, Window) \
    NEOCORE_VECTOR_PRIMARY_OPS(X, 4, Window) \
    X(NOP_CELL_NEW, "CELL_NEW", "cellnew", 1, Unary, 0, 0) \
    X(NOP_CELL_GET, "CELL_GET", "cellget", 1, Unary, 0, 0) \
    X(NOP_CELL_SET, "CELL_SET", "cellset", 2, Binary, 0, 0) \
    NEOCORE_ARRAY_OPS(X, I64, "i64", 0) \
    NEOCORE_ARRAY_OPS(X, F64, "f64", 0) \
    NEOCORE_ARRAY_OPS(X, BOOL, "b", 0) \
    NEOCORE_ARRAY_OPS(X, V2, "v2", 2) \
    NEOCORE_ARRAY_OPS(X, V3, "v3", 3) \
    NEOCORE_ARRAY_OPS(X, V4, "v4", 4) \
    NEOCORE_ARRAY_OPS(X, I32, "i32", 0) \
    NEOCORE_ARRAY_OPS(X, F32, "f32", 0) \
    X(NOP_ARRAY_LEN, "ARRAY_LEN", "alen", 1, Unary, 0, 0) \
    X(NOP_ARRAY_RESIZE, "ARRAY_RESIZE", "aresize", 2, Binary, 0, 0) \
    X(NOP_ARRAY_RESERVE, "ARRAY_RESERVE", "areserve", 2, Binary, 0, 0) \
    NEOCORE_IDENTITY_OPS(X) \
    NEOCORE_SCALAR_ACCESS_OPS(X)

#define NEOCORE_VECTOR_ARITHMETIC_FAMILIES(X) \
    NEOCORE_VECTOR_ARITHMETIC_OPS(X, 2) \
    NEOCORE_VECTOR_ARITHMETIC_OPS(X, 3) \
    NEOCORE_VECTOR_ARITHMETIC_OPS(X, 4)

// 로드 시 (기본 opcode, 세부 연산, 제자리 여부)를 한 바이트 opcode로 펼치는 런타임 전용 명령 목록.
// 이미지에는 나타나지 않는다. 디스패치 루프와 퓨전 실행기가 switch 한 번으로 고르고, 세부 연산은
// 템플릿 인자로 고정되어 핸들러 안의 두 번째 분기가 사라진다. X(T, NAME, SUB)에서 T는 I64/F64,
// SUB는 ArithmeticOp/ComparisonOp 멤버다. 산술은 3항 형식과 제자리 형식(이름 뒤 2)을 함께 만든다.
#define NEOCORE_RUNTIME_I64_ARITH(X) \
    X(I64, ADD, Add) X(I64, SUB, Sub) X(I64, MUL, Mul) X(I64, DIV, Div) X(I64, MOD, Mod) \
    X(I64, AND, BitAnd) X(I64, OR, BitOr) X(I64, XOR, BitXor) X(I64, SHL, ShiftLeft) X(I64, SHR, ShiftRight)
#define NEOCORE_RUNTIME_F64_ARITH(X) \
    X(F64, ADD, Add) X(F64, SUB, Sub) X(F64, MUL, Mul) X(F64, DIV, Div)
// 오른쪽 피연산자가 16비트 정수 상수인 3항 정수 산술은 로드 시 즉값 형식(NOP_RT_I64_NAME_IMM)으로 바뀐다.
// n3에 상수 값이 들어가 상수 슬롯을 읽지 않는다. 나눗셈·나머지는 0 검사가 남으므로 제외하고, 시프트는
// 0~63의 상수만 바꾼다. 정수 비교 점프(NOP_RT_JMP_I64_NAME_IMM)도 같은 조건으로 바뀐다.
#define NEOCORE_RUNTIME_I64_IMM_ARITH(X) \
    X(I64, ADD, Add) X(I64, SUB, Sub) X(I64, MUL, Mul) X(I64, AND, BitAnd) X(I64, OR, BitOr) \
    X(I64, XOR, BitXor) X(I64, SHL, ShiftLeft) X(I64, SHR, ShiftRight)
// 비교는 값 형식(NOP_RT_T_NAME)과 점프 형식(NOP_RT_JMP_T_NAME)을 만든다. 반전 비트는 argFlag에 남는다.
#define NEOCORE_RUNTIME_COMPARE(X, T) \
    X(T, EQ, Equal) X(T, LT, Less) X(T, LE, LessEqual) X(T, NE, NotEqual) X(T, GT, Greater) X(T, GE, GreaterEqual)

// 로드 시 인접한 두 명령을 한 디스패치로 실행하는 쌍 명령 목록. 첫 명령의 opcode만 NOP_RT_PAIR_NAME으로
// 바뀌고 둘째 명령은 바로 뒤에 그대로 남는다. 둘째로 들어오는 점프는 둘째만 실행하고, 둘째가 다시 다음 쌍의
// 첫 명령이어도 된다(쌍 핸들러는 둘째를 원래 의미로 실행한다). X(NAME, FIRST, SECOND). 목록은 벤치와 게임
// 스크립트의 동적 인접 빈도로 골랐다(PatchLocalOps 참조).
// 첫 명령이 오류 트랩을 세우거나 제어를 옮긴 뒤에 둘째를 실행하면 안 된다(트랩 명령을 둘째로 먹는다). 로더가
// 규칙마다 첫 명령을 검사한다. 허용하는 첫 명령은 두 종류다.
//   - IsPairFirstOpcode: 실패도 제어 이동도 없는 명령. payload 복사, 즉값 저장, 넘침이 감기는 정수 산술, float 산술,
//     순수 수학 네이티브.
//   - IsPairGuardedFirst: 실패할 수 있지만 제어를 옮기지 않고 VM에 다시 들어오지 않는 스칼라 배열 읽기·쓰기.
//     핸들러가 첫 명령의 성공을 확인한 뒤에만 둘째를 실행한다(ARRAY_* 쌍).
// 소유 값을 놓는 일반 MOV, 정수 나눗셈, 맵·목록 접근, 호출, 점프는 첫 명령이 될 수 없다.
// 호출·반환·반복 점검·배열 접근처럼 본문이 큰 둘째 명령은 핸들러가 원래 case의 본문으로 합류해 한 벌만 둔다.
#define NEOCORE_RUNTIME_PAIRS(X) \
    X(MOVL_MOVL,            NOP_RT_MOV_PAYLOAD_L,  NOP_RT_MOV_PAYLOAD_L) \
    X(MOVL_CALL_L,          NOP_RT_MOV_PAYLOAD_L,  NOP_CALL_L) \
    X(MOVG_MOVG,            NOP_RT_MOV_PAYLOAD_GL, NOP_RT_MOV_PAYLOAD_GL) \
    X(MOVI_L_JMP_TO_FOR,    NOP_RT_MOVI_L,         NOP_RT_JMP_TO_FOR) \
    X(MOVI_L_MOVI_L,        NOP_RT_MOVI_L,         NOP_RT_MOVI_L) \
    X(I64_MUL_IMM_ADD,      NOP_RT_I64_MUL_IMM,    NOP_RT_I64_ADD) \
    X(F64_SUB_F64_SUB,      NOP_RT_F64_SUB,        NOP_RT_F64_SUB) \
    X(NATIVE_F2_L_RETURN_L, NOP_RT_NATIVE_F2_L,    NOP_RETURN_L) \
    X(ARRAY_I64_STORE_JMP_FOR,         NOP_ARRAY_I64_STORE, NOP_JMP_FOR) \
    X(ARRAY_I64_GET_JMP_I64_EQ_TO_FOR, NOP_ARRAY_I64_GET,   NOP_RT_JMP_I64_EQ_TO_FOR) \
    X(ARRAY_F64_GET_CALL_L,            NOP_ARRAY_F64_GET,   NOP_CALL_L) \
    X(ARRAY_F64_GET_ARRAY_F64_GET,     NOP_ARRAY_F64_GET,   NOP_ARRAY_F64_GET) \
    X(ARRAY_BOOL_GET_JMP_FALSE,        NOP_ARRAY_BOOL_GET,  NOP_JMP_FALSE)
// 세 개나 네 개의 명령을 한 디스패치로 실행하는 묶음. 규칙은 쌍과 같고, 마지막을 뺀 모든 명령이 첫 명령의 조건을
// 지켜야 한다. X(NAME, FIRST, SECOND, THIRD, FOURTH)에서 세 개짜리는 FOURTH가 NOP_NONE이다. 같은 자리에서 쌍보다
// 먼저 고른다.
#define NEOCORE_RUNTIME_CHAINS(X) \
    X(I64_ADD_ADD2_JMP_FOR,                    NOP_RT_I64_ADD,        NOP_RT_I64_ADD2, NOP_JMP_FOR,         NOP_NONE) \
    X(F64_MUL_DIV_ADD,                         NOP_RT_F64_MUL,        NOP_RT_F64_DIV,  NOP_RT_F64_ADD,      NOP_NONE) \
    X(MOVG_MOVI_L_JMP_TO_FOR,                  NOP_RT_MOV_PAYLOAD_GL, NOP_RT_MOVI_L,   NOP_RT_JMP_TO_FOR,   NOP_NONE) \
    X(I64_MUL_IMM_ADD_ARRAY_I64_STORE_JMP_FOR, NOP_RT_I64_MUL_IMM,    NOP_RT_I64_ADD,  NOP_ARRAY_I64_STORE, NOP_JMP_FOR)

namespace NeoScript
{
typedef unsigned short OperationCode;
enum eNOperation : unsigned char;

struct TypedOpInfo
{
    OperationCode op;
    const char* name;
    const char* suffix;
    unsigned char arity;
    unsigned char slots;
    unsigned char writes;
    bool comparison;
    bool jump;
    OperationCode branchOp;
};

inline const TypedOpInfo* GetTypedOpInfo(OperationCode op);

enum class AggregateOpShape : unsigned char { Unary, Binary, Window, Store };

struct AggregateOpInfo
{
    OperationCode op;
    const char* name;
    const char* suffix;
    unsigned char arity;
    AggregateOpShape shape;
    unsigned char dimension;
    unsigned char controlMax;

    unsigned Slots() const
    { return shape == AggregateOpShape::Binary || shape == AggregateOpShape::Store ? 7 : 3; }
};

inline const AggregateOpInfo* GetAggregateOpInfo(OperationCode op);
}
