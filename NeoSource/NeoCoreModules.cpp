#include "NeoCoreModules.h"

#include "NeoCoreCompiler.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace NeoCore
{
namespace
{

// 토큰 글자는 모듈 소스(또는 define 치환 문자열) 안의 구간을 가리킨다. 소유하지 않으므로 토큰마다 문자열을
// 복사하지 않는다. 가리키는 문자열은 Unit이 살아 있는 동안 바뀌지 않는다.
struct Token
{
    const char* text = "";
    std::size_t size = 0;
    std::size_t begin = 0, end = 0;
    int line = 1, column = 1;
    bool identifier = false;
    bool expanded = false;

    bool Is(const char* word) const
    {
        const std::size_t length = std::strlen(word);
        return length == size && std::memcmp(text, word, length) == 0;
    }
    bool Is(const std::string& word) const { return word.size() == size && std::memcmp(text, word.data(), size) == 0; }
    std::string Text() const { return std::string(text, size); }
};

struct Diagnostic
{
    Error error;
};

static void Fail(const std::string& sourceName, const Token& token, const std::string& message)
{
    Diagnostic failure;
    failure.error.sourceName = sourceName;
    failure.error.line = token.line;
    failure.error.column = token.column;
    failure.error.message = message;
    throw failure;
}

static bool IdentifierStart(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool IdentifierPart(unsigned char c)
{
    return IdentifierStart(c) || (c >= '0' && c <= '9');
}

static bool ReservedImportName(const std::string& name)
{
    static const char* const reserved[] = {
        "var", "fun", "int", "float", "bool", "string", "void", "int32", "float32", "any",
        "null", "true", "false", "if", "else", "while", "for", "foreach", "in", "return",
        "break", "continue", "import", "public", "export", "const", "struct", "class", "switch",
        "case", "default", "print", "tostring", "toint", "tofloat", "tosize", "sleep",
        "yield", "list", "map", "array", "array32", "set", "fn"
    };
    for (const char* word : reserved) if (name == word) return true;
    return false;
}

// 이름 표의 열쇠. 가리키는 글자는 표보다 오래 사는 문자열(모듈 소스, 보존 이름 저장소)에 있다.
struct NameRef
{
    const char* data;
    std::size_t size;
};
struct NameHash
{
    std::size_t operator()(const NameRef& name) const
    {
        std::size_t hash = static_cast<std::size_t>(1469598103934665603ull);
        for (std::size_t i = 0; i < name.size; ++i)
            hash = (hash ^ static_cast<unsigned char>(name.data[i])) * static_cast<std::size_t>(1099511628211ull);
        return hash;
    }
};
struct NameEqual
{
    bool operator()(const NameRef& a, const NameRef& b) const { return a.size == b.size && std::memcmp(a.data, b.data, a.size) == 0; }
};

// 문자열·주석을 그대로 두고 식별자 경계만 읽는다. 문법과 타입 판정은 정적 컴파일러의 일이다.
static void Tokenize(const std::string& source, const std::string& name, std::vector<Token>& tokens)
{
    std::size_t at = source.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
    int line = 1, column = 1;
    const auto advance = [&]()
    {
        if (source[at++] == '\n') { ++line; column = 1; }
        else ++column;
    };
    while (at < source.size())
    {
        if (std::isspace(static_cast<unsigned char>(source[at]))) { advance(); continue; }
        if (source.compare(at, 2, "//") == 0)
        {
            while (at < source.size() && source[at] != '\n') advance();
            continue;
        }
        Token token;
        token.begin = at; token.line = line; token.column = column;
        if (source.compare(at, 2, "/*") == 0)
        {
            advance(); advance();
            while (at < source.size() && source.compare(at, 2, "*/") != 0) advance();
            if (at == source.size()) Fail(name, token, "unterminated block comment");
            advance(); advance();
            continue;
        }
        const unsigned char first = static_cast<unsigned char>(source[at]);
        if (IdentifierStart(first))
        {
            token.identifier = true;
            do { advance(); } while (at < source.size() && IdentifierPart(static_cast<unsigned char>(source[at])));
        }
        else if (first == '\'' || first == '"')
        {
            const char quote = source[at];
            advance();
            bool closed = false;
            while (at < source.size())
            {
                const char next = source[at];
                if (next == '\n' || next == '\r') Fail(name, token, "a string literal cannot contain a raw newline");
                advance();
                if (next == quote) { closed = true; break; }
                if (next == '\\' && at < source.size())
                {
                    if (source[at] == '\n' || source[at] == '\r') Fail(name, token, "a string literal cannot contain a raw newline");
                    advance();
                }
            }
            if (!closed) Fail(name, token, "unterminated string literal");
        }
        else if (std::isdigit(first) || (first == '.' && at + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[at + 1]))))
        {
            if (source.compare(at, 2, "0x") == 0 || source.compare(at, 2, "0X") == 0)
            {
                advance(); advance();
                while (at < source.size() && std::isxdigit(static_cast<unsigned char>(source[at]))) advance();
            }
            else
            {
                while (at < source.size() && std::isdigit(static_cast<unsigned char>(source[at]))) advance();
                if (at < source.size() && source[at] == '.' && source.compare(at, 2, "..") != 0)
                {
                    advance();
                    while (at < source.size() && std::isdigit(static_cast<unsigned char>(source[at]))) advance();
                }
                if (at < source.size() && (source[at] == 'e' || source[at] == 'E'))
                {
                    advance();
                    if (at < source.size() && (source[at] == '+' || source[at] == '-')) advance();
                    while (at < source.size() && std::isdigit(static_cast<unsigned char>(source[at]))) advance();
                }
            }
        }
        else
        {
            const bool pair = source.compare(at, 2, "..") == 0 || source.compare(at, 2, "->") == 0;
            advance();
            if (pair) advance();
        }
        token.end = at;
        token.text = source.data() + token.begin;
        token.size = token.end - token.begin;
        tokens.push_back(token);
    }
}

struct Import
{
    std::string name, alias;
    std::size_t first = 0, last = 0;
    std::size_t target = 0;
    bool builtin = false;
};

// 토큰을 내보낼 때의 처리. 식별자 대부분이 모듈 접두사를 받으므로 토큰마다 치환 문자열을 만들어 두지 않고
// 종류만 적는다. 다른 모듈의 멤버로 바뀌는 드문 토큰만 문자열(Unit::memberNames)을 가진다.
enum TokenAction : unsigned char { KeepToken, DropToken, PrefixToken, MemberToken };

struct Unit
{
    ModuleSource input;
    // define 치환 토큰이 가리키는 문자열. 토큰을 만든 뒤에는 바꾸지 않는다.
    std::vector<StaticDefineReplacement> defineValues;
    std::vector<Token> tokens;
    std::vector<std::size_t> lineStarts;
    std::vector<Import> imports;
    std::map<std::string, bool> symbols;
    std::set<std::size_t> fieldNames, strippedExports;
    std::vector<unsigned char> actions;
    std::map<std::size_t, std::string> memberNames;
    std::string prefix, diagnosticName;
    // 줄 찾기의 시작 위치. 한 모듈의 내보내기는 앞에서 뒤로 가므로 직전 줄부터 찾는다.
    mutable std::size_t lineCursor = 0;
    bool visiting = false, visited = false, emitted = false;
};

static bool At(const std::vector<Token>& tokens, std::size_t index, const char* value)
{
    return index < tokens.size() && tokens[index].Is(value);
}

// 타입의 모양만 건너뛰며 지원 여부는 합친 소스의 타입 검사에서 결정한다.
static bool SkipType(const std::vector<Token>& tokens, std::size_t& index, unsigned depth = 0)
{
    if (depth > 128 || index >= tokens.size() || !tokens[index].identifier) return false;
    const bool function = tokens[index++].Is("fn");
    // 다른 모듈의 공개 구조체는 alias.Type 으로 쓴다. 제네릭 인자와 함수 타입 안에서도 같은 규칙이다.
    if (!function && At(tokens, index, ".") && index + 1 < tokens.size() && tokens[index + 1].identifier) index += 2;
    if (function)
    {
        if (!At(tokens, index++, "(")) return false;
        if (!At(tokens, index, ")"))
        {
            for (;;)
            {
                if (!SkipType(tokens, index, depth + 1)) return false;
                if (!At(tokens, index, ",")) break;
                ++index;
            }
        }
        if (!At(tokens, index++, ")") || !At(tokens, index++, "->") || !SkipType(tokens, index, depth + 1)) return false;
    }
    else if (At(tokens, index, "<"))
    {
        ++index;
        for (;;)
        {
            if (!SkipType(tokens, index, depth + 1)) return false;
            if (!At(tokens, index, ",")) break;
            ++index;
        }
        if (!At(tokens, index++, ">")) return false;
    }
    if (At(tokens, index, "?")) ++index;
    return true;
}

static void Analyze(Unit& unit, const std::vector<CompileDefine>* defines)
{
    // 토큰 수는 소스 크기로 어림해 미리 잡는다. 늘리면서 옮기는 일과 그동안의 두 벌 메모리를 피한다.
    unit.tokens.reserve(unit.input.source.size() / 4 + 16);
    Tokenize(unit.input.source, unit.input.sourceName, unit.tokens);
    if (defines)
    {
        std::string error;
        Token at;
        if (!ExpandStaticDefines(unit.input.source, *defines, unit.defineValues, error, at.line, at.column))
            Fail(unit.input.sourceName, at, error);
        // 치환할 자리가 없으면 토큰은 그대로다.
        if (!unit.defineValues.empty())
        {
            std::vector<Token> expanded;
            expanded.reserve(unit.tokens.size() + unit.defineValues.size());
            std::vector<Token> values;
            std::size_t replacement = 0;
            for (const Token& original : unit.tokens)
            {
                if (replacement == unit.defineValues.size() || unit.defineValues[replacement].begin != original.begin)
                { expanded.push_back(original); continue; }
                values.clear();
                Tokenize(unit.defineValues[replacement++].text, unit.input.sourceName, values);
                for (std::size_t i = 0; i < values.size(); ++i)
                {
                    values[i].begin = original.begin;
                    values[i].end = i + 1 == values.size() ? original.end : original.begin;
                    values[i].line = original.line; values[i].column = original.column; values[i].expanded = true;
                    expanded.push_back(values[i]);
                }
            }
            unit.tokens.swap(expanded);
        }
    }
    if (unit.tokens.capacity() > unit.tokens.size() + unit.tokens.size() / 4 + 16)
        std::vector<Token>(unit.tokens).swap(unit.tokens);
    unit.lineStarts.push_back(unit.input.source.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0);
    for (std::size_t i = 0; i < unit.input.source.size(); ++i)
        if (unit.input.source[i] == '\n') unit.lineStarts.push_back(i + 1);
    const std::vector<Token>& tokens = unit.tokens;
    int braces = 0, parentheses = 0, brackets = 0;
    std::set<std::string> aliases;
    // 여는 괄호 글자와 그 토큰 위치.
    std::vector<std::pair<char, std::size_t>> delimiters;
    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        if (!braces && !parentheses && !brackets)
        {
            if (tokens[i].Is("import"))
            {
                if (i && !tokens[i - 1].Is(";") && !tokens[i - 1].Is("}"))
                    Fail(unit.input.sourceName, tokens[i], "import is only allowed at file scope");
                Import import;
                import.first = i;
                if (i + 2 >= tokens.size() || !tokens[i + 1].identifier)
                    Fail(unit.input.sourceName, tokens[i], "expected a module name after import");
                import.name = import.alias = tokens[i + 1].Text();
                std::size_t end = i + 2;
                if (At(tokens, end, "as"))
                {
                    if (end + 1 >= tokens.size() || !tokens[end + 1].identifier)
                        Fail(unit.input.sourceName, tokens[end], "expected a module alias");
                    import.alias = tokens[end + 1].Text();
                    end += 2;
                }
                if (!At(tokens, end, ";")) Fail(unit.input.sourceName, tokens[i], "expected ';' after import");
                import.last = end;
                import.builtin = import.name == "math" || import.name == "system" || import.name == "coroutine";
                if (ReservedImportName(import.name) || ReservedImportName(import.alias))
                    Fail(unit.input.sourceName, tokens[i], "module names and aliases cannot be reserved words");
                if (import.builtin && end != i + 2)
                    Fail(unit.input.sourceName, tokens[i], "built-in module aliases are not supported");
                if (!aliases.insert(import.alias).second && !import.builtin)
                    Fail(unit.input.sourceName, tokens[i], "duplicate module alias: " + import.alias);
                unit.imports.push_back(import);
                i = end;
                continue;
            }
            std::size_t name = i;
            bool publicSymbol = false, exported = false, constant = false;
            std::vector<std::size_t> exports;
            while (name < tokens.size())
            {
                const Token& modifier = tokens[name];
                const bool isPublic = modifier.Is("public"), isExport = !isPublic && modifier.Is("export");
                if (!isPublic && !isExport && !modifier.Is("const")) break;
                bool& seen = isPublic ? publicSymbol : isExport ? exported : constant;
                if (seen) Fail(unit.input.sourceName, tokens[name], "duplicate declaration modifier");
                seen = true;
                if (isExport) exports.push_back(name);
                ++name;
            }
            if ((publicSymbol || exported || constant) && i && !tokens[i - 1].Is(";") && !tokens[i - 1].Is("}"))
                Fail(unit.input.sourceName, tokens[i], "public, export and const are only allowed at file scope");
            if (exported && constant) Fail(unit.input.sourceName, tokens[i], "export const is not supported");
            if (At(tokens, name, "struct") && name + 2 < tokens.size() && tokens[name + 1].identifier && At(tokens, name + 2, "{"))
            {
                unit.symbols[tokens[name + 1].Text()] = publicSymbol;
                std::size_t field = name + 3;
                while (field < tokens.size() && !At(tokens, field, "}"))
                {
                    if (!SkipType(tokens, field) || field >= tokens.size() || !tokens[field].identifier) break;
                    unit.fieldNames.insert(field++);
                    // 필드 기본값 식은 그대로 두고 건너뛴다. 식 안의 const 이름은 일반 식별자처럼 모듈 접두사를 받는다.
                    if (At(tokens, field, "="))
                    {
                        int nesting = 0;
                        while (field < tokens.size())
                        {
                            const Token& text = tokens[field];
                            if (text.Is("(") || text.Is("[") || text.Is("{")) ++nesting;
                            else if (text.Is(")") || text.Is("]") || text.Is("}")) --nesting;
                            else if (text.Is(";") && nesting == 0) break;
                            ++field;
                        }
                    }
                    if (!At(tokens, field++, ";")) break;
                }
                // 수식어 뒤의 struct 토큰을 다시 선언 시작으로 보지 않도록 건너뛴다. 가시성이 덮어써지지 않는다.
                i = name;
            }
            else if (SkipType(tokens, name) && name < tokens.size() && tokens[name].identifier &&
                (At(tokens, name + 1, "(") || At(tokens, name + 1, "=") || At(tokens, name + 1, ";")))
            {
                unit.symbols[tokens[name].Text()] = publicSymbol;
                unit.strippedExports.insert(exports.begin(), exports.end());
                i = name;
            }
        }
        // 괄호 글자는 모두 한 글자 토큰이다.
        const char delimiter = tokens[i].size == 1 ? tokens[i].text[0] : '\0';
        if (delimiter == '{' || delimiter == '(' || delimiter == '[')
            delimiters.push_back(std::make_pair(delimiter, i));
        else if (delimiter == '}' || delimiter == ')' || delimiter == ']')
        {
            const char expected = delimiter == '}' ? '{' : delimiter == ')' ? '(' : '[';
            if (delimiters.empty() || delimiters.back().first != expected)
                Fail(unit.input.sourceName, tokens[i], "unmatched delimiter in module source");
            delimiters.pop_back();
        }
        if (delimiter == '{') ++braces;
        else if (delimiter == '}') --braces;
        else if (delimiter == '(') ++parentheses;
        else if (delimiter == ')') --parentheses;
        else if (delimiter == '[') ++brackets;
        else if (delimiter == ']') --brackets;
    }
    if (!delimiters.empty())
        Fail(unit.input.sourceName, tokens[delimiters.back().second], "unclosed delimiter in module source");
}

class Writer
{
public:
    explicit Writer(ModuleBundle& output) : output_(output) {}

    void Append(const char* text, std::size_t size, const Unit& unit, std::size_t originalOffset, bool fixed = false)
    {
        if (!size) return;
        // 원본 위치의 줄. 직전에 찾은 줄에서 이어 찾고, 뒤로 간 위치만 처음부터 다시 찾는다.
        const std::vector<std::size_t>& starts = unit.lineStarts;
        std::size_t cursor = unit.lineCursor;
        if (cursor >= starts.size() || starts[cursor] > originalOffset)
            cursor = static_cast<std::size_t>(std::upper_bound(starts.begin(), starts.end(), originalOffset) - starts.begin());
        else
        {
            while (cursor < starts.size() && starts[cursor] <= originalOffset) ++cursor;
        }
        // cursor는 originalOffset보다 뒤에서 시작하는 첫 줄의 번호(= 1부터 세는 줄 번호)다.
        std::size_t sourceLine = cursor;
        unit.lineCursor = cursor ? cursor - 1 : 0;
        if (!sourceLine) sourceLine = 1;
        int sourceColumn = static_cast<int>(originalOffset - starts[sourceLine - 1]) + 1;
        bool mapping = true;
        std::size_t position = 0;
        while (position < size)
        {
            if (newLine_)
            {
                FlushColumns();
                output_.lines.push_back(ModuleSourceLine());
                ModuleSourceLine& line = output_.lines.back();
                line.sourceName = unit.input.sourceName;
                line.line = static_cast<int>(sourceLine);
                newLine_ = false;
                column_ = 1;
                mapping = true;
            }
            if (mapping)
            {
                ModuleSourceColumn location;
                location.generatedColumn = column_;
                location.sourceColumn = sourceColumn;
                location.fixed = fixed;
                columns_.push_back(location);
                mapping = false;
            }
            // 줄 끝까지를 한 번에 붙인다.
            const char* const newline = static_cast<const char*>(std::memchr(text + position, '\n', size - position));
            const std::size_t end = newline ? static_cast<std::size_t>(newline - text) + 1 : size;
            const int plain = static_cast<int>(end - position) - (newline ? 1 : 0);
            output_.source.append(text + position, end - position);
            column_ += plain;
            if (!fixed) sourceColumn += plain;
            if (newline)
            {
                newLine_ = true;
                if (!fixed) { ++sourceLine; sourceColumn = 1; }
            }
            position = end;
        }
    }
    void Append(const std::string& text, const Unit& unit, std::size_t originalOffset, bool fixed = false)
    { Append(text.data(), text.size(), unit, originalOffset, fixed); }

    void Newline(const Unit& unit, std::size_t offset)
    {
        if (!output_.source.empty() && output_.source.back() != '\n') Append("\n", 1, unit, offset, true);
    }

    void Finish(const Unit& unit)
    {
        FlushColumns();
        if (!newLine_) return;
        ModuleSourceLine line;
        line.sourceName = unit.input.sourceName;
        line.line = static_cast<int>(unit.lineStarts.size());
        ModuleSourceColumn column;
        column.sourceColumn = static_cast<int>(unit.input.source.size() - unit.lineStarts.back()) + 1;
        column.fixed = true;
        line.columns.push_back(column);
        output_.lines.push_back(line);
    }

private:
    ModuleBundle& output_;
    // 지금 쓰는 줄의 열 대응. 줄이 끝날 때 꼭 맞는 크기로 한 번에 옮겨, 줄마다 벡터를 여러 번 늘리지 않는다.
    std::vector<ModuleSourceColumn> columns_;
    bool newLine_ = true;
    int column_ = 1;

    void FlushColumns()
    {
        if (columns_.empty()) return;
        output_.lines.back().columns.assign(columns_.begin(), columns_.end());
        columns_.clear();
    }
};

class Bundler
{
public:
    Bundler(const ModuleLoader& loader, const std::vector<std::string>& externalNames, const std::vector<CompileDefine>* defines,
        std::size_t diagnosticLimit, const std::string* privateEntrySource)
        : loader_(loader), defines_(defines), diagnosticLimit_(std::max<std::size_t>(1, diagnosticLimit)),
          privateEntrySource_(privateEntrySource ? *privateEntrySource : std::string())
    {
        static const char* const words[] = {
            "var", "fun", "int", "float", "bool", "string", "void", "int32", "float32", "any",
            "null", "true", "false", "if", "else", "while", "for", "foreach", "in", "return",
            "break", "continue", "import", "public", "export", "const", "struct", "class",
            "switch", "case", "default", "print", "tostring", "toint", "tofloat", "tosize",
            "sleep", "yield", "list", "map", "array", "array32", "set", "fn", "Vector2", "Vector3",
            "Vector4", "Quaternion", "TimeSpan", "Stopwatch", "AsyncRequest", "Coroutine", "math", "system", "coroutine"
        };
        // 저장소를 다 채운 뒤에 표를 만든다. 표의 열쇠가 저장소의 문자열을 가리킨다.
        preservedNames_.assign(words, words + sizeof(words) / sizeof(words[0]));
        preservedNames_.insert(preservedNames_.end(), externalNames.begin(), externalNames.end());
        for (const std::string& name : preservedNames_)
        {
            NameRef key; key.data = name.data(); key.size = name.size();
            preserved_.insert(key);
        }
    }

    void Build(const std::string& source, const std::string& sourceName, ModuleBundle& output)
    {
        std::unique_ptr<Unit> root(new Unit);
        root->input.source = source;
        root->input.sourceName = sourceName;
        Analyze(*root, defines_);
        identities_[sourceName] = 0;
        units_.push_back(std::move(root));
        Visit(0, 0);
        if (diagnostics_.empty() && !privateEntrySource_.empty() && !identities_.count(privateEntrySource_))
            Fail(sourceName, Token(), "entry private access source is not in the module bundle");
        if (!diagnostics_.empty())
        {
            Diagnostic failure;
            failure.error.message=diagnostics_.front().message; failure.error.sourceName=diagnostics_.front().sourceName;
            failure.error.line=diagnostics_.front().line; failure.error.column=diagnostics_.front().column;
            failure.error.diagnostics=diagnostics_; throw failure;
        }
        std::string prefix = "__ncmodule_";
        for (;;)
        {
            bool collision = false;
            for (const std::unique_ptr<Unit>& unit : units_)
                for (const Token& token : unit->tokens)
                    if (token.identifier && token.size >= prefix.size() && std::memcmp(token.text, prefix.data(), prefix.size()) == 0) collision = true;
            if (!collision) break;
            prefix += "x";
        }
        std::size_t total = 0;
        for (std::size_t i = 0; i < units_.size(); ++i) total += units_[i]->input.source.size();
        for (std::size_t i = 1; i < units_.size(); ++i) units_[i]->prefix = prefix + std::to_string(i) + "_";
        for (std::size_t i = 0; i < units_.size(); ++i)
        {
            // 식별자가 접두사를 받는지는 글자만으로 정해진다. 모듈 안에서 같은 이름은 한 번만 판정하고, 원래 이름
            // 표(symbolNames)에도 한 번만 넣는다.
            Unit& unit = *units_[i];
            unit.actions.assign(unit.tokens.size(), KeepToken);
            if (!i) continue;
            std::unordered_map<NameRef, bool, NameHash, NameEqual> renamed;
            for (std::size_t index = 0; index < unit.tokens.size(); ++index)
            {
                const Token& token = unit.tokens[index];
                if (!token.identifier) continue;
                NameRef key; key.data = token.text; key.size = token.size;
                const auto known = renamed.find(key);
                bool rename;
                if (known != renamed.end()) rename = known->second;
                else
                {
                    rename = preserved_.count(key) == 0;
                    renamed.insert(std::make_pair(key, rename));
                    if (rename)
                    {
                        const std::string name = token.Text();
                        output.symbolNames[unit.prefix + name] = name;
                    }
                }
                if (rename) unit.actions[index] = PrefixToken;
            }
        }
        // 구조체 표시 이름은 첫 import의 모듈 이름으로 고정한다. 별칭과 내부 타입 정체성은 바꾸지 않는다.
        for (std::size_t i = 1; i < units_.size(); ++i)
            for (std::size_t token = 0; token + 1 < units_[i]->tokens.size(); ++token)
                if (At(units_[i]->tokens, token, "struct") && units_[i]->tokens[token + 1].identifier)
                {
                    const std::string name = units_[i]->tokens[token + 1].Text();
                    output.symbolNames[units_[i]->prefix + name] = units_[i]->diagnosticName + "." + name;
                }
        for (std::size_t i = 0; i < units_.size(); ++i) Rewrite(i);
        // 접두사가 붙어 원본보다 길어진다. 넉넉히 잡아 두고 모자라면 늘린다.
        output.source.reserve(total + total / 2 + 64);
        Writer writer(output);
        Emit(0, writer);
        writer.Finish(*units_[0]);
    }

private:
    const ModuleLoader& loader_;
    const std::vector<CompileDefine>* defines_;
    std::vector<std::unique_ptr<Unit>> units_;
    std::map<std::string, std::size_t> identities_;
    std::vector<std::string> preservedNames_;
    std::unordered_set<NameRef, NameHash, NameEqual> preserved_;
    std::vector<CompileDiagnostic> diagnostics_;
    std::size_t diagnosticLimit_;
    std::string privateEntrySource_;

    bool Preserved(const std::string& name) const
    {
        NameRef key; key.data = name.data(); key.size = name.size();
        return preserved_.count(key) != 0;
    }

    void Visit(std::size_t index, unsigned depth)
    {
        Unit& unit = *units_[index];
        if (depth > 128) Fail(unit.input.sourceName, Token(), "module import nesting exceeds compiler limit");
        unit.visiting = true;
        // 반복 중에 units_ 가 늘어도 Unit 자체는 옮겨지지 않는다(unique_ptr).
        for (Import& import : unit.imports)
        {
            if (diagnostics_.size() >= diagnosticLimit_) break;
            if (import.builtin) continue;
            try
            {
                const Token& token = unit.tokens[import.first];
                if (!loader_) Fail(unit.input.sourceName, token, "user module imports require a ModuleLoader");
                ModuleSource input;
                Error error;
                if (!loader_(import.name, unit.input.sourceName, input, error))
                {
                    if (error.message.empty()) error.message = "cannot load module: " + import.name;
                    if (error.sourceName.empty()) { error.sourceName = unit.input.sourceName; error.line = token.line; error.column = token.column; }
                    Diagnostic failure; failure.error = error; throw failure;
                }
                if (input.sourceName.empty()) input.sourceName = import.name + ".nc";
                const std::map<std::string, std::size_t>::const_iterator known = identities_.find(input.sourceName);
                if (known != identities_.end())
                {
                    import.target = known->second;
                    if (units_[import.target]->visiting) Fail(unit.input.sourceName, token, "cyclic module import: " + input.sourceName);
                    if (units_[import.target]->input.source != input.source)
                        Fail(unit.input.sourceName, token, "module loader returned inconsistent source: " + input.sourceName);
                    continue;
                }
                if (units_.size() >= 1024) Fail(unit.input.sourceName, token, "module count exceeds compiler limit");
                std::unique_ptr<Unit> loaded(new Unit);
                loaded->diagnosticName = import.name;
                loaded->input = std::move(input);
                Analyze(*loaded, defines_);
                if (!loaded->tokens.empty() && !loaded->tokens.back().Is(";") && !loaded->tokens.back().Is("}"))
                    Fail(loaded->input.sourceName, loaded->tokens.back(), "incomplete statement at end of module source");
                import.target = units_.size();
                identities_[loaded->input.sourceName] = import.target;
                units_.push_back(std::move(loaded));
                Visit(import.target, depth + 1);
            }
            catch (const Diagnostic& failure)
            {
                CompileDiagnostic item; item.message=failure.error.message; item.sourceName=failure.error.sourceName;
                item.line=failure.error.line; item.column=failure.error.column; diagnostics_.push_back(item);
            }
        }
        unit.visiting = false;
        unit.visited = true;
    }

    void Rewrite(std::size_t index)
    {
        Unit& unit = *units_[index];
        // 호스트 구현을 합친 실행용 루트가 아니라 원래 지정한 시험 파일 하나에만 적용한다.
        const bool entryAccess = !privateEntrySource_.empty() && unit.input.sourceName == privateEntrySource_;
        // 같은 별칭이 여럿이면 앞선 import를 쓴다. import 수가 적어 차례로 찾는다.
        std::vector<const Import*> aliases;
        std::vector<char> importTokens(unit.tokens.size(), 0);
        for (const Import& import : unit.imports)
        {
            if (!import.builtin && Preserved(import.alias))
                Fail(unit.input.sourceName, unit.tokens[import.first], "module alias conflicts with a built-in or host name: " + import.alias);
            bool known = false;
            for (const Import* previous : aliases) if (previous->alias == import.alias) { known = true; break; }
            if (!known) aliases.push_back(&import);
            for (std::size_t i = import.first; i <= import.last; ++i)
            {
                importTokens[i] = 1;
                if (!import.builtin) unit.actions[i] = DropToken;
            }
        }
        for (std::size_t i = 0; i < unit.tokens.size(); ++i)
        {
            const Token& token = unit.tokens[i];
            if (importTokens[i] || !token.identifier || unit.fieldNames.count(i) || (i && At(unit.tokens, i - 1, "."))) { ResetAction(unit, i); continue; }
            const Import* imported = nullptr;
            for (const Import* candidate : aliases) if (token.Is(candidate->alias)) { imported = candidate; break; }
            if (imported)
            {
                const Import& import = *imported;
                if (i < import.last) Fail(unit.input.sourceName, token, "module must be imported before use: " + token.Text());
                if (import.builtin && token.Is("coroutine") && At(unit.tokens, i + 1, "<")) { ResetAction(unit, i); continue; }
                if (!At(unit.tokens, i + 1, ".") || i + 2 >= unit.tokens.size() || !unit.tokens[i + 2].identifier)
                    Fail(unit.input.sourceName, token, "a module alias cannot be hidden or used as a value: " + token.Text());
                if (import.builtin) { ResetAction(unit, i); continue; }
                const Unit& target = *units_[import.target];
                const Token& member = unit.tokens[i + 2];
                const std::string memberName = member.Text();
                const std::map<std::string, bool>::const_iterator symbol = target.symbols.find(memberName);
                if (symbol == target.symbols.end()) Fail(unit.input.sourceName, member, "module has no member: " + memberName);
                if (!symbol->second && !entryAccess) Fail(unit.input.sourceName, member, "module member is not public: " + memberName);
                unit.memberNames[i] = target.prefix + memberName;
                unit.actions[i] = MemberToken;
                unit.actions[i + 1] = DropToken;
                unit.actions[i + 2] = DropToken;
                i += 2;
                continue;
            }
            if ((token.Is("math") || token.Is("system") || token.Is("coroutine")) && At(unit.tokens, i + 1, "."))
                Fail(unit.input.sourceName, token, token.Text() + " must be imported before use");
            // 나머지 식별자는 Build가 적어 둔 대로다: 보존 이름이 아니면 모듈 접두사를 받는다.
            if (index && unit.strippedExports.count(i)) unit.actions[i] = DropToken;
        }
    }

    // 접두사 판정(PrefixToken)은 Build가 글자만 보고 미리 적어 둔 것이다. 이름을 바꾸지 않는 자리(import 문,
    // 필드 이름, 멤버 이름, 내장 모듈 별칭)의 토큰은 원래 글자로 되돌린다. 지우기로 한 토큰은 그대로 둔다.
    static void ResetAction(Unit& unit, std::size_t index)
    {
        if (unit.actions[index] == PrefixToken) unit.actions[index] = KeepToken;
    }

    void Emit(std::size_t index, Writer& writer)
    {
        Unit& unit = *units_[index];
        if (unit.emitted) return;
        unit.emitted = true;
        // import 문의 첫 토큰 자리에 그 모듈을 넣는다. 토큰 순서대로 훑으므로 다음 import만 기억한다.
        std::vector<const Import*> imports;
        for (const Import& import : unit.imports) if (!import.builtin) imports.push_back(&import);
        std::size_t nextImport = 0;
        const std::string& source = unit.input.source;
        std::size_t offset = source.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
        std::string renamed;
        for (std::size_t i = 0; i < unit.tokens.size(); ++i)
        {
            const Token& token = unit.tokens[i];
            writer.Append(source.data() + offset, token.begin - offset, unit, offset);
            if (nextImport < imports.size() && imports[nextImport]->first == i)
            {
                const Import& import = *imports[nextImport++];
                writer.Newline(unit, token.begin);
                writer.Append("{}\n", 3, unit, token.begin, true);
                Emit(import.target, writer);
                writer.Newline(unit, token.begin);
                writer.Append("{}\n", 3, unit, token.begin, true);
            }
            switch (unit.actions[i])
            {
            case KeepToken: writer.Append(token.text, token.size, unit, token.begin, token.expanded); break;
            case DropToken: break;
            case PrefixToken:
                renamed.assign(unit.prefix);
                renamed.append(token.text, token.size);
                writer.Append(renamed, unit, token.begin, true);
                break;
            default: writer.Append(unit.memberNames[i], unit, token.begin, true); break;
            }
            offset = token.end;
        }
        writer.Append(source.data() + offset, source.size() - offset, unit, offset);
        writer.Newline(unit, source.size());
    }
};

}

void ModuleBundle::Translate(Error& error) const
{
    if (error.line <= 0 || static_cast<std::size_t>(error.line) > lines.size()) return;
    // VM이 붙인 위치 표기도 모듈 원본 행으로 되돌린다. 오류 본문의 임의 숫자는 바꾸지 않는다.
    if (error.message.find("\nScript Call Stack:") != std::string::npos)
    {
        std::size_t begin = 0;
        while (begin < error.message.size())
        {
            const std::size_t newline = error.message.find('\n', begin);
            const std::size_t end = newline == std::string::npos ? error.message.size() : newline;
            const std::size_t ip = error.message.rfind(" : IP(", end);
            const std::size_t marker = ip != std::string::npos && ip >= begin ? error.message.find(", Line(", ip) : std::string::npos;
            if (marker != std::string::npos && marker < end)
            {
                const std::size_t first = marker + 7;
                char* last = nullptr;
                const unsigned long sourceLine = std::strtoul(error.message.c_str() + first, &last, 10);
                if (last != error.message.c_str() + first && *last == ')' && sourceLine > 0 && sourceLine <= lines.size())
                {
                    const std::size_t count = static_cast<std::size_t>(last - (error.message.c_str() + first));
                    error.message.replace(first, count, std::to_string(lines[sourceLine - 1].line));
                }
            }
            const std::size_t next = error.message.find('\n', begin);
            if (next == std::string::npos) break;
            begin = next + 1;
        }
    }
    const ModuleSourceLine& location = lines[static_cast<std::size_t>(error.line - 1)];
    error.sourceName = location.sourceName;
    error.line = location.line;
    for (std::vector<ModuleSourceColumn>::const_reverse_iterator it = location.columns.rbegin(); it != location.columns.rend(); ++it)
    {
        if (error.column < it->generatedColumn) continue;
        error.column = it->sourceColumn + (it->fixed ? 0 : error.column - it->generatedColumn);
        break;
    }
}

bool BundleModules(const std::string& source, const std::string& sourceName,
    const ModuleLoader& loader, ModuleBundle& result, Error& error,
    const std::vector<std::string>& externalNames, const std::vector<CompileDefine>* defines, std::size_t diagnosticLimit,
    const std::string* privateEntrySource)
{
    result = ModuleBundle();
    error = Error();
    try
    {
        Bundler bundler(loader, externalNames, defines, diagnosticLimit, privateEntrySource);
        bundler.Build(source, sourceName, result);
        return true;
    }
    catch (const Diagnostic& failure)
    {
        error = failure.error;
    }
    catch (const std::exception& exception)
    {
        error.sourceName = sourceName;
        error.message = std::string("module loader failed: ") + exception.what();
    }
    result = ModuleBundle();
    return false;
}

}
