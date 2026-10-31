#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "NeoCoreTypes.h"

namespace NeoCore
{

enum class IRValueType { Invalid, Int, Float, Bool, String, Void, Value };
enum class IROp
{
    IntLiteral, FloatLiteral, BoolLiteral, Symbol, LegacyExpression,
    Add, Subtract, Multiply, Divide, Remainder, BitAnd, BitOr, BitXor,
    ShiftLeft, ShiftRight, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    Negate, BitNot, LogicalNot, ToFloat, ToInt,
    LoadStorage, Assign, AddAssign, SubtractAssign, MultiplyAssign, DivideAssign,
    RemainderAssign, BitAndAssign, BitOrAssign, BitXorAssign, ShiftLeftAssign, ShiftRightAssign,
    PreIncrement, PreDecrement, PostIncrement, PostDecrement,
    ScriptCall, NullableConvert, CopyStruct
};

enum class IRStorageKind { Invalid, Variable, Cell };

struct IRStorageTarget
{
    IRStorageKind kind = IRStorageKind::Invalid;
    std::string backendName;
    std::size_t symbolId = 0;
    // 구조체의 평탄한 필드 경로는 우변 실행 뒤 루트부터 다시 찾는다.
    std::vector<std::size_t> fields;
};

struct IRSourceSpan
{
    std::size_t begin = 0, end = 0;
    int line = 1, column = 1;
};

struct IRNode
{
    IROp op = IROp::IntLiteral;
    IRValueType type = IRValueType::Invalid;
    std::uint32_t typeId = 0;
    IRSourceSpan source;
    std::vector<std::size_t> inputs;
    std::int64_t integer = 0;
    double number = 0;
    bool boolean = false;
    std::size_t symbolId = 0;
    std::string backendName;
    // 아직 이전 컴파일 경로를 쓰는 잎은 평가 순서에 도달했을 때만 해석한다.
    std::string legacySource;
    IRStorageTarget storage;
    std::size_t functionIndex = static_cast<std::size_t>(-1);
};

struct IRFunction
{
    std::string backendName;
    TypeId returnType = InvalidType;
    std::vector<TypeId> parameterTypes;
};

struct IRReturn
{
    std::size_t functionIndex = static_cast<std::size_t>(-1);
    std::size_t valueIndex = static_cast<std::size_t>(-1);
    IRSourceSpan source;
};

struct TypedIR
{
    // 본문에는 루트 번호만 남기고 타입·연산·피연산자는 이 구조에 보관한다.
    std::string prefix;
    std::string returnPrefix;
    // 호출 경계의 정확한 타입 계약은 컴파일 요청 동안만 유지한다.
    TypeRegistry types;
    std::vector<IRNode> nodes;
    std::vector<IRFunction> functions;
    std::vector<IRReturn> returns;
};

}
