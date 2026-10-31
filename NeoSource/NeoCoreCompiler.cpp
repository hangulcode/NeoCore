#include "NeoCoreCompiler.h"

#include "NeoCoreHostSchema.h"
#include "NeoCoreBuiltins.h"
#include "NeoNumberLiteral.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

namespace NeoCore
{
namespace
{

enum class TokenKind { End, Identifier, Integer, Float, String, Symbol };

struct Token
{
    TokenKind kind = TokenKind::End;
    std::string text;
    std::size_t begin = 0, end = 0;
    int line = 1, column = 1;
};

struct Diagnostic
{
    Token token;
    std::string message;
    Diagnostic(const Token& at, const std::string& why) : token(at), message(why) {}
};

static void Fail(const Token& token, const std::string& message)
{
    throw Diagnostic(token, message);
}

static bool IdentifierStart(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool IdentifierPart(unsigned char c)
{
    return IdentifierStart(c) || (c >= '0' && c <= '9');
}

class Lexer
{
public:
    explicit Lexer(const std::string& source) : source_(source) {}

    std::vector<Token> Read()
    {
        if (source_.size() >= 3 && source_.compare(0, 3, "\xef\xbb\xbf") == 0)
            offset_ = 3;
        std::vector<Token> tokens;
        for (;;)
        {
            SkipTrivia();
            Token token;
            token.begin = offset_;
            token.line = line_;
            token.column = column_;
            if (offset_ == source_.size())
            {
                token.end = offset_;
                tokens.push_back(token);
                return tokens;
            }
            const unsigned char c = static_cast<unsigned char>(source_[offset_]);
            if (IdentifierStart(c))
            {
                token.kind = TokenKind::Identifier;
                do { Advance(); } while (offset_ < source_.size() && IdentifierPart(source_[offset_]));
            }
            else if (std::isdigit(c) || (c == '.' && offset_ + 1 < source_.size() && std::isdigit(static_cast<unsigned char>(source_[offset_ + 1]))))
            {
                token.kind = TokenKind::Integer;
                if (c == '0' && offset_ + 1 < source_.size() && (source_[offset_ + 1] == 'x' || source_[offset_ + 1] == 'X'))
                {
                    Advance(); Advance();
                    const std::size_t firstDigit = offset_;
                    while (offset_ < source_.size() && std::isxdigit(static_cast<unsigned char>(source_[offset_]))) Advance();
                    if (offset_ == firstDigit) Fail(token, "expected hexadecimal digits");
                }
                else
                {
                    while (offset_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[offset_]))) Advance();
                    if (offset_ < source_.size() && source_[offset_] == '.' && !(offset_ + 1 < source_.size() && source_[offset_ + 1] == '.'))
                    {
                        token.kind = TokenKind::Float;
                        Advance();
                        while (offset_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[offset_]))) Advance();
                    }
                    if (offset_ < source_.size() && (source_[offset_] == 'e' || source_[offset_] == 'E'))
                    {
                        token.kind = TokenKind::Float;
                        Advance();
                        if (offset_ < source_.size() && (source_[offset_] == '+' || source_[offset_] == '-')) Advance();
                        const std::size_t firstDigit = offset_;
                        while (offset_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[offset_]))) Advance();
                        if (offset_ == firstDigit) Fail(token, "expected exponent digits");
                    }
                }
            }
            else if (c == '"' || c == '\'')
            {
                token.kind = TokenKind::String;
                const char quote = source_[offset_];
                Advance();
                bool closed = false;
                while (offset_ < source_.size())
                {
                    const char current = source_[offset_];
                    if (current == '\r' || current == '\n') Fail(token, "a string literal cannot contain a raw newline");
                    Advance();
                    if (current == quote) { closed = true; break; }
                    if (current == '\\' && offset_ < source_.size())
                    {
                        if (source_[offset_] == '\r' || source_[offset_] == '\n') Fail(token, "a string literal cannot contain a raw newline");
                        Advance();
                    }
                }
                if (!closed) Fail(token, "unterminated string literal");
            }
            else
            {
                token.kind = TokenKind::Symbol;
                static const char* const operators[] = {
                    "<<=", ">>=", "++", "--", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>",
                    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "..", "->"
                };
                std::size_t length = 0;
                for (const char* op : operators)
                {
                    const std::string spelling(op);
                    if (source_.compare(offset_, spelling.size(), spelling) == 0) { length = spelling.size(); break; }
                }
                if (!length)
                {
                    if (std::string("+-*/%~!&|^=<>?:;,.(){}[]").find(static_cast<char>(c)) == std::string::npos)
                        Fail(token, "unexpected character in source");
                    length = 1;
                }
                while (length--) Advance();
            }
            token.end = offset_;
            token.text = source_.substr(token.begin, token.end - token.begin);
            tokens.push_back(std::move(token));
        }
    }

private:
    const std::string& source_;
    std::size_t offset_ = 0;
    int line_ = 1, column_ = 1;

    void Advance()
    {
        if (source_[offset_++] == '\n') { ++line_; column_ = 1; }
        else ++column_;
    }

    void SkipTrivia()
    {
        for (;;)
        {
            while (offset_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[offset_]))) Advance();
            if (source_.compare(offset_, 2, "//") == 0)
            {
                while (offset_ < source_.size() && source_[offset_] != '\n') Advance();
            }
            else if (source_.compare(offset_, 2, "/*") == 0)
            {
                Token start;
                start.line = line_; start.column = column_;
                Advance(); Advance();
                while (offset_ < source_.size() && source_.compare(offset_, 2, "*/") != 0) Advance();
                if (offset_ == source_.size()) Fail(start, "unterminated block comment");
                Advance(); Advance();
            }
            else return;
        }
    }
};

struct Symbol;
struct Statement;
enum class ExprKind { Literal, Name, Unary, Binary, Assignment, Call, Member, Index, List, Map, Lambda };
struct Expr
{
    ExprKind kind;
    Token token;
    std::size_t begin = 0, end = 0;
    std::string op;
    std::vector<std::unique_ptr<Expr>> children;
    std::unique_ptr<Statement> lambda;
    std::vector<Symbol*> captures;
    std::vector<Symbol*> lambdaParameters;
    TypeId type = InvalidType;
    TypeId constructorType = InvalidType;
    bool constant = false;
    bool constantInitializer = false;
    bool branchCondition = false;
    int treeDepth = 1;
    Symbol* symbol = nullptr;
    std::size_t fieldIndex = 0;
    bool cellStorage = false, widen = false, constantWiden = false, clone = false, box = false;
    std::string callName;
    std::size_t functionIndex = static_cast<std::size_t>(-1);
    bool receiverArgument = false;
    bool indirectCall = false;
    const HostProperty* hostProperty = nullptr;
    bool vectorComponent = false;
    bool nativeNonNull = false;
    bool writeTarget = false;
    std::string hostWriteHelper;
    std::string typedWriteIntrinsic;
    bool typedWriteFloat = false;
    std::string extraArgument;
    std::int64_t integerValue = 0;
    double floatValue = 0;
    Expr(ExprKind k, const Token& at) : kind(k), token(at), begin(at.begin), end(at.end), op(at.text) {}
};

struct TypeSyntax
{
    Token first;
    std::size_t end = 0;
    TypeId type = InvalidType;
};

struct Parameter
{
    TypeSyntax type;
    Token name;
};

enum class StatementKind { Empty, Block, Declaration, Function, Expression, If, While, For, Return, Break, Continue, Import, Struct, Foreach, Yield };
struct Statement
{
    StatementKind kind;
    Token token;
    Token name;
    TypeSyntax declared;
    std::size_t end = 0;
    bool isPublic = false, isExported = false, isConst = false;
    std::vector<Parameter> parameters;
    std::vector<std::unique_ptr<Expr>> expressions;
    std::vector<std::unique_ptr<Statement>> children;
    Statement(StatementKind k, const Token& at) : kind(k), token(at) {}
};

// 파싱과 타입 검사를 분리한다. 함수 서명은 모든 본문보다 먼저 등록할 수 있다.
class Parser
{
public:
    Parser(const std::vector<Token>& tokens, TypeRegistry& types) : tokens_(tokens), types_(types) {}

    std::vector<std::unique_ptr<Statement>> Read()
    {
        // 이름을 먼저 등록하여 뒤에서 정의된 구조체도 필드·함수 서명에서 찾는다.
        int braces = 0;
        std::set<std::string> declarations;
        for (std::size_t i = 0; i + 1 < tokens_.size(); ++i)
        {
            if (braces == 0 && tokens_[i].text == "struct")
            {
                const Token& name = tokens_[i + 1];
                if (name.kind != TokenKind::Identifier || IsReserved(name.text)) Fail(name, "expected a structure name");
                if (!declarations.insert(name.text).second || types_.DeclareStruct(name.text) == InvalidType) Fail(name, "duplicate or invalid structure name");
            }
            if (tokens_[i].text == "{") ++braces;
            else if (tokens_[i].text == "}") --braces;
        }
        std::vector<std::unique_ptr<Statement>> program;
        while (Peek().kind != TokenKind::End) program.push_back(ParseStatement(true));
        return program;
    }

private:
    std::vector<Token> tokens_;
    TypeRegistry& types_;
    std::size_t position_ = 0;
    int depth_ = 0;

    struct Depth
    {
        int& value;
        Depth(int& v, const Token& at) : value(v) { if (++value > 256) Fail(at, "source nesting exceeds compiler limit"); }
        ~Depth() { --value; }
    };

    const Token& Peek(std::size_t ahead = 0) const { return tokens_[std::min(position_ + ahead, tokens_.size() - 1)]; }
    bool At(const char* text) const { return Peek().text == text; }
    Token Take() { return tokens_[position_++]; }
    bool Accept(const char* text) { if (!At(text)) return false; ++position_; return true; }
    Token Expect(const char* text)
    {
        if (!At(text)) Fail(Peek(), std::string("expected '") + text + "'");
        return Take();
    }

    Token TypeClose()
    {
        if (At(">>"))
        {
            Token first = Peek();
            first.text = ">"; --first.end;
            Token& second = tokens_[position_];
            second.text = ">"; ++second.begin; ++second.column;
            return first;
        }
        return Expect(">");
    }

    static bool IsReserved(const std::string& name)
    {
        static const char* const names[] = { "var", "fun", "int", "float", "bool", "string", "void", "int32", "float32", "any", "null", "true", "false", "if", "else", "while", "for", "foreach", "in", "return", "break", "continue", "import", "public", "export", "const", "struct", "class", "switch", "case", "default", "print", "tostring", "toint", "tofloat", "tosize", "type", "sleep", "yield", "__LINE__", "list", "map", "array" };
        for (const char* value : names) if (name == value) return true;
        return false;
    }

    Token Name()
    {
        if (Peek().kind != TokenKind::Identifier || IsReserved(Peek().text)) Fail(Peek(), "expected an identifier");
        return Take();
    }

    TypeId FindType(const std::string& name) const
    {
        const TypeId builtin = types_.LookupBuiltin(name);
        return builtin != InvalidType ? builtin : types_.LookupNamed(name);
    }

    TypeSyntax ParseType()
    {
        Depth guard(depth_, Peek());
        TypeSyntax result;
        result.first = Take();
        result.end = result.first.end;
        result.type = FindType(result.first.text);
        if (result.first.text == "fn")
        {
            Expect("(");
            std::vector<TypeId> parameters;
            if (!At(")")) do { parameters.push_back(ParseType().type); } while (Accept(","));
            Expect(")"); Expect("->");
            const TypeSyntax returnType = ParseType();
            result.type = types_.FunctionOf(returnType.type, parameters);
            result.end = returnType.end;
        }
        if (result.first.text == "list" || result.first.text == "map" || result.first.text == "array" || result.first.text == "set" || result.first.text == "coroutine")
        {
            Expect("<");
            const TypeSyntax first = ParseType();
            if (result.first.text == "coroutine")
            {
                result.type = types_.CoroutineOf(first.type);
                if (result.type == InvalidType) Fail(first.first, "a coroutine requires a complete function signature returning void");
            }
            else if (result.first.text == "array")
            {
                const TypeKind element = types_.Get(first.type).kind;
                if (element != TypeKind::Int && element != TypeKind::Float && element != TypeKind::Bool
                    && element != TypeKind::Vector2 && element != TypeKind::Vector3 && element != TypeKind::Vector4 && element != TypeKind::Quaternion)
                    Fail(first.first, "array elements must be non-null int, float, bool, or vector types");
                result.type = types_.ArrayOf(first.type);
            }
            else if (result.first.text == "list")
            {
                if (!types_.IsStorable(first.type) || types_.IsNullable(first.type))
                    Fail(first.first, "list elements must have a non-null value type");
                result.type = types_.ListOf(first.type);
            }
            else if (result.first.text == "set")
            {
                if (first.type != types_.Int() && first.type != types_.Float() && first.type != types_.Bool() && first.type != types_.String())
                    Fail(first.first, "set elements must be non-null scalar types");
                result.type = types_.SetOf(first.type);
            }
            else
            {
                if (first.type != types_.Int() && first.type != types_.String()) Fail(first.first, "map keys must be int or string");
                Expect(",");
                const TypeSyntax value = ParseType();
                if (!types_.IsStorable(value.type) || types_.IsNullable(value.type))
                    Fail(value.first, "map values must have a non-null value type");
                result.type = types_.MapOf(first.type, value.type);
            }
            result.end = TypeClose().end;
        }
        if (result.type == InvalidType)
            Fail(result.first, "unknown type '" + result.first.text + "'");
        const TypeKind kind = types_.Get(result.type).kind;
        if (kind != TypeKind::Void && !types_.IsStorable(result.type))
            Fail(result.first, "type '" + result.first.text + "' is not supported by this compiler stage");
        if (At("?"))
        {
            const Token nullable = Take();
            if (kind == TypeKind::Struct) Fail(nullable, "nullable structures are not supported by this compiler stage");
            result.end = nullable.end;
            result.type = types_.NullableOf(result.type);
            if (result.type == InvalidType) Fail(nullable, "this type cannot be nullable");
        }
        if (At("&"))
            Fail(Peek(), "reference types are not supported; pass and return values instead");
        return result;
    }

    std::unique_ptr<Statement> ParseStatement(bool topLevel)
    {
        Depth guard(depth_, Peek());
        const Token at = Peek();
        if (Accept(";")) { std::unique_ptr<Statement> s(new Statement(StatementKind::Empty, at)); s->end = at.end; return s; }
        if (Accept("{"))
        {
            std::unique_ptr<Statement> s(new Statement(StatementKind::Block, at));
            while (!At("}"))
            {
                if (Peek().kind == TokenKind::End) Fail(Peek(), "expected '}'");
                s->children.push_back(ParseStatement(false));
            }
            s->end = Take().end;
            return s;
        }
        if (At("var") || At("fun")) Fail(at, "NeoCore requires explicit types; 'var' and 'fun' declarations are not supported");
        if (At("int32") || At("float32") || At("any")) Fail(at, "this name is not a user-declarable type");
        if (Accept("struct"))
        {
            if (!topLevel) Fail(at, "structures are only allowed at file scope");
            std::unique_ptr<Statement> s(new Statement(StatementKind::Struct, at));
            s->name = Name();
            s->declared.type = types_.LookupNamed(s->name.text);
            Expect("{");
            std::vector<StructField> fields;
            while (!At("}"))
            {
                Parameter field;
                field.type = ParseType();
                field.name = Name();
                if (At("=")) Fail(Peek(), "structure field initializers are not supported; use the positional constructor");
                Expect(";");
                fields.push_back(StructField(field.name.text, field.type.type));
                s->parameters.push_back(field);
            }
            Take(); s->end = Expect(";").end;
            std::string error;
            if (!types_.DefineStructFields(s->declared.type, fields, &error)) Fail(s->name, error);
            return s;
        }
        if (At("class") || At("switch")) Fail(at, "this statement is not supported by this compiler stage");
        if (Accept("import"))
        {
            if (!topLevel) Fail(at, "import is only allowed at file scope");
            std::unique_ptr<Statement> s(new Statement(StatementKind::Import, at));
            s->name = Name();
            if (s->name.text != "math" && s->name.text != "system" && s->name.text != "coroutine") Fail(s->name, "user modules are not supported by this compiler stage");
            Expect(";");
            return s;
        }
        if (Accept("if"))
        {
            std::unique_ptr<Statement> s(new Statement(StatementKind::If, at));
            Expect("("); s->expressions.push_back(ParseExpression()); Expect(")");
            s->children.push_back(ParseStatement(false));
            if (Accept("else")) s->children.push_back(ParseStatement(false));
            s->end = s->children.back()->end;
            return s;
        }
        if (Accept("while"))
        {
            std::unique_ptr<Statement> s(new Statement(StatementKind::While, at));
            Expect("("); s->expressions.push_back(ParseExpression()); Expect(")");
            s->children.push_back(ParseStatement(false));
            s->end = s->children.back()->end;
            return s;
        }
        if (Accept("for"))
        {
            std::unique_ptr<Statement> s(new Statement(StatementKind::For, at));
            Expect("("); s->declared = ParseType(); s->name = Name(); Expect("in");
            s->expressions.push_back(ParseExpression()); Expect(","); s->expressions.push_back(ParseExpression());
            if (Accept(",")) s->expressions.push_back(ParseExpression());
            Expect(")"); s->children.push_back(ParseStatement(false));
            s->end = s->children.back()->end;
            return s;
        }
        if (Accept("return"))
        {
            std::unique_ptr<Statement> s(new Statement(StatementKind::Return, at));
            if (!At(";")) s->expressions.push_back(ParseExpression());
            s->end = Expect(";").end;
            return s;
        }
        if (Accept("foreach"))
        {
            std::unique_ptr<Statement> s(new Statement(StatementKind::Foreach, at));
            Expect("(");
            do { Parameter p; p.type = ParseType(); p.name = Name(); s->parameters.push_back(p); } while (Accept(","));
            if (s->parameters.size() > 2) Fail(at, "foreach accepts one value or a key and value");
            Expect("in"); s->expressions.push_back(ParseExpression()); Expect(")");
            s->children.push_back(ParseStatement(false));
            s->end = s->children.back()->end;
            return s;
        }
        if (Accept("yield"))
        {
            std::unique_ptr<Statement> s(new Statement(StatementKind::Yield, at));
            s->end = Expect(";").end;
            return s;
        }
        if (At("break") || At("continue"))
        {
            const StatementKind kind = At("break") ? StatementKind::Break : StatementKind::Continue;
            Take();
            std::unique_ptr<Statement> s(new Statement(kind, at));
            s->end = Expect(";").end;
            return s;
        }
        const bool arrayConstructor = (At("array") || At("set")) && Peek(1).text == "<" && Peek(3).text == ">" && Peek(4).text == "(";
        if (At("public") || At("export") || At("const") || At("fn") || At("list") || At("map") || (At("coroutine") && Peek(1).text == "<") || ((At("array") || At("set")) && !arrayConstructor) || (FindType(Peek().text) != InvalidType && (Peek(1).kind == TokenKind::Identifier || Peek(1).text == "?" || Peek(1).text == "&")))
        {
            std::unique_ptr<Statement> s(new Statement(StatementKind::Declaration, at));
            while (At("public") || At("export") || At("const"))
            {
                const Token modifier = Take();
                if (!topLevel) Fail(modifier, "public, export and const are only allowed at file scope");
                bool& flag = modifier.text == "public" ? s->isPublic : modifier.text == "export" ? s->isExported : s->isConst;
                if (flag) Fail(modifier, "duplicate declaration modifier");
                flag = true;
            }
            if (At("var") || At("fun") || At("int32") || At("float32") || At("any"))
                Fail(Peek(), "declarations require a supported explicit type");
            s->declared = ParseType();
            s->name = Name();
            if (Accept("("))
            {
                if (!topLevel) Fail(s->name, "named functions are only allowed at file scope");
                if (s->isConst) Fail(at, "a function cannot be const");
                s->kind = StatementKind::Function;
                if (!At(")"))
                {
                    do
                    {
                        Parameter parameter;
                        parameter.type = ParseType();
                        parameter.name = Name();
                        s->parameters.push_back(parameter);
                    } while (Accept(","));
                }
                Expect(")");
                if (!At("{")) Fail(Peek(), "a function definition requires a body");
                s->children.push_back(ParseStatement(false));
            }
            else
            {
                if (Accept("=")) s->expressions.push_back(ParseExpression());
                s->end = Expect(";").end;
            }
            return s;
        }
        std::unique_ptr<Statement> s(new Statement(StatementKind::Expression, at));
        s->expressions.push_back(ParseExpression());
        s->end = Expect(";").end;
        return s;
    }

    static int Precedence(const std::string& op)
    {
        if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "%=" || op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=") return 1;
        if (op == "||") return 2;
        if (op == "&&") return 3;
        if (op == "|") return 4;
        if (op == "^") return 5;
        if (op == "&") return 6;
        if (op == "==" || op == "!=") return 7;
        if (op == "<" || op == "<=" || op == ">" || op == ">=") return 8;
        if (op == "<<" || op == ">>") return 9;
        if (op == "..") return 10;
        if (op == "+" || op == "-") return 11;
        if (op == "*" || op == "/" || op == "%") return 12;
        return 0;
    }

    static void CheckTreeDepth(Expr& expression)
    {
        for (const auto& child : expression.children)
            expression.treeDepth = std::max(expression.treeDepth, child->treeDepth + 1);
        if (expression.treeDepth > 256) Fail(expression.token, "expression nesting exceeds compiler limit");
    }

    std::unique_ptr<Expr> ParseExpression(int minimum = 1)
    {
        Depth guard(depth_, Peek());
        std::unique_ptr<Expr> left = ParseUnary();
        for (;;)
        {
            const int precedence = Precedence(Peek().text);
            if (precedence < minimum) return left;
            const Token op = Take();
            std::unique_ptr<Expr> node(new Expr(precedence == 1 ? ExprKind::Assignment : ExprKind::Binary, op));
            node->begin = left->begin;
            node->children.push_back(std::move(left));
            node->children.push_back(ParseExpression(precedence + (precedence == 1 ? 0 : 1)));
            node->end = node->children[1]->end;
            CheckTreeDepth(*node);
            left = std::move(node);
        }
    }

    std::unique_ptr<Expr> ParseUnary()
    {
        Depth guard(depth_, Peek());
        if (At("+") || At("-") || At("!") || At("~") || At("++") || At("--"))
        {
            const Token op = Take();
            std::unique_ptr<Expr> node(new Expr(ExprKind::Unary, op));
            node->children.push_back(ParseUnary());
            node->end = node->children[0]->end;
            CheckTreeDepth(*node);
            return node;
        }
        std::unique_ptr<Expr> result;
        const Token token = Peek();
        if (Accept("("))
        {
            result = ParseExpression();
            result->begin = token.begin;
            result->end = Expect(")").end;
        }
        else if (Accept("["))
        {
            result.reset(new Expr(ExprKind::List, token));
            if (!At("]")) do { result->children.push_back(ParseExpression()); } while (Accept(","));
            result->end = Expect("]").end;
            CheckTreeDepth(*result);
        }
        else if (Accept("{"))
        {
            result.reset(new Expr(ExprKind::Map, token));
            if (!At("}")) do {
                result->children.push_back(ParseExpression()); Expect(":");
                result->children.push_back(ParseExpression());
            } while (Accept(","));
            result->end = Expect("}").end;
            CheckTreeDepth(*result);
        }
        else if (Accept("fn"))
        {
            result.reset(new Expr(ExprKind::Lambda, token));
            result->lambda.reset(new Statement(StatementKind::Function, token));
            Statement& function = *result->lambda;
            Expect("(");
            if (!At(")")) do { Parameter p; p.type = ParseType(); p.name = Name(); function.parameters.push_back(p); } while (Accept(","));
            Expect(")"); Expect("->"); function.declared = ParseType();
            if (!At("{")) Fail(Peek(), "an anonymous function requires a body");
            function.children.push_back(ParseStatement(false));
            result->end = tokens_[position_ - 1].end;
            function.end = result->end;
        }
        else if (token.kind == TokenKind::Integer || token.kind == TokenKind::Float || token.kind == TokenKind::String || At("true") || At("false") || At("null"))
        {
            Take(); result.reset(new Expr(ExprKind::Literal, token));
        }
        else if (At("array") || At("set"))
        {
            const TypeSyntax type = ParseType();
            if (!At("(")) Fail(token, "a collection constructor requires arguments in parentheses");
            result.reset(new Expr(ExprKind::Name, token));
            result->constructorType = type.type;
            result->end = type.end;
        }
        else if (token.kind == TokenKind::Identifier)
        {
            Take(); result.reset(new Expr(ExprKind::Name, token));
        }
        else Fail(token, "expected an expression");
        for (;;)
        {
            if (At("["))
            {
                const Token index = Take();
                std::unique_ptr<Expr> access(new Expr(ExprKind::Index, index));
                access->begin = result->begin;
                access->children.push_back(std::move(result));
                access->children.push_back(ParseExpression());
                access->end = Expect("]").end;
                CheckTreeDepth(*access);
                result = std::move(access);
            }
            else if (Accept("."))
            {
                const Token member = Name();
                std::unique_ptr<Expr> access(new Expr(ExprKind::Member, member));
                access->begin = result->begin;
                access->children.push_back(std::move(result));
                CheckTreeDepth(*access);
                result = std::move(access);
            }
            else if (Accept("("))
            {
                std::unique_ptr<Expr> call(new Expr(ExprKind::Call, result->token));
                call->begin = result->begin;
                call->children.push_back(std::move(result));
                if (!At(")"))
                {
                    do { call->children.push_back(ParseExpression()); } while (Accept(","));
                }
                call->end = Expect(")").end;
                CheckTreeDepth(*call);
                result = std::move(call);
            }
            else if (At("++") || At("--"))
            {
                const Token op = Take();
                std::unique_ptr<Expr> unary(new Expr(ExprKind::Unary, op));
                unary->op = "post" + op.text;
                unary->begin = result->begin;
                unary->end = op.end;
                unary->children.push_back(std::move(result));
                CheckTreeDepth(*unary);
                result = std::move(unary);
            }
            else return result;
        }
    }
};

struct Edit
{
    std::size_t begin, end;
    std::string text;
    Edit(std::size_t first, std::size_t last, const std::string& value) : begin(first), end(last), text(value) {}
};

struct Symbol
{
    TypeId type;
    bool constant;
    bool boxed = false, parameter = false;
    bool hostGlobal = false;
    bool fileGlobal = false;
    std::size_t hostGlobalIndex = 0;
    Token name;
    std::string backendName;
    Statement* declaration = nullptr;
    std::size_t functionBody = 0;
    std::string defaultValue;
    int functionDepth = 0;
    Symbol(TypeId t, bool c, const Token& n) : type(t), constant(c), name(n) {}
};

class Checker
{
public:
    Checker(const std::string& source, StaticCompileResult& result, const HostSchema* hostSchema) : source_(source), result_(result), types_(result.types), hostSchema_(hostSchema)
    {
        scopes_.push_back(std::map<std::string, Symbol*>());
        result_.backendIntrinsicPrefix = internalPrefix_ + "op_";
        result_.typedIR.prefix = internalPrefix_ + "ir_";
        result_.typedIR.returnPrefix = internalPrefix_ + "return_";
        if (hostSchema_) result_.hostPrefix = internalPrefix_ + "host_";
    }

    void Check(std::vector<std::unique_ptr<Statement>>& program)
    {
        if (hostSchema_)
        {
            for (std::size_t i = 0; i < hostSchema_->Globals().size(); ++i)
            {
                const HostGlobal& global = hostSchema_->Globals()[i];
                Token name;
                name.text = global.name;
                symbols_.push_back(Symbol(global.type, false, name));
                Symbol& symbol = symbols_.back();
                symbol.backendName = global.name;
                symbol.hostGlobal = true;
                symbol.hostGlobalIndex = i;
                scopes_[0].insert(std::make_pair(global.name, &symbol));
            }
        }
        for (const auto& node : program)
        {
            if (node->kind == StatementKind::Struct)
            {
                if (hostSchema_ && hostSchema_->FindGlobal(node->name.text)) Fail(node->name, "a structure cannot hide a host global");
                structures_.push_back(node->declared.type);
            }
            if (node->kind != StatementKind::Function) continue;
            if (functions_.count(node->name.text)) Fail(node->name, "duplicate function '" + node->name.text + "'");
            if (hostSchema_ && hostSchema_->FindGlobal(node->name.text)) Fail(node->name, "a function cannot hide a host global");
            if (types_.LookupNamed(node->name.text) != InvalidType) Fail(node->name, "a function cannot have the same name as a declared type");
            StaticFunctionSignature signature;
            signature.name = node->name.text;
            signature.returnType = node->declared.type;
            signature.exported = node->isExported;
            for (const Parameter& parameter : node->parameters)
            {
                if (!types_.IsStorable(parameter.type.type)) Fail(parameter.type.first, "a parameter cannot have this type");
                signature.parameterTypes.push_back(parameter.type.type);
            }
            functions_[signature.name] = signature;
            result_.functions.push_back(signature);
            {
                IRFunction function;
                function.backendName = signature.name;
                function.returnType = signature.returnType;
                function.parameterTypes = signature.parameterTypes;
                irFunctions_[signature.name] = result_.typedIR.functions.size();
                result_.typedIR.functions.push_back(std::move(function));
            }
        }
        for (const auto& node : program) CheckStatement(*node);
        CheckGlobalInitializationCalls();
        FinishStorage();
        for (Expr* expression : roots_)
        {
            std::string text = RenderExpr(*expression);
            edits_.push_back(Edit(expression->begin, expression->end, text));
        }
        for (const auto& entry : returns_)
        {
            const Statement& statement = *entry.first;
            IRReturn result;
            result.functionIndex = entry.second;
            result.source.begin = statement.token.begin;
            result.source.end = statement.end;
            result.source.line = statement.token.line;
            result.source.column = statement.token.column;
            if (!statement.expressions.empty())
                result.valueIndex = ConvertIR(BuildValueIR(*statement.expressions[0]),
                    result_.typedIR.functions[entry.second].returnType, result.source);
            const std::string marker = "return " + result_.typedIR.returnPrefix
                + std::to_string(result_.typedIR.returns.size()) + "();"
                + Lines(statement.token.begin, statement.end);
            result_.typedIR.returns.push_back(std::move(result));
            edits_.push_back(Edit(statement.token.begin, statement.end, marker));
        }
        result_.loweredSource = Lower();
        const std::string helpers = Helpers();
        int generatedLine = 1 + static_cast<int>(std::count(result_.loweredSource.begin(), result_.loweredSource.end(), '\n'));
        for (const HostWrite& write : hostWrites_)
            result_.hostWriteSourceLines.push_back(std::make_pair(++generatedLine, write.line));
        result_.loweredSource += helpers;
        result_.typedIR.types = types_;
    }

private:
    enum Flow { FallsThrough = 1, Returns = 2, Breaks = 4, Continues = 8 };
    const std::string& source_;
    StaticCompileResult& result_;
    TypeRegistry& types_;
    const HostSchema* hostSchema_;
    std::map<std::string, StaticFunctionSignature> functions_;
    std::map<std::string, std::size_t> irFunctions_;
    std::vector<std::pair<const Statement*, std::size_t>> returns_;
    std::string currentFunction_;
    std::map<std::string, std::size_t> requiredGlobalEnd_;
    std::map<std::string, std::vector<std::string>> functionCallers_;
    std::vector<std::pair<Token, std::string>> globalCalls_;
    std::vector<std::map<std::string, Symbol*>> scopes_;
    std::deque<Symbol> symbols_;
    std::vector<Expr*> roots_;
    std::vector<TypeId> structures_;
    std::set<std::string> imports_;
    std::vector<Edit> edits_;
    TypeId returnType_ = InvalidType;
    int loopDepth_ = 0;
    bool checkingConstant_ = false;
    const std::string internalPrefix_ = InternalSymbolPrefix();
    bool resizeHelper_ = false, getHelper_ = false;
    bool concatHelper_ = false, unionHelper_ = false, intersectionHelper_ = false, differenceHelper_ = false;
    std::set<TypeId> structListClones_, structMapClones_, structMapValues_, structListResizes_, structListConcats_;
    int functionDepth_ = 0;
    std::vector<Expr*> activeLambdas_;
    std::vector<Expr*> lambdas_;
    std::set<Symbol*> nonNullLocals_;
    mutable std::map<TypeId, bool> defaultable_;
    mutable std::map<const Symbol*, std::size_t> irSymbols_;
    struct HostWrite
    {
        std::string name;
        const HostProperty* property;
        std::string operation;
        TypeId valueType;
        int line;
        int component = -1;
    };
    std::vector<HostWrite> hostWrites_;
    void RecordScriptCall(const Token& call, const std::string& callee)
    {
        if (currentFunction_.empty()) globalCalls_.push_back(std::make_pair(call, callee));
        else functionCallers_[callee].push_back(currentFunction_);
    }

    void CheckGlobalInitializationCalls()
    {
        // 전역 초기화가 호출하는 함수도 미완성 전역을 읽거나 쓰면 정적 타입 증거가 깨진다.
        // 재귀를 포함한 호출 그래프에 필요한 초기화 완료 위치를 역방향으로 전파한다.
        std::deque<std::string> pending;
        for (const auto& function : requiredGlobalEnd_) pending.push_back(function.first);
        for (const auto& function : functions_) pending.push_back(function.first);
        while (!pending.empty())
        {
            const std::string callee = pending.front();
            pending.pop_front();
            const std::size_t required = requiredGlobalEnd_[callee];
            const auto callers = functionCallers_.find(callee);
            if (callers == functionCallers_.end()) continue;
            for (const std::string& caller : callers->second)
            {
                std::size_t& previous = requiredGlobalEnd_[caller];
                if (previous >= required) continue;
                previous = required;
                pending.push_back(caller);
            }
        }
        for (const auto& call : globalCalls_)
            if (call.first.begin < requiredGlobalEnd_[call.second])
                Fail(call.first, "function '" + call.second + "' can access a global before its initializer completes");
    }

    bool IsStruct(TypeId type) const { return types_.Get(type).kind == TypeKind::Struct; }
    bool IsScalarValue(TypeId type) const
    {
        if (types_.IsNullable(type)) type = types_.Get(type).element;
        return type == types_.Int() || type == types_.Float() || type == types_.Bool() || type == types_.String() || type == types_.Null();
    }
    bool IsVector(TypeId type) const
    {
        const TypeKind kind = types_.Get(type).kind;
        return kind == TypeKind::Vector2 || kind == TypeKind::Vector3 || kind == TypeKind::Vector4 || kind == TypeKind::Quaternion;
    }
    bool IsVectorValue(TypeId type) const { return IsVector(type) || (types_.IsNullable(type) && IsVector(types_.Get(type).element)); }
    bool IsArithmeticVector(TypeId type) const { return IsVector(type) && type != types_.Quaternion(); }
    int VectorSize(TypeId type) const { return type == types_.Vector2() ? 2 : type == types_.Vector3() ? 3 : 4; }
    std::string VectorConstructor(TypeId type, int line = 1) const
    {
        return Intrinsic("v" + std::to_string(VectorSize(type)) + "make", line);
    }
    std::string ArrayStorage(TypeId type) const
    {
        if (type == types_.Int()) return "i64";
        if (type == types_.Float()) return "f64";
        if (type == types_.Bool()) return "b";
        return "v" + std::to_string(VectorSize(type));
    }
    bool IsNative(TypeId type) const { return types_.Get(type).kind == TypeKind::Native; }
    bool IsNativeValue(TypeId type) const { return IsNative(type) || (types_.IsNullable(type) && IsNative(types_.Get(type).element)); }
    bool IsArray(TypeId type) const { return types_.Get(type).kind == TypeKind::Array; }
    bool IsCollection(TypeId type) const { return types_.Get(type).kind == TypeKind::List || types_.Get(type).kind == TypeKind::Map || types_.Get(type).kind == TypeKind::Set || IsArray(type); }
    std::string Helper(const char* operation, TypeId type) const { return internalPrefix_ + operation + "_" + std::to_string(type); }
    std::string HostCall(HostBindingId binding) const { return result_.hostPrefix + "b" + std::to_string(binding) + ".call"; }

    bool IsArrayWriteTarget(const Expr& target) const
    {
        const Expr& owner = target.vectorComponent ? *target.children[0] : target;
        return owner.kind == ExprKind::Index && IsArray(owner.children[0]->type);
    }

    void HostWriteHelper(Expr& expression, Expr& target, const std::string& operation, TypeId valueType)
    {
        Expr& property = target.vectorComponent ? *target.children[0] : target;
        const bool vectorCompound = IsArithmeticVector(target.type) && operation != "=";
        const bool arrayElement = IsArrayWriteTarget(target);
        const bool listElement = target.kind == ExprKind::Index && types_.Get(target.children[0]->type).kind == TypeKind::List
            && types_.IsNumeric(target.type) && operation != "=";
        if (!property.hostProperty && !target.vectorComponent && !vectorCompound && !arrayElement && !listElement) return;
        if (!property.hostProperty && !arrayElement && !listElement)
        {
            // 벡터 쓰기는 저장 셀을 전달한다. 성분 쓰기의 반환값은 저장된 float32를 다시 읽는다.
            Symbol* symbol = Writable(property);
            if (property.kind == ExprKind::Name) symbol->boxed = true;
            property.cellStorage = property.kind == ExprKind::Name;
        }
        if (!property.hostProperty)
        {
            int mode = 0;
            if (operation == "+=" || operation == "++") mode = 1;
            else if (operation == "-=" || operation == "--") mode = 2;
            else if (operation == "*=") mode = 3;
            else if (operation == "/=") mode = 4;
            else if (operation == "post++") mode = 5;
            else if (operation == "post--") mode = 6;
            else if (operation == "%=") mode = 7;
            else if (operation == "&=") mode = 8;
            else if (operation == "|=") mode = 9;
            else if (operation == "^=") mode = 10;
            else if (operation == "<<=") mode = 11;
            else if (operation == ">>=") mode = 12;
            std::string name;
            if (target.vectorComponent || vectorCompound)
            {
                name = (arrayElement ? "av" : "v") + std::to_string(VectorSize(property.type))
                    + (arrayElement ? "write_C" : property.kind == ExprKind::Member ? "fieldwrite_C" : "cellwrite_C")
                    + std::to_string(target.vectorComponent ? target.fieldIndex : 4) + "_M" + std::to_string(mode);
                expression.typedWriteFloat = target.vectorComponent || mode == 3 || mode == 4;
            }
            else
            {
                name = std::string(listElement ? "c" : "a") + ArrayStorage(target.type)
                    + (mode == 0 ? "set" : "write_M" + std::to_string(mode));
                expression.typedWriteFloat = target.type == types_.Float();
            }
            expression.typedWriteIntrinsic = Intrinsic(name, expression.token.line);
            return;
        }
        HostWrite write;
        write.name = internalPrefix_ + "hostwrite_" + std::to_string(hostWrites_.size());
        write.property = property.hostProperty;
        write.operation = operation;
        write.valueType = valueType;
        write.line = expression.token.line;
        if (target.vectorComponent) write.component = static_cast<int>(target.fieldIndex);
        expression.hostWriteHelper = write.name;
        hostWrites_.push_back(write);
    }

    const Expr& WriteReceiver(const Expr& target) const
    {
        const Expr& property = target.vectorComponent ? *target.children[0] : target;
        return !property.hostProperty ? property : *property.children[0];
    }

    std::string WriteReceiverArguments(const Expr& target) const
    {
        const Expr& property = target.vectorComponent ? *target.children[0] : target;
        if (!property.hostProperty && property.kind == ExprKind::Member)
            return RenderExpr(*property.children[0]) + "," + std::to_string(property.fieldIndex);
        if (property.kind != ExprKind::Index || !IsCollection(property.children[0]->type))
            return RenderExpr(WriteReceiver(target));
        // 컬렉션 저장소와 인덱스는 우변 뒤에 한 번씩 확정한다. 원소 포인터는 호출 밖으로 보관하지 않는다.
        const Expr& element = target.vectorComponent ? *target.children[0] : target;
        const Expr& owner = *element.children[0];
        const Expr& key = *element.children[1];
        return Lines(element.begin, owner.begin) + RenderExpr(owner) + Lines(owner.end, key.begin) + ","
            + RenderExpr(key) + Lines(key.end, element.end);
    }

    bool Defaultable(TypeId type) const
    {
        if (type == types_.Int() || type == types_.Float() || type == types_.Bool() || IsVector(type) || types_.IsNullable(type)) return true;
        if (!IsStruct(type)) return false;
        // 반복 필드가 같은 구조체를 가리켜도 기본값 가능 여부를 타입마다 한 번만 계산한다.
        std::vector<TypeId> pending(1, type);
        while (!pending.empty())
        {
            const TypeId current = pending.back();
            if (defaultable_.count(current)) { pending.pop_back(); continue; }
            const TypeDesc& description = types_.Get(current);
            bool complete = true;
            bool value = description.fieldsDefined;
            for (const StructField& field : description.fields)
            {
                if (IsStruct(field.type))
                {
                    const auto found = defaultable_.find(field.type);
                    if (found == defaultable_.end()) { pending.push_back(field.type); complete = false; break; }
                    value = value && found->second;
                }
                else value = value && (field.type == types_.Int() || field.type == types_.Float() || field.type == types_.Bool() || IsVector(field.type) || types_.IsNullable(field.type));
            }
            if (complete) { defaultable_[current] = value; pending.pop_back(); }
        }
        return defaultable_[type];
    }

    std::string DefaultValue(TypeId type) const
    {
        if (type == types_.Int()) return "0";
        if (type == types_.Float()) return "0.0";
        if (type == types_.Bool()) return "false";
        if (types_.IsNullable(type)) return "null";
        if (IsVector(type))
        {
            std::string result = VectorConstructor(type) + "(";
            for (int i = 0; i < VectorSize(type); ++i)
            {
                if (i) result += ",";
                result += type == types_.Quaternion() && i == 0 ? "1.0" : "0.0";
            }
            return result + ")";
        }
        if (IsStruct(type)) return Helper("default", type) + "()";
        return "";
    }

    Symbol* Lookup(const std::string& name) const
    {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
        {
            const auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        return nullptr;
    }

    bool CanRefineNative(const Symbol* symbol) const
    {
        return symbol && !symbol->fileGlobal && !symbol->hostGlobal && !symbol->boxed
            && types_.IsNullable(symbol->type);
    }

    void ApplyNativeGuard(const Expr& condition, bool whenTrue)
    {
        if (condition.kind == ExprKind::Unary && condition.op == "!")
        {
            ApplyNativeGuard(*condition.children[0], !whenTrue);
            return;
        }
        if (condition.kind != ExprKind::Binary || (condition.op != "==" && condition.op != "!=")) return;
        if (whenTrue != (condition.op == "!=")) return;
        const Expr* value = condition.children[0].get();
        const Expr* other = condition.children[1].get();
        if (value->kind == ExprKind::Literal && value->op == "null") std::swap(value, other);
        if (value->kind == ExprKind::Name && other->kind == ExprKind::Literal && other->op == "null"
            && CanRefineNative(value->symbol)) nonNullLocals_.insert(value->symbol);
    }

    // 대입 대상보다 먼저 실행되는 우변이 지역 ID를 바꿀 수 있으면 이전 null 검사는 사용하지 않는다.
    void InvalidateNativeWrites(const Expr& expression)
    {
        if (expression.kind == ExprKind::Assignment && expression.children[0]->kind == ExprKind::Name)
            nonNullLocals_.erase(Lookup(expression.children[0]->op));
        for (const auto& child : expression.children) InvalidateNativeWrites(*child);
    }

    TypeId NativeReceiverType(const Expr& expression) const
    {
        if (IsNative(expression.type)) return expression.type;
        if (expression.nativeNonNull && types_.IsNullable(expression.type)) return types_.Get(expression.type).element;
        Fail(expression.token, "a nullable host object cannot be dereferenced without a local null guard");
        return InvalidType;
    }

    Symbol& Define(const Token& name, TypeId type, bool constant, bool parameter = false)
    {
        if (hostSchema_ && hostSchema_->FindGlobal(name.text)) Fail(name, "a variable cannot hide a host global");
        if (functions_.count(name.text)) Fail(name, "a variable cannot hide a function");
        if (imports_.count(name.text)) Fail(name, "a variable cannot hide an imported module");
        if (types_.LookupNamed(name.text) != InvalidType) Fail(name, "a variable cannot have the same name as a declared type");
        if (scopes_.back().count(name.text)) Fail(name, "duplicate variable '" + name.text + "'");
        symbols_.push_back(Symbol(type, constant, name));
        Symbol& symbol = symbols_.back();
        symbol.fileGlobal = scopes_.size() == 1 && !parameter;
        symbol.backendName = scopes_.size() == 1 ? name.text : internalPrefix_ + std::to_string(symbols_.size());
        symbol.parameter = parameter;
        symbol.functionDepth = functionDepth_;
        scopes_.back().insert(std::make_pair(name.text, &symbol));
        return symbol;
    }

    // 벡터 성분 쓰기의 내부 저장 셀과 기본 초기값만 준비한다. 구조체 필드는 직접 저장한다.
    void FinishStorage()
    {
        for (Symbol& symbol : symbols_)
        {
            if (symbol.backendName != symbol.name.text) edits_.push_back(Edit(symbol.name.begin, symbol.name.end, symbol.backendName));
            if (symbol.parameter)
            {
                if (symbol.boxed) edits_.push_back(Edit(symbol.functionBody, symbol.functionBody, symbol.backendName + "="
                    + Intrinsic("cellnew", symbol.name.line) + "(" + symbol.backendName + ")" + ";"));
                continue;
            }
            if (!symbol.declaration) continue;
            Statement& declaration = *symbol.declaration;
            if (!symbol.defaultValue.empty())
            {
                const std::string value = symbol.boxed ? Intrinsic("cellnew", symbol.name.line) + "(" + symbol.defaultValue + ")" : symbol.defaultValue;
                edits_.push_back(Edit(symbol.name.end, symbol.name.end, "=" + value));
            }
            else if (symbol.boxed)
            {
                Expr& value = *declaration.expressions[0];
                value.box = true;
            }
        }
    }

    void ReplaceType(const TypeSyntax& type, const std::string& spelling)
    {
        std::string text = spelling;
        for (std::size_t i = type.first.begin; i < type.end; ++i)
            if (source_[i] == '\n') text += '\n';
        edits_.push_back(Edit(type.first.begin, type.end, text));
    }

    void ReplaceModifiers(const Statement& statement)
    {
        if (statement.token.begin == statement.declared.first.begin) return;
        std::string prefix;
        if (statement.isExported) prefix += "export ";
        if (statement.isPublic) prefix += "public ";
        if (statement.isConst) prefix += "const ";
        for (std::size_t i = statement.token.begin; i < statement.declared.first.begin; ++i)
            if (source_[i] == '\n') prefix += '\n';
        edits_.push_back(Edit(statement.token.begin, statement.declared.first.begin, prefix));
    }

    void Convert(Expr& source, TypeId target)
    {
        if (!types_.CanAssign(target, source.type))
            Fail(source.token, "cannot convert '" + types_.DisplayName(source.type) + "' to '" + types_.DisplayName(target) + "'");
        TypeId concrete = types_.IsNullable(target) ? types_.Get(target).element : target;
        if (source.type == types_.Int() && concrete == types_.Float())
        {
            source.widen = true;
            source.constantWiden = checkingConstant_;
        }
        if (IsStruct(target)) source.clone = true;
    }

    void Contextualize(Expr& expression, TypeId expected)
    {
        if (types_.IsNullable(expected)) expected = types_.Get(expected).element;
        if ((expression.kind == ExprKind::List && types_.Get(expected).kind == TypeKind::List)
            || (expression.kind == ExprKind::Map && types_.Get(expected).kind == TypeKind::Map)) expression.type = expected;
    }

    void Require(Expr& expression, TypeId type, const char* context)
    {
        if (expression.type != type) Fail(expression.token, std::string(context) + " requires '" + types_.DisplayName(type) + "'");
    }

    Symbol* Writable(Expr& expression)
    {
        if (expression.kind == ExprKind::Index) return nullptr;
        if (expression.hostProperty)
        {
            if (!expression.hostProperty->setter) Fail(expression.token, "host property is read-only");
            return nullptr;
        }
        if (expression.kind == ExprKind::Member) return Writable(*expression.children[0]);
        if (expression.kind != ExprKind::Name) Fail(expression.token, "assignment requires a named variable or its field");
        Symbol* symbol = expression.symbol;
        if (!symbol) Fail(expression.token, "assignment requires a variable");
        if (symbol->hostGlobal) Fail(expression.token, "host global handles cannot be rebound");
        if (symbol->constant) Fail(expression.token, "cannot modify a const value");
        return symbol;
    }

    TypeId BinaryType(const Token& token, const std::string& op, TypeId left, TypeId right)
    {
        if (left == right && types_.Get(left).kind == TypeKind::List && op == "+") return left;
        if (left == right && types_.Get(left).kind == TypeKind::Set && (op == "|" || op == "&" || op == "-")) return left;
        if (IsStruct(left) || IsStruct(right)) Fail(token, "structure operators and equality are not supported by this compiler stage");
        if (IsVectorValue(left) || IsVectorValue(right))
        {
            const bool nullComparison = (op == "==" || op == "!=") &&
                ((types_.IsNullable(left) && IsVectorValue(left) && right == types_.Null()) ||
                    (left == types_.Null() && types_.IsNullable(right) && IsVectorValue(right)));
            if (nullComparison) return types_.Bool();
            if (IsArithmeticVector(left) && left == right && (op == "+" || op == "-")) return left;
            if (IsArithmeticVector(left) && types_.IsNumeric(right) && (op == "*" || op == "/")) return left;
            if (types_.IsNumeric(left) && IsArithmeticVector(right) && op == "*") return right;
            Fail(token, "vector arithmetic requires matching Vector2/3/4 dimensions or multiplication/division by a numeric scalar; Quaternion arithmetic is not supported");
        }
        const TypeId numeric = types_.CommonNumericType(left, right);
        if (op == "+" || op == "-" || op == "*" || op == "/")
        {
            if (numeric != InvalidType) return numeric;
            Fail(token, "arithmetic requires non-null numeric operands");
        }
        if (op == "%" || op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>")
        {
            if (left == types_.Int() && right == types_.Int()) return types_.Int();
            Fail(token, "this operator requires int operands");
        }
        if (op == "&&" || op == "||")
        {
            if (left == types_.Bool() && right == types_.Bool()) return types_.Bool();
            Fail(token, "logical operators require bool operands");
        }
        if (op == "..")
        {
            if (left == types_.String() && right == types_.String()) return types_.String();
            Fail(token, "string concatenation requires string operands; use tostring explicitly");
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=")
        {
            if (numeric != InvalidType || (left == types_.String() && right == types_.String())) return types_.Bool();
            Fail(token, "ordered comparison requires compatible non-null operands");
        }
        if (op == "==" || op == "!=")
        {
            if (left != types_.Void() && right != types_.Void() && (left == right || numeric != InvalidType || types_.CanAssign(left, right) || types_.CanAssign(right, left)))
                return types_.Bool();
            Fail(token, "equality operands have incompatible types");
        }
        Fail(token, "unsupported operator");
        return InvalidType;
    }

    void CheckBuiltinSignature(Expr& expression, const BuiltinSignature& signature)
    {
        const std::size_t count = expression.children.size() - 1;
        if (count < signature.minimum || count > signature.maximum)
            Fail(expression.token, "builtin argument count does not match its declaration");
        for (std::size_t i = 0; i < count; ++i)
            Convert(*expression.children[i + 1], BuiltinSignatureType(types_, signature.parameters[i]));
        expression.type = BuiltinSignatureType(types_, signature.result);
    }

    void CheckMethod(Expr& expression)
    {
        Expr& member = *expression.children[0];
        Expr& receiver = *member.children[0];
        CheckExpr(receiver);
        const std::size_t count = expression.children.size() - 1;
        expression.receiverArgument = true;
        const TypeDesc& type = types_.Get(receiver.type);
        if (type.kind == TypeKind::Struct)
        {
            const StructField* field = types_.FindStructField(receiver.type, member.op, &member.fieldIndex);
            if (field && types_.Get(field->type).kind == TypeKind::Function)
            {
                const TypeDesc& signature = types_.Get(field->type);
                if (count != signature.parameters.size()) Fail(member.token, "function field argument count does not match its declaration");
                member.type = field->type;
                for (std::size_t i = 1; i < expression.children.size(); ++i)
                {
                    Contextualize(*expression.children[i], signature.parameters[i - 1]);
                    CheckExpr(*expression.children[i]);
                    Convert(*expression.children[i], signature.parameters[i - 1]);
                }
                expression.receiverArgument = false;
                expression.indirectCall = true;
                expression.type = signature.returnType;
                return;
            }
        }
        for (std::size_t i = 1; i < expression.children.size(); ++i)
        {
            if (type.kind == TypeKind::List && ((member.op == "append" && i == 1) || (member.op == "insert" && i == 2) || (member.op == "resize" && i == 2)))
                Contextualize(*expression.children[i], type.element);
            if (type.kind == TypeKind::Map && member.op == "get" && i == 2)
                Contextualize(*expression.children[i], type.value);
            CheckExpr(*expression.children[i]);
        }
        if (IsNativeValue(receiver.type))
        {
            const TypeId ownerType = NativeReceiverType(receiver);
            const HostBinding* binding = hostSchema_ ? hostSchema_->FindMethod(ownerType, member.op) : nullptr;
            if (!binding) Fail(member.token, "unknown host method '" + member.op + "'");
            if (count != binding->parameters.size()) Fail(member.token, "host method argument count does not match its declaration");
            for (std::size_t i = 0; i < count; ++i) Convert(*expression.children[i + 1], binding->parameters[i]);
            expression.callName = HostCall(binding->id);
            expression.type = binding->returnType;
            return;
        }
        if (member.op == "len" && (IsCollection(receiver.type) || receiver.type == types_.String()))
        {
            if (count) Fail(member.token, "len requires no arguments");
            expression.callName = IsArray(receiver.type) ? Intrinsic("alen", member.token.line) : "tosize";
            expression.type = types_.Int();
            return;
        }
        if (member.op == "resize" && type.kind == TypeKind::List)
        {
            if (count != 1 && count != 2) Fail(member.token, "resize requires a size and an optional fill value");
            Require(*expression.children[1], types_.Int(), "list size");
            if (count == 2) Convert(*expression.children[2], type.element);
            else
            {
                if (!Defaultable(type.element)) Fail(member.token, "this element type requires an explicit resize fill value");
                expression.extraArgument = DefaultValue(type.element);
            }
            expression.callName = internalPrefix_ + "resize";
            if (IsStruct(type.element))
            {
                structListResizes_.insert(type.element);
                expression.callName += "_" + std::to_string(type.element);
            }
            expression.type = types_.Void();
            resizeHelper_ = true;
            return;
        }
        if (type.kind == TypeKind::Array && (member.op == "resize" || member.op == "reserve" || member.op == "append"))
        {
            if (count != 1) Fail(member.token, "array " + member.op + " requires one argument");
            if (member.op == "append") Convert(*expression.children[1], type.element);
            else Require(*expression.children[1], types_.Int(), "array size");
            expression.callName = Intrinsic(member.op == "append" ? "a" + ArrayStorage(type.element) + "append" : "a" + member.op, member.token.line);
            expression.type = types_.Void();
            return;
        }
        if (member.op == "get" && type.kind == TypeKind::Map)
        {
            if (count != 2) Fail(member.token, "map.get requires a key and a fallback value");
            Require(*expression.children[1], type.key, "map key");
            Convert(*expression.children[2], type.value);
            expression.callName = internalPrefix_ + "get";
            expression.type = type.value;
            getHelper_ = true;
            return;
        }
        const BuiltinSignature* builtin = FindBuiltinSignature(type.name, member.op);
        if (builtin && (receiver.type == types_.String() || type.kind == TypeKind::BuiltinObject))
        {
            CheckBuiltinSignature(expression, *builtin);
            expression.receiverArgument = false;
            expression.callName = "." + member.op;
            return;
        }
        const bool list = type.kind == TypeKind::List;
        const bool map = type.kind == TypeKind::Map;
        const bool set = type.kind == TypeKind::Set;
        const bool array = type.kind == TypeKind::Array;
        if ((list || map) && member.op == "clone" && IsStruct(list ? type.element : type.value))
        {
            if (count) Fail(member.token, "clone requires no arguments");
            const TypeId element = list ? type.element : type.value;
            if (list) structListClones_.insert(element); else structMapClones_.insert(element);
            expression.callName = internalPrefix_ + (list ? "clonelist_" : "clonemap_") + std::to_string(element);
            expression.type = receiver.type;
            return;
        }
        if (map && member.op == "values" && IsStruct(type.value))
        {
            if (count) Fail(member.token, "values requires no arguments");
            structMapValues_.insert(type.value);
            expression.callName = internalPrefix_ + "mapvalues_" + std::to_string(type.value);
            expression.type = types_.ListOf(type.value);
            return;
        }
        TypeId result = InvalidType;
        if ((list || map || set || array) && (member.op == "clear" || member.op == "clone"))
        {
            if (count) Fail(member.token, "this collection method requires no arguments");
            result = member.op == "clone" ? receiver.type : types_.Void();
        }
        else if (set && member.op == "len")
        {
            if (count) Fail(member.token, "len requires no arguments");
            result = types_.Int();
        }
        else if ((list || map) && member.op == "reserve")
        {
            if (count != 1) Fail(member.token, "reserve requires one capacity argument");
            Require(*expression.children[1], types_.Int(), "collection capacity");
            result = types_.Void();
        }
        else if (list && (member.op == "append" || member.op == "insert"))
        {
            if (member.op == "append")
            {
                if (count != 1 && count != 2) Fail(member.token, "append requires a value and an optional index");
                Convert(*expression.children[1], type.element);
                if (count == 2) Require(*expression.children[2], types_.Int(), "list index");
            }
            else
            {
                if (count != 2) Fail(member.token, "insert requires an index and a value");
                Require(*expression.children[1], types_.Int(), "list index");
                Convert(*expression.children[2], type.element);
            }
            result = types_.Void();
        }
        else if (list && (member.op == "remove" || member.op == "remove_swap" || member.op == "pop"))
        {
            if (count != (member.op == "pop" ? 0u : 1u)) Fail(member.token, "list removal argument count does not match its declaration");
            if (count) Require(*expression.children[1], types_.Int(), "list index");
            result = type.element;
        }
        else if ((list || array) && (member.op == "find" || member.op == "contains" || (array && member.op == "fill")))
        {
            if (IsStruct(type.element)) Fail(member.token, "struct equality is not supported by collection search");
            if (count != 1) Fail(member.token, "this collection method requires one value");
            Convert(*expression.children[1], type.element);
            result = member.op == "find" ? types_.Int() : member.op == "contains" ? types_.Bool() : types_.Void();
        }
        else if ((map || set) && (member.op == "contains" || member.op == "remove" || (set && member.op == "add")))
        {
            if (count != 1) Fail(member.token, "this collection method requires one key");
            Convert(*expression.children[1], map ? type.key : type.element);
            result = types_.Bool();
        }
        else if ((map || set) && (member.op == "values" || (map && member.op == "keys")))
        {
            if (count) Fail(member.token, "this collection method requires no arguments");
            result = types_.ListOf(set ? type.element : member.op == "keys" ? type.key : type.value);
        }
        else if ((list || map) && member.op == "sort")
        {
            if (count != 1) Fail(member.token, "sort requires a comparator");
            const TypeId element = list ? type.element : type.value;
            if (IsStruct(element)) Fail(member.token, "native sort of struct values requires a value-copy callback adapter");
            Require(*expression.children[1], types_.FunctionOf(types_.Bool(), std::vector<TypeId>(2, element)), "sort comparator");
            result = types_.Void();
        }
        if (result != InvalidType)
        {
            expression.receiverArgument = false;
            expression.callName = "." + member.op;
            expression.type = result;
            return;
        }
        Fail(member.token, "unknown or unsupported typed method '" + member.op + "'");
    }

    void CheckCall(Expr& expression)
    {
        Expr& callee = *expression.children[0];
        // 뒤의 인자가 nullable 지역을 덮어쓰면 저장된 수신자 증명을 재사용하지 않는다.
        for (std::size_t i = 1; i < expression.children.size(); ++i) InvalidateNativeWrites(*expression.children[i]);
        if ((callee.kind == ExprKind::Name && Lookup(callee.op)) ||
            (callee.kind != ExprKind::Name && callee.kind != ExprKind::Member))
        {
            CheckExpr(callee);
            const TypeDesc& signature = types_.Get(callee.type);
            if (signature.kind != TypeKind::Function) Fail(callee.token, "this value is not callable without a non-null function signature");
            if (expression.children.size() - 1 != signature.parameters.size()) Fail(callee.token, "function argument count does not match its declaration");
            for (std::size_t i = 1; i < expression.children.size(); ++i)
            {
                Contextualize(*expression.children[i], signature.parameters[i - 1]);
                CheckExpr(*expression.children[i]);
                Convert(*expression.children[i], signature.parameters[i - 1]);
            }
            expression.indirectCall = true;
            expression.type = signature.returnType;
            return;
        }
        if (callee.kind == ExprKind::Member && !(callee.children[0]->kind == ExprKind::Name
            && (callee.children[0]->op == "math" || callee.children[0]->op == "system" || callee.children[0]->op == "coroutine")))
        {
            CheckMethod(expression);
            return;
        }
        std::string name;
        if (callee.kind == ExprKind::Name) name = callee.op;
        else if (callee.kind == ExprKind::Member && callee.children[0]->kind == ExprKind::Name)
            name = callee.children[0]->op + "." + callee.op;
        else Fail(callee.token, "indirect calls are not supported by this compiler stage");
        expression.callName = name;
        if (Lookup(name)) Fail(callee.token, "this value is not callable");
        const auto function = functions_.find(name);
        const TypeId namedType = types_.LookupNamed(name);
        for (std::size_t i = 1; i < expression.children.size(); ++i)
        {
            if (function != functions_.end() && i <= function->second.parameterTypes.size())
                Contextualize(*expression.children[i], function->second.parameterTypes[i - 1]);
            if (callee.constructorType != InvalidType && types_.Get(callee.constructorType).kind == TypeKind::Set && i == 1)
                Contextualize(*expression.children[i], types_.ListOf(types_.Get(callee.constructorType).element));
            if (IsStruct(namedType) && i <= types_.Get(namedType).fields.size())
                Contextualize(*expression.children[i], types_.Get(namedType).fields[i - 1].type);
            if (name == "coroutine.resume" && i >= 2 && types_.Get(expression.children[1]->type).kind == TypeKind::Coroutine)
            {
                const TypeDesc& signature = types_.Get(types_.Get(expression.children[1]->type).element);
                if (i - 2 < signature.parameters.size()) Contextualize(*expression.children[i], signature.parameters[i - 2]);
            }
            CheckExpr(*expression.children[i]);
            if (expression.children[i]->type == types_.Void()) Fail(expression.children[i]->token, "a void call cannot be used as an argument");
        }
        const std::size_t count = expression.children.size() - 1;
        if (name == "coroutine.create" || name == "coroutine.resume" || name == "coroutine.status" || name == "coroutine.close")
        {
            if (!imports_.count("coroutine")) Fail(callee.token, "coroutine must be imported before use");
            if (name == "coroutine.create")
            {
                if (count != 1) Fail(callee.token, "coroutine.create requires one function");
                expression.type = types_.CoroutineOf(expression.children[1]->type);
                if (expression.type == InvalidType) Fail(callee.token, "coroutine.create requires a function returning void");
                return;
            }
            if (!count || types_.Get(expression.children[1]->type).kind != TypeKind::Coroutine)
                Fail(callee.token, "a non-null typed coroutine is required");
            const TypeDesc& signature = types_.Get(types_.Get(expression.children[1]->type).element);
            if (name == "coroutine.resume")
            {
                if (count - 1 != signature.parameters.size()) Fail(callee.token, "coroutine.resume arguments must match the entry signature");
                for (std::size_t i = 0; i < signature.parameters.size(); ++i) Convert(*expression.children[i + 2], signature.parameters[i]);
                expression.type = types_.Void();
            }
            else
            {
                if (count != 1) Fail(callee.token, "this coroutine operation requires one handle");
                expression.type = name == "coroutine.status" ? types_.String() : types_.Void();
            }
            return;
        }
        if (callee.constructorType != InvalidType)
        {
            if (types_.Get(callee.constructorType).kind == TypeKind::Set)
            {
                if (count > 1) Fail(callee.token, "set constructor requires no arguments or one list");
                if (count) Require(*expression.children[1], types_.ListOf(types_.Get(callee.constructorType).element), "set initializer");
                else expression.extraArgument = "[]";
                expression.callName = "system.set";
                expression.type = callee.constructorType;
                if (!imports_.count("system")) Fail(callee.token, "system must be imported before constructing a set");
                return;
            }
            if (count > 2) Fail(callee.token, "array constructor requires a size and optional fill value, or no arguments");
            const TypeId element = types_.Get(callee.constructorType).element;
            if (count) Require(*expression.children[1], types_.Int(), "array size");
            if (count == 2) Convert(*expression.children[2], element);
            else expression.extraArgument = (count ? "" : "0,") + DefaultValue(element);
            expression.callName = Intrinsic("a" + ArrayStorage(element) + "new", callee.token.line);
            expression.type = callee.constructorType;
            return;
        }
        const TypeId structure = types_.LookupNamed(name);
        if (IsNative(structure)) Fail(callee.token, "host objects must be obtained from registered bindings, not constructed by scripts");
        if (IsVector(structure))
        {
            const std::size_t components = static_cast<std::size_t>(VectorSize(structure));
            if (count != 0 && count != components) Fail(callee.token, "vector constructor requires no arguments or exactly its component count");
            for (std::size_t i = 1; i < expression.children.size(); ++i) Convert(*expression.children[i], types_.Float());
            expression.callName = VectorConstructor(structure, callee.token.line);
            if (count == 0)
            {
                for (std::size_t i = 0; i < components; ++i)
                {
                    if (i) expression.extraArgument += ",";
                    expression.extraArgument += structure == types_.Quaternion() && i == 0 ? "1.0" : "0.0";
                }
            }
            expression.type = structure;
            return;
        }
        if (IsStruct(structure))
        {
            const TypeDesc& description = types_.Get(structure);
            if (count == 0)
            {
                if (!Defaultable(structure)) Fail(callee.token, "this structure requires a full positional initializer");
                expression.callName = Helper("default", structure);
            }
            else
            {
                if (count != description.fields.size()) Fail(callee.token, "structure constructor argument count does not match its fields");
                for (std::size_t i = 0; i < count; ++i) Convert(*expression.children[i + 1], description.fields[i].type);
                expression.callName = Helper("make", structure);
            }
            expression.type = structure;
            return;
        }
        if (function != functions_.end())
        {
            if (count != function->second.parameterTypes.size()) Fail(callee.token, "function argument count does not match its declaration");
            for (std::size_t i = 0; i < count; ++i)
            {
                const TypeId parameter = function->second.parameterTypes[i];
                Convert(*expression.children[i + 1], parameter);
            }
            RecordScriptCall(callee.token, name);
            expression.functionIndex = irFunctions_.at(name);
            expression.type = function->second.returnType;
            return;
        }
        if (name == "sleep")
        {
            if (count != 1) Fail(callee.token, "sleep requires exactly one argument");
            Require(*expression.children[1], types_.Int(), "sleep duration");
            expression.type = types_.Void();
            return;
        }
        if (name == "print")
        {
            if (count < 1 || count > 2) Fail(callee.token, "print requires one or two arguments");
            for (std::size_t i = 1; i < expression.children.size(); ++i)
                if (!IsScalarValue(expression.children[i]->type)) Fail(expression.children[i]->token, "print supports scalar values only");
            expression.type = types_.Void();
            return;
        }
        if (name == "tostring" || name == "toint" || name == "tofloat" || name == "int" || name == "float")
        {
            if (count != 1) Fail(callee.token, "conversion requires exactly one argument");
            const TypeId argument = expression.children[1]->type;
            if (name == "tostring")
            {
                if (!IsScalarValue(argument)) Fail(callee.token, "tostring supports scalar values only");
                expression.type = types_.String();
            }
            else
            {
                if (!types_.IsNumeric(argument) && argument != types_.Bool() && argument != types_.String())
                    Fail(callee.token, "numeric conversion requires a non-null number, bool or string");
                expression.type = (name == "int" || name == "toint") ? types_.Int() : types_.Float();
                if (name == "int" || name == "float") expression.callName = "to" + name;
            }
            return;
        }
        if (name == "system.set")
        {
            if (!imports_.count("system")) Fail(callee.token, "system must be imported before use");
            if (count != 1 || types_.Get(expression.children[1]->type).kind != TypeKind::List)
                Fail(callee.token, "system.set requires one typed list");
            const TypeId element = types_.Get(expression.children[1]->type).element;
            if (element != types_.Int() && element != types_.Float() && element != types_.Bool() && element != types_.String()) Fail(callee.token, "set elements must be non-null scalar types");
            expression.type = types_.SetOf(element);
            return;
        }
        const std::size_t dot = name.find('.');
        if (dot != std::string::npos)
        {
            const std::string owner = name.substr(0, dot);
            const BuiltinSignature* builtin = FindBuiltinSignature(owner, name.substr(dot + 1));
            if (builtin && (owner == "math" || owner == "system"))
            {
                if (!imports_.count(owner)) Fail(callee.token, owner + " must be imported before use");
                CheckBuiltinSignature(expression, *builtin);
                if (name == "system.async_create") expression.callName = "system.aysnc_create";
                return;
            }
        }
        Fail(callee.token, "unknown or untyped function '" + name + "'");
    }

    void CheckInteger(Expr& expression, bool allowMinMagnitude)
    {
        const std::string& text = expression.token.text;
        const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
        const std::uint64_t maximum = hex ? (std::numeric_limits<std::uint64_t>::max)()
            : static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) + (allowMinMagnitude ? 1u : 0u);
        const unsigned base = hex ? 16u : 10u;
        std::uint64_t magnitude = 0;
        for (std::size_t i = hex ? 2 : 0; i < text.size(); ++i)
        {
            const char c = text[i];
            const unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c - 'A' + 10;
            if (magnitude > (maximum - digit) / base) Fail(expression.token, "integer literal is outside the 64-bit range");
            magnitude = magnitude * base + digit;
        }
        // 16진수와 INT_MIN의 양의 크기는 기존 언어의 64비트 비트 패턴을 유지한다.
        std::memcpy(&expression.integerValue, &magnitude, sizeof(magnitude));
    }

    void CheckExpr(Expr& expression, bool allowMinMagnitude = false)
    {
        expression.constantInitializer = checkingConstant_;
        switch (expression.kind)
        {
        case ExprKind::Literal:
            if (expression.token.kind == TokenKind::Integer) CheckInteger(expression, allowMinMagnitude);
            if (expression.token.kind == TokenKind::Float && !NeoScript::ParseDecimalLiteral(expression.floatValue, expression.token.text.c_str()))
                Fail(expression.token, "invalid floating-point literal");
            expression.type = expression.token.kind == TokenKind::Integer ? types_.Int() : expression.token.kind == TokenKind::Float ? types_.Float()
                : expression.token.kind == TokenKind::String ? types_.String() : expression.op == "null" ? types_.Null() : types_.Bool();
            expression.constant = true;
            return;
        case ExprKind::Name:
        {
            Symbol* symbol = Lookup(expression.op);
            if (!symbol)
            {
                const auto function = functions_.find(expression.op);
                if (function == functions_.end()) Fail(expression.token, "unknown variable '" + expression.op + "'");
                expression.type = types_.FunctionOf(function->second.returnType, function->second.parameterTypes);
                expression.callName = expression.op;
                RecordScriptCall(expression.token, expression.op);
                return;
            }
            expression.type = symbol->type;
            expression.constant = symbol->constant;
            expression.symbol = symbol;
            expression.nativeNonNull = CanRefineNative(symbol) && nonNullLocals_.count(symbol) != 0;
            if (expression.nativeNonNull && !expression.writeTarget) expression.type = types_.Get(symbol->type).element;
            if (!symbol->fileGlobal && !symbol->hostGlobal)
                for (std::size_t i = 0; i < activeLambdas_.size(); ++i)
                {
                    Expr* lambda = activeLambdas_[i];
                    if (symbol->functionDepth < static_cast<int>(lambda->fieldIndex) &&
                        std::find(lambda->captures.begin(), lambda->captures.end(), symbol) == lambda->captures.end())
                        lambda->captures.push_back(symbol);
                }
            if (symbol->fileGlobal && !currentFunction_.empty())
                requiredGlobalEnd_[currentFunction_] = std::max(requiredGlobalEnd_[currentFunction_], symbol->declaration->end);
            return;
        }
        case ExprKind::Lambda:
        {
            if (checkingConstant_) Fail(expression.token, "a function value is not a constant initializer");
            Statement& function = *expression.lambda;
            std::vector<TypeId> parameters;
            for (const Parameter& parameter : function.parameters) parameters.push_back(parameter.type.type);
            expression.type = types_.FunctionOf(function.declared.type, parameters);
            if (expression.type == InvalidType) Fail(expression.token, "invalid anonymous function signature");
            expression.callName = internalPrefix_ + "lambda_" + std::to_string(lambdas_.size());
            lambdas_.push_back(&expression);
            RecordScriptCall(expression.token, expression.callName);
            const TypeId savedReturn = returnType_;
            const std::string savedFunction = currentFunction_;
            const int savedLoop = loopDepth_;
            const std::set<Symbol*> savedNonNull = nonNullLocals_;
            currentFunction_ = expression.callName;
            returnType_ = function.declared.type;
            loopDepth_ = 0;
            nonNullLocals_.clear();
            ++functionDepth_;
            expression.fieldIndex = static_cast<std::size_t>(functionDepth_);
            activeLambdas_.push_back(&expression);
            scopes_.push_back(std::map<std::string, Symbol*>());
            for (const Parameter& parameter : function.parameters)
            {
                Symbol& symbol = Define(parameter.name, parameter.type.type, false, true);
                symbol.functionBody = function.children[0]->token.end;
                expression.lambdaParameters.push_back(&symbol);
            }
            const int flow = CheckStatement(*function.children[0]);
            if (returnType_ != types_.Void() && (flow & FallsThrough)) Fail(expression.token, "not all anonymous function paths return a value");
            scopes_.pop_back();
            activeLambdas_.pop_back();
            --functionDepth_;
            returnType_ = savedReturn;
            currentFunction_ = savedFunction;
            loopDepth_ = savedLoop;
            nonNullLocals_ = savedNonNull;
            return;
        }
        case ExprKind::Unary:
        {
            Expr& value = *expression.children[0];
            CheckExpr(value, expression.op == "-" && value.kind == ExprKind::Literal && value.begin == value.token.begin);
            if (checkingConstant_ && expression.op == "!") Fail(expression.token, "logical operators in const initializers are not supported by this compiler stage");
            if (expression.op == "!") { Require(value, types_.Bool(), "logical negation"); expression.type = types_.Bool(); }
            else if (expression.op == "~") { Require(value, types_.Int(), "bitwise negation"); expression.type = types_.Int(); }
            else
            {
                const bool vectorSign = IsArithmeticVector(value.type) && (expression.op == "+" || expression.op == "-");
                if (!types_.IsNumeric(value.type) && !vectorSign) Fail(expression.token, "unary arithmetic requires a non-null number or a Vector2/3/4 sign operator");
                expression.type = value.type;
                if (expression.op == "++" || expression.op == "--" || expression.op == "post++" || expression.op == "post--")
                {
                    Writable(value);
                    HostWriteHelper(expression, value, expression.op, value.type);
                }
            }
            expression.constant = value.constant && expression.op != "++" && expression.op != "--" && expression.op != "post++" && expression.op != "post--";
            return;
        }
        case ExprKind::Binary:
            CheckExpr(*expression.children[0]); CheckExpr(*expression.children[1]);
            expression.type = BinaryType(expression.token, expression.op, expression.children[0]->type, expression.children[1]->type);
            if (types_.Get(expression.type).kind == TypeKind::List && expression.op == "+")
            {
                concatHelper_ = true;
                expression.callName = internalPrefix_ + "concat";
                const TypeId element = types_.Get(expression.type).element;
                if (IsStruct(element))
                {
                    structListConcats_.insert(element);
                    structListClones_.insert(element);
                    expression.callName += "_" + std::to_string(element);
                }
            }
            else if (types_.Get(expression.type).kind == TypeKind::Set)
            {
                if (expression.op == "|") { unionHelper_ = true; expression.callName = internalPrefix_ + "union"; }
                else if (expression.op == "&") { intersectionHelper_ = true; expression.callName = internalPrefix_ + "intersection"; }
                else { differenceHelper_ = true; expression.callName = internalPrefix_ + "difference"; }
            }
            else if (expression.op == "*" && types_.IsNumeric(expression.children[0]->type) && IsArithmeticVector(expression.children[1]->type))
            {
                Convert(*expression.children[0], types_.Float());
                expression.callName = Intrinsic("v" + std::to_string(VectorSize(expression.type)) + "rmul", expression.token.line);
            }
            else if (IsArithmeticVector(expression.type))
            {
                if (types_.IsNumeric(expression.children[1]->type)) Convert(*expression.children[1], types_.Float());
                const char* operation = expression.op == "+" ? "add" : expression.op == "-" ? "sub" : expression.op == "*" ? "mul" : "div";
                expression.callName = Intrinsic("v" + std::to_string(VectorSize(expression.type)) + operation, expression.token.line);
            }
            if (checkingConstant_ && (expression.type == types_.Bool() || expression.type == types_.String()))
                Fail(expression.token, "comparison, logical and concatenation operators in const initializers are not supported by this compiler stage");
            expression.constant = expression.children[0]->constant && expression.children[1]->constant;
            return;
        case ExprKind::Assignment:
        {
            Expr& target = *expression.children[0];
            Expr& source = *expression.children[1];
            InvalidateNativeWrites(source);
            target.writeTarget = expression.op == "=";
            CheckExpr(target); Writable(target);
            TypeId writeType = target.type;
            if (expression.op == "=" && target.kind == ExprKind::Index && types_.Get(target.children[0]->type).kind == TypeKind::Map)
                writeType = types_.Get(target.children[0]->type).value;
            Contextualize(source, writeType);
            CheckExpr(source);
            if (expression.op == "=")
            {
                Convert(source, writeType);
            }
            else
            {
                const TypeId computed = BinaryType(expression.token, expression.op.substr(0, expression.op.size() - 1), target.type, source.type);
                if (!types_.CanAssign(target.type, computed)) Fail(expression.token, "compound assignment would narrow its result");
            }
            expression.type = writeType;
            if (target.kind == ExprKind::Name) nonNullLocals_.erase(target.symbol);
            HostWriteHelper(expression, target, expression.op, source.type);
            return;
        }
        case ExprKind::Call:
            CheckCall(expression);
            return;
        case ExprKind::Member:
        {
            Expr& owner = *expression.children[0];
            CheckExpr(owner);
            if (IsNativeValue(owner.type))
            {
                const TypeId ownerType = NativeReceiverType(owner);
                const HostProperty* property = hostSchema_ ? hostSchema_->FindProperty(ownerType, expression.op) : nullptr;
                if (!property) Fail(expression.token, "unknown host property '" + expression.op + "'");
                if (!expression.writeTarget && !property->getter) Fail(expression.token, "host property is write-only");
                expression.hostProperty = property;
                expression.type = property->type;
                return;
            }
            if (IsVector(owner.type))
            {
                const std::string components = owner.type == types_.Quaternion() ? "wxyz" : "xyzw";
                const std::size_t component = expression.op.size() == 1 ? components.find(expression.op[0]) : std::string::npos;
                if (component >= static_cast<std::size_t>(VectorSize(owner.type))) Fail(expression.token, "unknown vector component '" + expression.op + "'");
                expression.vectorComponent = true;
                expression.fieldIndex = component;
                expression.type = types_.Float();
                return;
            }
            if (!IsStruct(owner.type)) Fail(expression.token, "field access requires a non-null structure");
            const StructField* field = types_.FindStructField(owner.type, expression.op, &expression.fieldIndex);
            if (!field) Fail(expression.token, "unknown structure field '" + expression.op + "'");
            expression.type = field->type;
            return;
        }
        case ExprKind::Index:
        {
            Expr& owner = *expression.children[0];
            Expr& key = *expression.children[1];
            InvalidateNativeWrites(key);
            CheckExpr(owner); CheckExpr(key);
            const TypeDesc& type = types_.Get(owner.type);
            if (type.kind == TypeKind::List || type.kind == TypeKind::Array)
            {
                Require(key, types_.Int(), type.kind == TypeKind::Array ? "array index" : "list index");
                expression.type = type.element;
            }
            else if (type.kind == TypeKind::Map)
            {
                Require(key, type.key, "map key");
                expression.type = types_.NullableOf(type.value);
            }
            else Fail(expression.token, "indexing requires a typed array, list, or map");
            return;
        }
        case ExprKind::List:
        {
            if (types_.Get(expression.type).kind != TypeKind::List) Fail(expression.token, "a list literal requires an explicit list type context");
            const TypeId element = types_.Get(expression.type).element;
            for (const auto& value : expression.children) { Contextualize(*value, element); CheckExpr(*value); Convert(*value, element); }
            return;
        }
        case ExprKind::Map:
        {
            if (types_.Get(expression.type).kind != TypeKind::Map) Fail(expression.token, "a map literal requires an explicit map type context");
            const TypeDesc& type = types_.Get(expression.type);
            for (std::size_t i = 0; i < expression.children.size(); i += 2)
            {
                CheckExpr(*expression.children[i]); Require(*expression.children[i], type.key, "map key");
                Contextualize(*expression.children[i + 1], type.value);
                CheckExpr(*expression.children[i + 1]); Convert(*expression.children[i + 1], type.value);
            }
            return;
        }
        }
    }

    int ScopedStatement(Statement& statement)
    {
        scopes_.push_back(std::map<std::string, Symbol*>());
        const int result = CheckStatement(statement);
        scopes_.pop_back();
        return result;
    }

    int CheckStatement(Statement& statement)
    {
        if (statement.kind != StatementKind::Return)
            for (const auto& expression : statement.expressions) roots_.push_back(expression.get());
        switch (statement.kind)
        {
        case StatementKind::Empty:
            return FallsThrough;
        case StatementKind::Block:
        {
            scopes_.push_back(std::map<std::string, Symbol*>());
            int flow = FallsThrough;
            for (const auto& child : statement.children)
            {
                const int next = CheckStatement(*child);
                if (flow & FallsThrough) flow = (flow & ~FallsThrough) | next;
            }
            scopes_.pop_back();
            return flow;
        }
        case StatementKind::Declaration:
        {
            if (!types_.IsStorable(statement.declared.type)) Fail(statement.declared.first, "a variable cannot have this type");
            if (statement.isConst && (IsStruct(statement.declared.type) || IsVector(statement.declared.type))) Fail(statement.name, "const structures and vectors are not supported by this compiler stage");
            if (statement.isExported && statement.isConst) Fail(statement.token, "export const is not supported; use public const");
            if (statement.isConst && statement.expressions.empty()) Fail(statement.name, "const requires an initializer");
            const bool savedConstant = checkingConstant_;
            checkingConstant_ = statement.isConst;
            std::string initial;
            if (!statement.expressions.empty())
            {
                Expr& value = *statement.expressions[0];
                Contextualize(value, statement.declared.type);
                CheckExpr(value);
                if (statement.isConst && !value.constant) Fail(value.token, "const initializer must be a constant expression");
                Convert(value, statement.declared.type);
            }
            else
            {
                if (!Defaultable(statement.declared.type)) Fail(statement.name, "this non-null type requires an initializer");
                initial = DefaultValue(statement.declared.type);
            }
            checkingConstant_ = savedConstant;
            Symbol& symbol = Define(statement.name, statement.declared.type, statement.isConst);
            symbol.declaration = &statement;
            symbol.defaultValue = initial;
            ReplaceType(statement.declared, statement.isConst ? "" : "var");
            ReplaceModifiers(statement);
            return FallsThrough;
        }
        case StatementKind::Function:
        {
            ReplaceType(statement.declared, "fun");
            ReplaceModifiers(statement);
            const TypeId previousReturn = returnType_;
            const std::string previousFunction = currentFunction_;
            const std::set<Symbol*> previousNonNull = nonNullLocals_;
            nonNullLocals_.clear();
            returnType_ = statement.declared.type;
            currentFunction_ = statement.name.text;
            ++functionDepth_;
            scopes_.push_back(std::map<std::string, Symbol*>());
            for (const Parameter& parameter : statement.parameters)
            {
                Symbol& symbol = Define(parameter.name, parameter.type.type, false, true);
                symbol.functionBody = statement.children[0]->token.end;
                ReplaceType(parameter.type, "var");
            }
            const int flow = CheckStatement(*statement.children[0]);
            scopes_.pop_back();
            --functionDepth_;
            if (returnType_ != types_.Void() && (flow & FallsThrough)) Fail(statement.name, "not all paths return a value");
            returnType_ = previousReturn;
            currentFunction_ = previousFunction;
            nonNullLocals_ = previousNonNull;
            return FallsThrough;
        }
        case StatementKind::Expression:
            CheckExpr(*statement.expressions[0]);
            return FallsThrough;
        case StatementKind::If:
        {
            CheckExpr(*statement.expressions[0]); Require(*statement.expressions[0], types_.Bool(), "if condition");
            statement.expressions[0]->branchCondition = true;
            const std::set<Symbol*> before = nonNullLocals_;
            ApplyNativeGuard(*statement.expressions[0], true);
            const int yes = ScopedStatement(*statement.children[0]);
            const std::set<Symbol*> yesFacts = nonNullLocals_;
            nonNullLocals_ = before;
            ApplyNativeGuard(*statement.expressions[0], false);
            const int no = statement.children.size() == 2 ? ScopedStatement(*statement.children[1]) : FallsThrough;
            if (yes & FallsThrough)
            {
                if (!(no & FallsThrough)) nonNullLocals_ = yesFacts;
                else
                    for (auto it = nonNullLocals_.begin(); it != nonNullLocals_.end();)
                        if (!yesFacts.count(*it)) it = nonNullLocals_.erase(it); else ++it;
            }
            return yes | no;
        }
        case StatementKind::While:
        {
            // 반복문의 다음 회차에는 본문에서 바뀐 ID가 들어올 수 있다. 조건에서 다시 확인한다.
            nonNullLocals_.clear();
            CheckExpr(*statement.expressions[0]); Require(*statement.expressions[0], types_.Bool(), "while condition");
            statement.expressions[0]->branchCondition = true;
            ApplyNativeGuard(*statement.expressions[0], true);
            ++loopDepth_;
            const int body = ScopedStatement(*statement.children[0]);
            --loopDepth_;
            nonNullLocals_.clear();
            return FallsThrough | (body & Returns);
        }
        case StatementKind::For:
        {
            nonNullLocals_.clear();
            if (statement.declared.type != types_.Int()) Fail(statement.declared.first, "a range loop requires an int variable");
            for (const auto& expression : statement.expressions)
            {
                CheckExpr(*expression); Require(*expression, types_.Int(), "range bound or step");
            }
            scopes_.push_back(std::map<std::string, Symbol*>());
            Define(statement.name, types_.Int(), false);
            ReplaceType(statement.declared, "var");
            ++loopDepth_;
            const int body = CheckStatement(*statement.children[0]);
            --loopDepth_;
            scopes_.pop_back();
            nonNullLocals_.clear();
            return FallsThrough | (body & Returns);
        }
        case StatementKind::Return:
            if (returnType_ == InvalidType) Fail(statement.token, "return is only allowed in a function");
            if (statement.expressions.empty())
            {
                if (returnType_ != types_.Void()) Fail(statement.token, "this function must return a value");
            }
            else
            {
                if (returnType_ == types_.Void()) Fail(statement.token, "a void function cannot return a value");
                Contextualize(*statement.expressions[0], returnType_);
                CheckExpr(*statement.expressions[0]); Convert(*statement.expressions[0], returnType_);
            }
            if (activeLambdas_.empty()) returns_.push_back(std::make_pair(&statement, irFunctions_.at(currentFunction_)));
            else if (!statement.expressions.empty()) roots_.push_back(statement.expressions[0].get());
            return Returns;
        case StatementKind::Foreach:
        {
            CheckExpr(*statement.expressions[0]);
            nonNullLocals_.clear();
            const TypeId source = statement.expressions[0]->type;
            const TypeDesc& collection = types_.Get(source);
            std::vector<TypeId> expected;
            if (collection.kind == TypeKind::Map)
            {
                if (statement.parameters.size() != 2) Fail(statement.token, "map foreach requires a key and value; use values() to iterate values alone");
                expected.push_back(collection.key); expected.push_back(collection.value);
            }
            else
            {
                if (statement.parameters.size() != 1) Fail(statement.token, "this foreach requires one iteration variable");
                if (source == types_.String()) expected.push_back(types_.String());
                else if (collection.kind == TypeKind::List || collection.kind == TypeKind::Array || collection.kind == TypeKind::Set) expected.push_back(collection.element);
                else Fail(statement.token, "foreach requires a non-null string or collection");
            }
            scopes_.push_back(std::map<std::string, Symbol*>());
            std::vector<Symbol*> variables;
            for (std::size_t i = 0; i < expected.size(); ++i)
            {
                const Parameter& parameter = statement.parameters[i];
                if (parameter.type.type != expected[i]) Fail(parameter.name, "foreach variable type must exactly match the element type");
                variables.push_back(&Define(parameter.name, expected[i], false));
                ReplaceType(parameter.type, i == 0 ? "var" : "");
            }
            ++loopDepth_;
            const int body = CheckStatement(*statement.children[0]);
            --loopDepth_;
            std::string initialization;
            for (Symbol* symbol : variables)
            {
                std::string value;
                if (source == types_.String()) value = "tostring(" + symbol->backendName + ")";
                else if (IsStruct(symbol->type)) value = Helper("clone", symbol->type) + "(" + symbol->backendName + ")";
                if (symbol->boxed) value = Intrinsic("cellnew", symbol->name.line) + "(" + symbol->backendName + ")";
                if (!value.empty()) initialization += symbol->backendName + "=" + value + ";";
            }
            if (!initialization.empty())
            {
                edits_.push_back(Edit(statement.children[0]->token.begin, statement.children[0]->token.begin, "{" + initialization));
                edits_.push_back(Edit(statement.children[0]->end, statement.children[0]->end, "}"));
            }
            scopes_.pop_back();
            nonNullLocals_.clear();
            return FallsThrough | (body & Returns);
        }
        case StatementKind::Yield:
            if (returnType_ == InvalidType) Fail(statement.token, "yield is only allowed in a function");
            nonNullLocals_.clear();
            return FallsThrough;
        case StatementKind::Break:
            if (!loopDepth_) Fail(statement.token, "break is only allowed in a loop");
            return Breaks;
        case StatementKind::Continue:
            if (!loopDepth_) Fail(statement.token, "continue is only allowed in a loop");
            return Continues;
        case StatementKind::Import:
            if (Lookup(statement.name.text) || functions_.count(statement.name.text)) Fail(statement.name, "module name conflicts with an existing declaration");
            imports_.insert(statement.name.text);
            return FallsThrough;
        case StatementKind::Struct:
        {
            std::string lines;
            for (std::size_t i = statement.token.begin; i < statement.end; ++i) if (source_[i] == '\n') lines += '\n';
            edits_.push_back(Edit(statement.token.begin, statement.end, lines));
            return FallsThrough;
        }
        }
        return FallsThrough;
    }

    // 식 전체를 타입이 붙은 트리에서 생성하여 멤버 접근·변환·참조의 부분 치환이 겹치지 않게 한다.
    std::string Lines(std::size_t begin, std::size_t end) const
    {
        std::string lines;
        for (std::size_t i = begin; i < end; ++i) if (source_[i] == '\n') lines += '\n';
        return lines;
    }

    std::string Intrinsic(const std::string& operation, int line) const
    {
        return result_.backendIntrinsicPrefix + operation + "_L" + std::to_string(line);
    }

    int BeginLine(const Expr& expression) const
    {
        return expression.token.line - static_cast<int>(std::count(source_.begin() + expression.begin, source_.begin() + expression.token.begin, '\n'));
    }

    IRValueType IRType(TypeId type) const
    {
        return type == types_.Int() ? IRValueType::Int : type == types_.Float() ? IRValueType::Float
            : type == types_.Bool() ? IRValueType::Bool : type == types_.String() ? IRValueType::String
            : type == types_.Void() ? IRValueType::Void
            : type == types_.Null() || types_.IsStorable(type) ? IRValueType::Value : IRValueType::Invalid;
    }

    bool ValueStorage(const Expr& expression, IRStorageTarget* storage = nullptr) const
    {
        const Expr* root = &expression;
        std::vector<std::size_t> fields;
        while (root->kind == ExprKind::Member)
        {
            if (root->hostProperty || root->vectorComponent || !IsStruct(root->children[0]->type)) return false;
            fields.push_back(root->fieldIndex);
            root = root->children[0].get();
        }
        if (root->kind != ExprKind::Name || !root->symbol || root->symbol->constant || root->symbol->hostGlobal)
            return false;
        const Symbol* symbol = root->symbol;
        const bool cell = symbol->boxed;
        if (storage)
        {
            storage->kind = cell ? IRStorageKind::Cell : IRStorageKind::Variable;
            storage->backendName = symbol->backendName;
            const auto inserted = irSymbols_.insert(std::make_pair(symbol, irSymbols_.size() + 1));
            storage->symbolId = inserted.first->second;
            storage->fields.assign(fields.rbegin(), fields.rend());
        }
        return true;
    }

    bool ScalarStorage(const Expr& expression, IRStorageTarget* storage = nullptr) const
    {
        const IRValueType type = IRType(expression.type);
        return (type == IRValueType::Int || type == IRValueType::Float || type == IRValueType::Bool
            || type == IRValueType::String) && ValueStorage(expression, storage);
    }

    bool ScalarMutationIROp(const Expr& expression, IROp& operation) const
    {
        if (expression.kind != ExprKind::Assignment && expression.kind != ExprKind::Unary) return false;
        if (!expression.hostWriteHelper.empty() || !expression.typedWriteIntrinsic.empty()
            || !ScalarStorage(*expression.children[0])) return false;
        if (expression.kind == ExprKind::Assignment && expression.op == "=")
        { operation = IROp::Assign; return true; }
        if (!types_.IsNumeric(expression.type)) return false;
        if (expression.kind == ExprKind::Assignment)
        {
            if (expression.op == "+=") operation = IROp::AddAssign;
            else if (expression.op == "-=") operation = IROp::SubtractAssign;
            else if (expression.op == "*=") operation = IROp::MultiplyAssign;
            else if (expression.op == "/=") operation = IROp::DivideAssign;
            else if (expression.op == "%=") operation = IROp::RemainderAssign;
            else if (expression.op == "&=") operation = IROp::BitAndAssign;
            else if (expression.op == "|=") operation = IROp::BitOrAssign;
            else if (expression.op == "^=") operation = IROp::BitXorAssign;
            else if (expression.op == "<<=") operation = IROp::ShiftLeftAssign;
            else if (expression.op == ">>=") operation = IROp::ShiftRightAssign;
            else return false;
            return true;
        }
        if (expression.op == "++") operation = IROp::PreIncrement;
        else if (expression.op == "--") operation = IROp::PreDecrement;
        else if (expression.op == "post++") operation = IROp::PostIncrement;
        else if (expression.op == "post--") operation = IROp::PostDecrement;
        else return false;
        return true;
    }

    bool NumericIROp(const Expr& expression, IROp& operation) const
    {
        if (expression.kind == ExprKind::Binary && types_.IsNumeric(expression.children[0]->type)
            && types_.IsNumeric(expression.children[1]->type))
        {
            if (expression.op == "+") operation = IROp::Add;
            else if (expression.op == "-") operation = IROp::Subtract;
            else if (expression.op == "*") operation = IROp::Multiply;
            else if (expression.op == "/") operation = IROp::Divide;
            else if (expression.op == "%") operation = IROp::Remainder;
            else if (expression.op == "&") operation = IROp::BitAnd;
            else if (expression.op == "|") operation = IROp::BitOr;
            else if (expression.op == "^") operation = IROp::BitXor;
            else if (expression.op == "<<") operation = IROp::ShiftLeft;
            else if (expression.op == ">>") operation = IROp::ShiftRight;
            else if (expression.op == "==") operation = IROp::Equal;
            else if (expression.op == "!=") operation = IROp::NotEqual;
            else if (expression.op == "<") operation = IROp::Less;
            else if (expression.op == "<=") operation = IROp::LessEqual;
            else if (expression.op == ">") operation = IROp::Greater;
            else if (expression.op == ">=") operation = IROp::GreaterEqual;
            else return false;
            return true;
        }
        if (expression.kind == ExprKind::Unary)
        {
            if (expression.op == "-" && types_.IsNumeric(expression.type)) operation = IROp::Negate;
            else if (expression.op == "~") operation = IROp::BitNot;
            else if (expression.op == "!") operation = IROp::LogicalNot;
            else return false;
            return true;
        }
        if (expression.kind == ExprKind::Call && expression.children.size() == 2)
        {
            const TypeId argument = expression.children[1]->type;
            if (expression.callName == "tofloat" && argument == types_.Int()) operation = IROp::ToFloat;
            else if (expression.callName == "toint" && argument == types_.Float()) operation = IROp::ToInt;
            else return false;
            return true;
        }
        return false;
    }

    std::size_t AppendIR(IRNode node) const
    {
        result_.typedIR.nodes.push_back(std::move(node));
        return result_.typedIR.nodes.size() - 1;
    }

    std::size_t WidenIR(std::size_t input, const IRSourceSpan& source) const
    {
        if (result_.typedIR.nodes[input].type != IRValueType::Int) return input;
        IRNode conversion;
        conversion.op = IROp::ToFloat;
        conversion.type = IRValueType::Float;
        conversion.typeId = types_.Float();
        conversion.source = source;
        conversion.inputs.push_back(input);
        return AppendIR(std::move(conversion));
    }

    std::size_t ConvertIR(std::size_t input, TypeId target, const IRSourceSpan& source) const
    {
        if (result_.typedIR.nodes[input].typeId == target) return input;
        if (target == types_.Float()) return WidenIR(input, source);
        IRNode conversion;
        conversion.op = IROp::NullableConvert;
        conversion.type = IRType(target);
        conversion.typeId = target;
        conversion.source = source;
        conversion.inputs.push_back(input);
        return AppendIR(std::move(conversion));
    }

    std::size_t BuildValueIR(const Expr& expression) const
    {
        IRNode node;
        node.type = IRType(expression.type);
        node.typeId = expression.type;
        node.source.begin = expression.begin;
        node.source.end = expression.end;
        node.source.line = expression.token.line;
        node.source.column = expression.token.column;
        if (expression.functionIndex != static_cast<std::size_t>(-1))
        {
            node.op = IROp::ScriptCall;
            node.functionIndex = expression.functionIndex;
            const IRFunction& function = result_.typedIR.functions[node.functionIndex];
            for (std::size_t i = 1; i < expression.children.size(); ++i)
            {
                const std::size_t input = BuildValueIR(*expression.children[i]);
                const IRSourceSpan source = result_.typedIR.nodes[input].source;
                node.inputs.push_back(ConvertIR(input, function.parameterTypes[i - 1], source));
            }
        }
        else if (ScalarMutationIROp(expression, node.op))
        {
            ScalarStorage(*expression.children[0], &node.storage);
            if (expression.kind == ExprKind::Assignment)
            {
                std::size_t value = BuildValueIR(*expression.children[1]);
                if (node.type == IRValueType::Float)
                {
                    const IRSourceSpan origin = result_.typedIR.nodes[value].source;
                    value = WidenIR(value, origin);
                }
                node.inputs.push_back(value);
            }
        }
        else if (NumericIROp(expression, node.op))
        {
            const std::size_t first = expression.kind == ExprKind::Call ? 1 : 0;
            for (std::size_t i = first; i < expression.children.size(); ++i)
                node.inputs.push_back(BuildValueIR(*expression.children[i]));
            if (expression.kind == ExprKind::Binary &&
                (expression.children[0]->type == types_.Float() || expression.children[1]->type == types_.Float()))
                for (std::size_t i = 0; i < node.inputs.size(); ++i)
                {
                    const IRSourceSpan origin = result_.typedIR.nodes[node.inputs[i]].source;
                    node.inputs[i] = WidenIR(node.inputs[i], origin);
                }
        }
        else if (expression.kind == ExprKind::Literal &&
            (node.type == IRValueType::Int || node.type == IRValueType::Float || node.type == IRValueType::Bool))
        {
            node.op = node.type == IRValueType::Int ? IROp::IntLiteral
                : node.type == IRValueType::Float ? IROp::FloatLiteral : IROp::BoolLiteral;
            node.integer = expression.integerValue;
            node.number = expression.floatValue;
            node.boolean = expression.op == "true";
        }
        else if (expression.kind == ExprKind::Name && expression.symbol && !expression.symbol->constant &&
            !expression.symbol->hostGlobal && !expression.symbol->boxed)
        {
            node.op = IROp::Symbol;
            const auto inserted = irSymbols_.insert(std::make_pair(expression.symbol, irSymbols_.size() + 1));
            node.symbolId = inserted.first->second;
            node.backendName = expression.symbol->backendName;
        }
        else if (ValueStorage(expression, &node.storage))
        {
            node.op = IROp::LoadStorage;
        }
        else if (expression.kind == ExprKind::Unary && expression.op == "+")
        {
            const std::size_t input = BuildValueIR(*expression.children[0]);
            return expression.widen ? WidenIR(input, node.source) : input;
        }
        else
        {
            // 아직 이관하지 않은 내장 호출·컨테이너·호스트 선택자는 해당 실행 순서에 도달했을 때만 방출한다.
            // 숫자 자식은 다시 IR이 될 수 있지만 현재 식의 변환과 박싱은 중복하지 않는다.
            node.op = IROp::LegacyExpression;
            node.source.line = BeginLine(expression);
            const std::size_t newline = expression.begin ? source_.rfind('\n', expression.begin - 1) : std::string::npos;
            node.source.column = static_cast<int>(expression.begin - (newline == std::string::npos ? 0 : newline + 1) + 1);
            node.legacySource = RenderExpr(expression, false, false);
        }
        const IRSourceSpan origin = node.source;
        std::size_t result = AppendIR(std::move(node));
        if (expression.widen) result = WidenIR(result, origin);
        if (expression.clone)
        {
            IRNode copy;
            copy.op = IROp::CopyStruct;
            copy.type = IRType(expression.type);
            copy.typeId = expression.type;
            copy.source = origin;
            copy.inputs.push_back(result);
            copy.backendName = Helper("clone", expression.type);
            result = AppendIR(std::move(copy));
        }
        return result;
    }

    std::string RenderExpr(const Expr& expression, bool allowIR = true, bool applyWrappers = true) const
    {
        std::string text;
        IROp operation;
        if (allowIR && !expression.constantInitializer &&
            (NumericIROp(expression, operation) || ScalarMutationIROp(expression, operation)
                || expression.functionIndex != static_cast<std::size_t>(-1)
                || (expression.widen && !expression.constantWiden)))
        {
            const std::size_t root = BuildValueIR(expression);
            text = result_.typedIR.prefix + std::to_string(root) + "()" + Lines(expression.begin, expression.end);
            if (applyWrappers && expression.box) text = Intrinsic("cellnew", BeginLine(expression)) + "(" + text + ")";
            return text;
        }
        switch (expression.kind)
        {
        case ExprKind::Literal:
            text = expression.token.text;
            if (expression.token.kind == TokenKind::Float)
            {
                if (text[0] == '.') text.insert(0, "0");
                const std::size_t dot = text.find('.');
                if (dot != std::string::npos && (dot + 1 == text.size() || text[dot + 1] == 'e' || text[dot + 1] == 'E')) text.insert(dot + 1, "0");
            }
            text = Lines(expression.begin, expression.token.begin) + text + Lines(expression.token.end, expression.end);
            break;
        case ExprKind::Name:
            if (!expression.symbol)
            {
                text = Lines(expression.begin, expression.token.begin) + expression.callName + Lines(expression.token.end, expression.end);
                break;
            }
            text = expression.symbol->hostGlobal ? result_.hostPrefix + "g" + std::to_string(expression.symbol->hostGlobalIndex) + ".call()" : expression.symbol->backendName;
            if (!expression.cellStorage && expression.symbol->boxed)
                text = IsVector(expression.type) && !expression.writeTarget
                    ? Intrinsic("cellget", expression.token.line) + "(" + text + ")" : text + "[0]";
            text = Lines(expression.begin, expression.token.begin) + text + Lines(expression.token.end, expression.end);
            break;
        case ExprKind::Unary:
        {
            const Expr& value = *expression.children[0];
            if (!expression.typedWriteIntrinsic.empty())
            {
                text = expression.typedWriteIntrinsic + "(" + (expression.typedWriteFloat ? "1.0," : "1,")
                    + Lines(expression.begin, value.begin) + WriteReceiverArguments(value) + Lines(value.end, expression.end) + ")";
                break;
            }
            if (!expression.hostWriteHelper.empty())
            {
                const Expr& receiver = WriteReceiver(value);
                text = expression.hostWriteHelper + "(" + Lines(expression.begin, receiver.begin) + WriteReceiverArguments(value) + Lines(receiver.end, expression.end) + ")";
                break;
            }
            if (expression.op == "post++" || expression.op == "post--")
                text = "(" + Lines(expression.begin, value.begin) + RenderExpr(value) + Lines(value.end, expression.token.begin) + expression.op.substr(4) + Lines(expression.token.end, expression.end) + ")";
            else if (expression.op == "-" && IsArithmeticVector(value.type))
                text = Intrinsic("v" + std::to_string(VectorSize(value.type)) + "mul", expression.token.line)
                    + "(" + Lines(expression.begin, expression.token.begin) + Lines(expression.token.end, value.begin)
                    + RenderExpr(value) + ", -1.0" + Lines(value.end, expression.end) + ")";
            else text = "(" + Lines(expression.begin, expression.token.begin) + (expression.op == "+" ? "" : expression.op)
                + Lines(expression.token.end, value.begin) + RenderExpr(value) + Lines(value.end, expression.end) + ")";
            break;
        }
        case ExprKind::Binary:
        {
            const Expr& left = *expression.children[0];
            const Expr& right = *expression.children[1];
            if (!expression.callName.empty())
            {
                text = expression.callName + "(" + Lines(expression.begin, left.begin) + RenderExpr(left) + Lines(left.end, expression.token.begin)
                    + "," + Lines(expression.token.end, right.begin) + RenderExpr(right) + Lines(right.end, expression.end) + ")";
                break;
            }
            // 조건문의 최상위 논리식은 바깥 if/while 괄호로 충분하다. 추가 괄호는 분기를 불린 값 생성으로 바꾼다.
            // 자식 식의 괄호는 그대로 두어 중첩 논리식의 결합 순서와 단락 평가를 보존한다.
            const bool directCondition = expression.branchCondition && (expression.op == "&&" || expression.op == "||");
            text = (directCondition ? "" : "(") + Lines(expression.begin, left.begin) + RenderExpr(left) + Lines(left.end, expression.token.begin)
                + " " + expression.op + " " + Lines(expression.token.end, right.begin) + RenderExpr(right) + Lines(right.end, expression.end) + (directCondition ? "" : ")");
            break;
        }
        case ExprKind::Assignment:
        {
            const Expr& target = *expression.children[0];
            const Expr& value = *expression.children[1];
            if (!expression.typedWriteIntrinsic.empty())
            {
                std::string receiver = WriteReceiverArguments(target);
                receiver.erase(std::remove(receiver.begin(), receiver.end(), '\n'), receiver.end());
                std::string argument = RenderExpr(value);
                if (expression.typedWriteFloat && value.type == types_.Int() && !value.widen)
                    argument = Intrinsic("itof", BeginLine(value)) + "(" + argument + ")";
                text = expression.typedWriteIntrinsic + "(" + Lines(expression.begin, value.begin) + argument + ","
                    + receiver + Lines(value.end, expression.end) + ")";
            }
            else if (!expression.hostWriteHelper.empty())
            {
                // 우변을 먼저 확정한 뒤 전체 수신 객체 경로를 한 번만 계산한다.
                std::string receiver = WriteReceiverArguments(target);
                receiver.erase(std::remove(receiver.begin(), receiver.end(), '\n'), receiver.end());
                text = expression.hostWriteHelper + "(" + Lines(expression.begin, value.begin) + RenderExpr(value) + "," + receiver + Lines(value.end, expression.end) + ")";
            }
            else if (IsVector(target.type) && expression.op == "=" &&
                target.kind == ExprKind::Name && target.symbol->boxed)
            {
                std::string cell = target.symbol->backendName;
                cell.erase(std::remove(cell.begin(), cell.end(), '\n'), cell.end());
                text = Intrinsic("cellset", expression.token.line) + "(" + Lines(expression.begin, value.begin)
                    + RenderExpr(value) + "," + cell + Lines(value.end, expression.end) + ")";
            }
            else text = "(" + Lines(expression.begin, target.begin) + RenderExpr(target) + Lines(target.end, expression.token.begin)
                + " " + expression.op + " " + Lines(expression.token.end, value.begin) + RenderExpr(value) + Lines(value.end, expression.end) + ")";
            break;
        }
        case ExprKind::Call:
        {
            const Expr& callee = *expression.children[0];
            std::string callName = expression.callName;
            if (expression.indirectCall) callName = "(" + RenderExpr(callee) + ")";
            const bool builtinMethod = !callName.empty() && callName[0] == '.';
            if (builtinMethod)
                callName = "(" + RenderExpr(*callee.children[0]) + ")" + callName;
            text = Lines(expression.begin, callee.begin) + callName + "(";
            if (expression.receiverArgument)
            {
                const Expr& receiver = *callee.children[0];
                text += Lines(callee.begin, receiver.begin) + RenderExpr(receiver) + Lines(receiver.end, callee.end);
            }
            else if (builtinMethod) text += Lines(callee.children[0]->end, callee.end);
            else if (!expression.indirectCall) text += Lines(callee.begin, callee.end);
            std::size_t previous = callee.end;
            for (std::size_t i = 1; i < expression.children.size(); ++i)
            {
                if (i != 1 || expression.receiverArgument) text += ",";
                text += Lines(previous, expression.children[i]->begin);
                text += RenderExpr(*expression.children[i]);
                previous = expression.children[i]->end;
            }
            if (!expression.extraArgument.empty()) text += (expression.children.size() > 1 || expression.receiverArgument ? "," : "") + expression.extraArgument;
            text += Lines(previous, expression.end) + ")";
            break;
        }
        case ExprKind::Member:
        {
            const Expr& owner = *expression.children[0];
            if (expression.hostProperty)
            {
                text = HostCall(expression.hostProperty->getter) + "(" + Lines(expression.begin, owner.begin) + RenderExpr(owner)
                    + Lines(owner.end, expression.end) + ")";
                break;
            }
            if (expression.vectorComponent)
            {
                text = Intrinsic("v" + std::to_string(VectorSize(owner.type)) + "get_C" + std::to_string(expression.fieldIndex), expression.token.line)
                    + "(" + Lines(expression.begin, owner.begin) + RenderExpr(owner) + Lines(owner.end, expression.end) + ")";
                break;
            }
            text = Lines(expression.begin, owner.begin) + RenderExpr(owner) + "[" + Lines(owner.end, expression.token.begin) + std::to_string(expression.fieldIndex) + "]";
            text += Lines(expression.token.end, expression.end);
            break;
        }
        case ExprKind::Index:
        {
            const Expr& owner = *expression.children[0];
            const Expr& key = *expression.children[1];
            if (IsArray(owner.type))
                text = Intrinsic("a" + ArrayStorage(expression.type) + "get", expression.token.line) + "("
                    + Lines(expression.begin, owner.begin) + RenderExpr(owner) + Lines(owner.end, expression.token.begin) + ","
                    + Lines(expression.token.end, key.begin) + RenderExpr(key) + Lines(key.end, expression.end) + ")";
            else text = Lines(expression.begin, owner.begin) + RenderExpr(owner) + Lines(owner.end, expression.token.begin) + "["
                    + Lines(expression.token.end, key.begin) + RenderExpr(key) + Lines(key.end, expression.end) + "]";
            break;
        }
        case ExprKind::List:
        {
            text = Lines(expression.begin, expression.token.begin) + "[";
            std::size_t previous = expression.token.end;
            for (std::size_t i = 0; i < expression.children.size(); ++i)
            {
                if (i) text += ",";
                text += Lines(previous, expression.children[i]->begin) + RenderExpr(*expression.children[i]);
                previous = expression.children[i]->end;
            }
            text += Lines(previous, expression.end) + "]";
            break;
        }
        case ExprKind::Map:
        {
            text = "{";
            std::size_t previous = expression.begin;
            for (std::size_t i = 0; i < expression.children.size(); ++i)
            {
                if (i) text += i % 2 ? ":" : ",";
                text += Lines(previous, expression.children[i]->begin) + RenderExpr(*expression.children[i]);
                previous = expression.children[i]->end;
            }
            text += Lines(previous, expression.end) + "}";
            break;
        }
        case ExprKind::Lambda:
            text = expression.callName + "(";
            for (std::size_t i = 0; i < expression.captures.size(); ++i)
            {
                if (i) text += ",";
                text += CaptureValue(*expression.captures[i], expression.token.line);
            }
            text += ")" + Lines(expression.begin, expression.end);
            break;
        }
        if (applyWrappers && expression.widen) text = expression.constantWiden ? "(" + text + "+0.0)"
            : Intrinsic("itof", BeginLine(expression)) + "(" + text + ")";
        if (applyWrappers && expression.clone) text = Helper("clone", expression.type) + "(" + text + ")";
        if (applyWrappers && expression.box) text = Intrinsic("cellnew", BeginLine(expression)) + "(" + text + ")";
        return text;
    }

    static std::string ArithmeticSuffix(const std::string& operation)
    {
        if (operation == "+") return "add";
        if (operation == "-") return "sub";
        if (operation == "*") return "mul";
        if (operation == "/") return "div";
        if (operation == "%") return "mod";
        if (operation == "&") return "and";
        if (operation == "|") return "or";
        if (operation == "^") return "xor";
        if (operation == "<<") return "shl";
        if (operation == ">>") return "shr";
        throw std::logic_error("unsupported typed arithmetic operation");
    }

    std::string CaptureValue(const Symbol& symbol, int line) const
    {
        std::string value = symbol.backendName;
        if (symbol.boxed) value = Intrinsic("cellget", line) + "(" + value + ")";
        if (IsStruct(symbol.type)) value = Helper("clone", symbol.type) + "(" + value + ")";
        if (symbol.boxed) value = Intrinsic("cellnew", line) + "(" + value + ")";
        return value;
    }

    std::string Helpers() const
    {
        std::string result;
        for (const HostWrite& write : hostWrites_)
        {
            const bool increment = write.operation == "++" || write.operation == "--" || write.operation == "post++" || write.operation == "post--";
            const bool postfix = write.operation == "post++" || write.operation == "post--";
            result += "\nfun " + write.name + (increment ? "(var self){" : "(var value,var self){");
            if (write.component >= 0)
            {
                // 벡터는 값으로 가져온다. 쓰기 직전 setter가 같은 ID의 생존 여부를 다시 검사한다.
                const std::string vectorType = "v" + std::to_string(VectorSize(write.property->type));
                const std::string componentRead = Intrinsic(vectorType + "get_C" + std::to_string(write.component), write.line) + "(vector)";
                result += "var vector=" + HostCall(write.property->getter) + "(self);";
                if (write.operation != "=")
                {
                    result += "var old=" + componentRead + ";";
                    const std::string operation = increment ? (write.operation.find("++") != std::string::npos ? "+" : "-") : write.operation.substr(0, write.operation.size() - 1);
                    std::string argument = increment ? "1.0" : "value";
                    if (write.valueType == types_.Int() && !increment) argument = Intrinsic("itof", write.line) + "(" + argument + ")";
                    const std::string calculation = Intrinsic("f" + ArithmeticSuffix(operation), write.line) + "(old," + argument + ")";
                    result += std::string(increment ? "var value=" : "value=") + calculation + ";";
                }
                result += "vector=" + Intrinsic(vectorType + "set_C" + std::to_string(write.component), write.line) + "(vector,value);";
                result += HostCall(write.property->setter) + "(self,vector);";
                result += "return " + (postfix ? "old" : componentRead) + ";}";
                continue;
            }
            if (write.operation != "=")
            {
                result += "var old=" + HostCall(write.property->getter) + "(self);";
                const std::string operation = increment ? (write.operation.find("++") != std::string::npos ? "+" : "-") : write.operation.substr(0, write.operation.size() - 1);
                std::string argument = increment ? (write.property->type == types_.Float() ? "1.0" : "1") : "value";
                std::string prefix;
                if (types_.IsNumeric(write.property->type))
                {
                    const bool floating = write.property->type == types_.Float();
                    if (floating && write.valueType == types_.Int() && !increment) argument = Intrinsic("itof", write.line) + "(" + argument + ")";
                    prefix = floating ? "f" : "i";
                }
                else
                {
                    if (write.valueType == types_.Int()) argument = Intrinsic("itof", write.line) + "(" + argument + ")";
                    prefix = "v" + std::to_string(VectorSize(write.property->type));
                }
                const std::string calculation = Intrinsic(prefix + ArithmeticSuffix(operation), write.line) + "(old," + argument + ")";
                result += std::string(increment ? "var value=" : "value=") + calculation + ";";
            }
            result += HostCall(write.property->setter) + "(self,value);";
            result += "return " + std::string(postfix ? "old" : "value") + ";}";
        }
        for (TypeId type : structures_)
        {
            const TypeDesc& description = types_.Get(type);
            result += "\nfun " + Helper("make", type) + "(";
            for (std::size_t i = 0; i < description.fields.size(); ++i)
            {
                if (i) result += ",";
                result += "var a" + std::to_string(i);
            }
            result += "){return [";
            for (std::size_t i = 0; i < description.fields.size(); ++i)
            {
                if (i) result += ",";
                result += "a" + std::to_string(i);
            }
            result += "]; }\nfun " + Helper("clone", type) + "(var s){return [";
            for (std::size_t i = 0; i < description.fields.size(); ++i)
            {
                if (i) result += ",";
                std::string value = "s[" + std::to_string(i) + "]";
                if (IsStruct(description.fields[i].type)) value = Helper("clone", description.fields[i].type) + "(" + value + ")";
                result += value;
            }
            result += "]; }";
            if (Defaultable(type))
            {
                result += "\nfun " + Helper("default", type) + "(){return [";
                for (std::size_t i = 0; i < description.fields.size(); ++i)
                {
                    if (i) result += ",";
                    result += DefaultValue(description.fields[i].type);
                }
                result += "];}";
            }
        }
        if (resizeHelper_)
        {
            // 증가 중 오류가 나도 새 원소가 null인 중간 상태를 남기지 않는다.
            result += "\nfun " + internalPrefix_ + "resize(var a,var n,var fill){if(n<0)n=0;var old=tosize(a);a.reserve(n);if(n<old){a.resize(n);}else{for(var i in old,n){a.append(fill);}}}";
        }
        if (getHelper_) result += "\nfun " + internalPrefix_ + "get(var m,var k,var fallback){var v=m[k];if(v==null)return fallback;return v;}";
        if (concatHelper_) result += "\nfun " + internalPrefix_ + "concat(var a,var b){var r=a.clone();foreach(var v in b)r.append(v);return r;}";
        if (unionHelper_) result += "\nfun " + internalPrefix_ + "union(var a,var b){var r=a.clone();foreach(var v in b)r.add(v);return r;}";
        if (intersectionHelper_) result += "\nfun " + internalPrefix_ + "intersection(var a,var b){var r=a.clone();foreach(var v in a)if(!b.contains(v))r.remove(v);return r;}";
        if (differenceHelper_) result += "\nfun " + internalPrefix_ + "difference(var a,var b){var r=a.clone();foreach(var v in b)r.remove(v);return r;}";
        for (TypeId type : structListClones_)
            result += "\nfun " + internalPrefix_ + "clonelist_" + std::to_string(type) + "(var a){var r=[];foreach(var v in a)r.append(" + Helper("clone", type) + "(v));return r;}";
        for (TypeId type : structMapClones_)
            result += "\nfun " + internalPrefix_ + "clonemap_" + std::to_string(type) + "(var a){var r={};foreach(var k,v in a)r[k]=" + Helper("clone", type) + "(v);return r;}";
        for (TypeId type : structMapValues_)
            result += "\nfun " + internalPrefix_ + "mapvalues_" + std::to_string(type) + "(var a){var r=[];foreach(var k,v in a)r.append(" + Helper("clone", type) + "(v));return r;}";
        for (TypeId type : structListResizes_)
            result += "\nfun " + internalPrefix_ + "resize_" + std::to_string(type) + "(var a,var n,var fill){if(n<0)n=0;var old=tosize(a);a.reserve(n);if(n<old)a.resize(n);else for(var i in old,n)a.append(" + Helper("clone", type) + "(fill));}";
        for (TypeId type : structListConcats_)
            result += "\nfun " + internalPrefix_ + "concat_" + std::to_string(type) + "(var a,var b){var r=" + internalPrefix_ + "clonelist_" + std::to_string(type) + "(a);foreach(var v in b)r.append(" + Helper("clone", type) + "(v));return r;}";
        for (const Expr* lambda : lambdas_)
        {
            result += "\nfun " + lambda->callName + "(";
            for (std::size_t i = 0; i < lambda->captures.size(); ++i)
            {
                if (i) result += ",";
                result += "var " + lambda->captures[i]->backendName;
            }
            result += "){return fun(";
            for (std::size_t i = 0; i < lambda->lambdaParameters.size(); ++i)
            {
                if (i) result += ",";
                result += "var " + lambda->lambdaParameters[i]->backendName;
            }
            const Statement& body = *lambda->lambda->children[0];
            const std::string loweredBody = ApplyEdits(body.token.begin, body.end);
            const int generatedLine = 1 + static_cast<int>(std::count(result_.loweredSource.begin(), result_.loweredSource.end(), '\n'))
                + static_cast<int>(std::count(result.begin(), result.end(), '\n'));
            const int bodyLines = static_cast<int>(std::count(loweredBody.begin(), loweredBody.end(), '\n'));
            for (int line = 0; line <= bodyLines; ++line)
                result_.hostWriteSourceLines.push_back(std::make_pair(generatedLine + line, body.token.line + line));
            result += ")" + loweredBody + ";}";
        }
        return result;
    }

    std::string ApplyEdits(std::size_t begin, std::size_t end) const
    {
        std::vector<const Edit*> selected;
        for (const Edit& edit : edits_)
            if (edit.begin >= begin && edit.end <= end) selected.push_back(&edit);
        std::stable_sort(selected.begin(), selected.end(), [](const Edit* a, const Edit* b)
        {
            if (a->begin != b->begin) return a->begin < b->begin;
            if (a->end == a->begin || b->end == b->begin) return a->end == a->begin && b->end != b->begin;
            return a->end > b->end;
        });
        std::string output;
        std::size_t edit = 0;
        for (std::size_t position = begin; position <= end;)
        {
            while (edit < selected.size() && selected[edit]->begin < position) ++edit;
            while (edit < selected.size() && selected[edit]->begin == position && selected[edit]->end == position)
                output += selected[edit++]->text;
            if (edit < selected.size() && selected[edit]->begin == position)
            {
                output += selected[edit]->text;
                position = selected[edit++]->end;
            }
            else
            {
                if (position == end) break;
                output += source_[position++];
            }
        }
        return output;
    }

    std::string Lower() const { return ApplyEdits(0, source_.size()); }
};

}

bool CompileStaticSource(const std::string& source, StaticCompileResult& result, const HostSchema* hostSchema)
{
    result = StaticCompileResult();
    try
    {
        if (hostSchema)
        {
            if (!hostSchema->Validate(result.errorMessage)) return false;
            result.types = hostSchema->Types();
        }
        const std::vector<Token> tokens = Lexer(source).Read();
        std::vector<std::unique_ptr<Statement>> program = Parser(tokens, result.types).Read();
        Checker(source, result, hostSchema).Check(program);
        result.success = true;
        return true;
    }
    catch (const Diagnostic& diagnostic)
    {
        result.errorMessage = diagnostic.message;
        result.errorLine = diagnostic.token.line;
        result.errorColumn = diagnostic.token.column;
        result.loweredSource.clear();
        return false;
    }
}

}
