#pragma once

// 타입이 검증된 숫자 명령의 정의를 등록·저장·검증·표시에서 함께 사용한다.
// arity는 입력 수이고 slots/writes는 n1부터 시작하는 피연산자 비트 마스크다.
#define NEOCORE_TYPED_OPS(X) \
    X(NOP_I64_ADD, "I64_ADD", "iadd", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_I64_SUB, "I64_SUB", "isub", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_I64_MUL, "I64_MUL", "imul", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_I64_DIV, "I64_DIV", "idiv", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_I64_MOD, "I64_MOD", "imod", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_I64_AND, "I64_AND", "iand", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_I64_OR, "I64_OR", "ior", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_I64_XOR, "I64_XOR", "ixor", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_I64_SHL, "I64_SHL", "ishl", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_I64_SHR, "I64_SHR", "ishr", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_F64_ADD, "F64_ADD", "fadd", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_F64_SUB, "F64_SUB", "fsub", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_F64_MUL, "F64_MUL", "fmul", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_F64_DIV, "F64_DIV", "fdiv", 2, 7, 1, false, false, NOP_NONE) \
    X(NOP_I64_NEG, "I64_NEG", "ineg", 1, 3, 1, false, false, NOP_NONE) \
    X(NOP_F64_NEG, "F64_NEG", "fneg", 1, 3, 1, false, false, NOP_NONE) \
    X(NOP_I64_TO_F64, "I64_TO_F64", "itof", 1, 3, 1, false, false, NOP_NONE) \
    X(NOP_F64_TO_I64, "F64_TO_I64", "ftoi", 1, 3, 1, false, false, NOP_NONE) \
    X(NOP_I64_EQ, "I64_EQ", "ieq", 2, 7, 1, true, false, NOP_JMP_I64_EQ) \
    X(NOP_I64_LT, "I64_LT", "ilt", 2, 7, 1, true, false, NOP_JMP_I64_LT) \
    X(NOP_I64_LE, "I64_LE", "ile", 2, 7, 1, true, false, NOP_JMP_I64_LE) \
    X(NOP_F64_EQ, "F64_EQ", "feq", 2, 7, 1, true, false, NOP_JMP_F64_EQ) \
    X(NOP_F64_LT, "F64_LT", "flt", 2, 7, 1, true, false, NOP_JMP_F64_LT) \
    X(NOP_F64_LE, "F64_LE", "fle", 2, 7, 1, true, false, NOP_JMP_F64_LE) \
    X(NOP_JMP_I64_EQ, "JMP_I64_EQ", "", 2, 6, 0, true, true, NOP_NONE) \
    X(NOP_JMP_I64_LT, "JMP_I64_LT", "", 2, 6, 0, true, true, NOP_NONE) \
    X(NOP_JMP_I64_LE, "JMP_I64_LE", "", 2, 6, 0, true, true, NOP_NONE) \
    X(NOP_JMP_F64_EQ, "JMP_F64_EQ", "", 2, 6, 0, true, true, NOP_NONE) \
    X(NOP_JMP_F64_LT, "JMP_F64_LT", "", 2, 6, 0, true, true, NOP_NONE) \
    X(NOP_JMP_F64_LE, "JMP_F64_LE", "", 2, 6, 0, true, true, NOP_NONE)

// 집합 명령의 물리 피연산자 모양은 숫자 명령과 분리한다. Window는 연속 지역 슬롯을 읽는다.
#define NEOCORE_VECTOR_OPS(X, D, MAKE_SHAPE) \
    X(NOP_V##D##_MAKE, "V" #D "_MAKE", "v" #D "make", D, MAKE_SHAPE, D, 0) \
    X(NOP_V##D##_ADD, "V" #D "_ADD", "v" #D "add", 2, Binary, D, 0) \
    X(NOP_V##D##_SUB, "V" #D "_SUB", "v" #D "sub", 2, Binary, D, 0) \
    X(NOP_V##D##_MUL, "V" #D "_MUL", "v" #D "mul", 2, Binary, D, 0) \
    X(NOP_V##D##_DIV, "V" #D "_DIV", "v" #D "div", 2, Binary, D, 0) \
    X(NOP_V##D##_GET, "V" #D "_GET", "v" #D "get", 1, Unary, D, D - 1) \
    X(NOP_V##D##_SET, "V" #D "_SET", "v" #D "set", 2, Window, D, D - 1)

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

// n1은 기존 값을 읽은 뒤 덮어쓴다. 숫자 종류와 키 종류는 명령 자체로 확정한다.
#define NEOCORE_MAP_COMPOUND_FOR(X, KEY, VALUE) \
    X(NOP_MAP_##KEY##_##VALUE##_ADD, "MAP_" #KEY "_" #VALUE "_ADD", "", 2, Compound, 0, 0) \
    X(NOP_MAP_##KEY##_##VALUE##_SUB, "MAP_" #KEY "_" #VALUE "_SUB", "", 2, Compound, 0, 0) \
    X(NOP_MAP_##KEY##_##VALUE##_MUL, "MAP_" #KEY "_" #VALUE "_MUL", "", 2, Compound, 0, 0) \
    X(NOP_MAP_##KEY##_##VALUE##_DIV, "MAP_" #KEY "_" #VALUE "_DIV", "", 2, Compound, 0, 0)

#define NEOCORE_MAP_COMPOUND_OPS(X) \
    NEOCORE_MAP_COMPOUND_FOR(X, I64, I64) \
    NEOCORE_MAP_COMPOUND_FOR(X, I64, F64) \
    NEOCORE_MAP_COMPOUND_FOR(X, STRING, I64) \
    NEOCORE_MAP_COMPOUND_FOR(X, STRING, F64)

#define NEOCORE_AGGREGATE_OPS(X) \
    NEOCORE_VECTOR_OPS(X, 2, Binary) \
    NEOCORE_VECTOR_OPS(X, 3, Window) \
    NEOCORE_VECTOR_OPS(X, 4, Window) \
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
    NEOCORE_MAP_COMPOUND_OPS(X)

namespace NeoScript
{
enum eNOperation : unsigned char;

struct TypedOpInfo
{
    eNOperation op;
    const char* name;
    const char* suffix;
    unsigned char arity;
    unsigned char slots;
    unsigned char writes;
    bool comparison;
    bool jump;
    eNOperation branchOp;
};

inline const TypedOpInfo* GetTypedOpInfo(eNOperation op);

enum class AggregateOpShape : unsigned char { Unary, Binary, Window, Compound };

struct AggregateOpInfo
{
    eNOperation op;
    const char* name;
    const char* suffix;
    unsigned char arity;
    AggregateOpShape shape;
    unsigned char dimension;
    unsigned char controlMax;

    unsigned Slots() const
    { return shape == AggregateOpShape::Binary || shape == AggregateOpShape::Compound ? 7 : 3; }
};

inline const AggregateOpInfo* GetAggregateOpInfo(eNOperation op);
}
