#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace NeoCore
{

enum class IRValueType { Invalid, Int, Float, Bool };
enum class IROp
{
    IntLiteral, FloatLiteral, BoolLiteral, Symbol, LegacyExpression,
    Add, Subtract, Multiply, Divide, Remainder, BitAnd, BitOr, BitXor,
    ShiftLeft, ShiftRight, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
    Negate, BitNot, LogicalNot, ToFloat, ToInt
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
};

struct TypedIR
{
    // 본문에는 루트 번호만 남기고 타입·연산·피연산자는 이 구조에 보관한다.
    std::string prefix;
    std::vector<IRNode> nodes;
};

}
