#include "NeoCoreModules.h"

#include "NeoCoreCompiler.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

namespace NeoCore
{
namespace
{

struct Token
{
    std::string text;
    std::size_t begin = 0, end = 0;
    int line = 1, column = 1;
    bool identifier = false;
    bool expanded = false;
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

// 문자열·주석을 그대로 두고 식별자 경계만 읽는다. 문법과 타입 판정은 정적 컴파일러의 일이다.
static std::vector<Token> Tokenize(const std::string& source, const std::string& name)
{
    std::vector<Token> tokens;
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
        token.text = source.substr(token.begin, token.end - token.begin);
        tokens.push_back(token);
    }
    return tokens;
}

struct Import
{
    std::string name, alias;
    std::size_t first = 0, last = 0;
    std::size_t target = 0;
    bool builtin = false;
};

struct Unit
{
    ModuleSource input;
    std::vector<Token> tokens;
    std::vector<std::size_t> lineStarts;
    std::vector<Import> imports;
    std::map<std::string, bool> symbols;
    std::set<std::size_t> fieldNames, strippedExports;
    std::map<std::size_t, std::string> replacements;
    std::string prefix;
    bool visiting = false, visited = false, emitted = false;
};

static bool At(const std::vector<Token>& tokens, std::size_t index, const char* value)
{
    return index < tokens.size() && tokens[index].text == value;
}

// 타입의 모양만 건너뛰며 지원 여부는 합친 소스의 타입 검사에서 결정한다.
static bool SkipType(const std::vector<Token>& tokens, std::size_t& index, unsigned depth = 0)
{
    if (depth > 128 || index >= tokens.size() || !tokens[index].identifier) return false;
    const std::string type = tokens[index++].text;
    // 다른 모듈의 공개 구조체는 alias.Type 으로 쓴다. 제네릭 인자와 함수 타입 안에서도 같은 규칙이다.
    if (type != "fn" && At(tokens, index, ".") && index + 1 < tokens.size() && tokens[index + 1].identifier) index += 2;
    if (type == "fn")
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
    unit.tokens = Tokenize(unit.input.source, unit.input.sourceName);
    if (defines)
    {
        std::vector<StaticDefineReplacement> replacements;
        std::string error;
        Token at;
        if (!ExpandStaticDefines(unit.input.source, *defines, replacements, error, at.line, at.column))
            Fail(unit.input.sourceName, at, error);
        std::vector<Token> expanded;
        std::size_t replacement = 0;
        for (const Token& original : unit.tokens)
        {
            if (replacement == replacements.size() || replacements[replacement].begin != original.begin)
            { expanded.push_back(original); continue; }
            std::vector<Token> values = Tokenize(replacements[replacement++].text, unit.input.sourceName);
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
    unit.lineStarts.push_back(unit.input.source.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0);
    for (std::size_t i = 0; i < unit.input.source.size(); ++i)
        if (unit.input.source[i] == '\n') unit.lineStarts.push_back(i + 1);
    const std::vector<Token>& tokens = unit.tokens;
    int braces = 0, parentheses = 0, brackets = 0;
    std::set<std::string> aliases;
    std::vector<std::pair<std::string, std::size_t>> delimiters;
    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        if (!braces && !parentheses && !brackets)
        {
            if (tokens[i].text == "import")
            {
                if (i && tokens[i - 1].text != ";" && tokens[i - 1].text != "}")
                    Fail(unit.input.sourceName, tokens[i], "import is only allowed at file scope");
                Import import;
                import.first = i;
                if (i + 2 >= tokens.size() || !tokens[i + 1].identifier)
                    Fail(unit.input.sourceName, tokens[i], "expected a module name after import");
                import.name = import.alias = tokens[i + 1].text;
                std::size_t end = i + 2;
                if (At(tokens, end, "as"))
                {
                    if (end + 1 >= tokens.size() || !tokens[end + 1].identifier)
                        Fail(unit.input.sourceName, tokens[end], "expected a module alias");
                    import.alias = tokens[end + 1].text;
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
                const std::string modifier = tokens[name].text;
                if (modifier != "public" && modifier != "export" && modifier != "const") break;
                bool& seen = modifier == "public" ? publicSymbol : modifier == "export" ? exported : constant;
                if (seen) Fail(unit.input.sourceName, tokens[name], "duplicate declaration modifier");
                seen = true;
                if (modifier == "export") exports.push_back(name);
                ++name;
            }
            if ((publicSymbol || exported || constant) && i && tokens[i - 1].text != ";" && tokens[i - 1].text != "}")
                Fail(unit.input.sourceName, tokens[i], "public, export and const are only allowed at file scope");
            if (exported && constant) Fail(unit.input.sourceName, tokens[i], "export const is not supported");
            if (At(tokens, name, "struct") && name + 2 < tokens.size() && tokens[name + 1].identifier && At(tokens, name + 2, "{"))
            {
                unit.symbols[tokens[name + 1].text] = publicSymbol;
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
                            const std::string& text = tokens[field].text;
                            if (text == "(" || text == "[" || text == "{") ++nesting;
                            else if (text == ")" || text == "]" || text == "}") --nesting;
                            else if (text == ";" && nesting == 0) break;
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
                unit.symbols[tokens[name].text] = publicSymbol;
                unit.strippedExports.insert(exports.begin(), exports.end());
                i = name;
            }
        }
        const std::string delimiter = tokens[i].text;
        if (delimiter == "{" || delimiter == "(" || delimiter == "[")
            delimiters.push_back(std::make_pair(delimiter, i));
        else if (delimiter == "}" || delimiter == ")" || delimiter == "]")
        {
            const std::string expected = delimiter == "}" ? "{" : delimiter == ")" ? "(" : "[";
            if (delimiters.empty() || delimiters.back().first != expected)
                Fail(unit.input.sourceName, tokens[i], "unmatched delimiter in module source");
            delimiters.pop_back();
        }
        if (tokens[i].text == "{") ++braces;
        else if (tokens[i].text == "}") --braces;
        else if (tokens[i].text == "(") ++parentheses;
        else if (tokens[i].text == ")") --parentheses;
        else if (tokens[i].text == "[") ++brackets;
        else if (tokens[i].text == "]") --brackets;
    }
    if (!delimiters.empty())
        Fail(unit.input.sourceName, tokens[delimiters.back().second], "unclosed delimiter in module source");
}

class Writer
{
public:
    explicit Writer(ModuleBundle& output) : output_(output) {}

    void Append(const std::string& text, const Unit& unit, std::size_t originalOffset, bool fixed = false)
    {
        if (text.empty()) return;
        std::size_t sourceLine = static_cast<std::size_t>(std::upper_bound(unit.lineStarts.begin(), unit.lineStarts.end(), originalOffset) - unit.lineStarts.begin());
        if (!sourceLine) sourceLine = 1;
        int sourceColumn = static_cast<int>(originalOffset - unit.lineStarts[sourceLine - 1]) + 1;
        bool mapping = true;
        for (char character : text)
        {
            if (newLine_)
            {
                ModuleSourceLine line;
                line.sourceName = unit.input.sourceName;
                line.line = static_cast<int>(sourceLine);
                output_.lines.push_back(line);
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
                output_.lines.back().columns.push_back(location);
                mapping = false;
            }
            output_.source += character;
            if (character == '\n')
            {
                newLine_ = true;
                if (!fixed) { ++sourceLine; sourceColumn = 1; }
            }
            else
            {
                ++column_;
                if (!fixed) ++sourceColumn;
            }
        }
    }

    void Newline(const Unit& unit, std::size_t offset)
    {
        if (!output_.source.empty() && output_.source.back() != '\n') Append("\n", unit, offset, true);
    }

    void Finish(const Unit& unit)
    {
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
    bool newLine_ = true;
    int column_ = 1;
};

class Bundler
{
public:
    Bundler(const ModuleLoader& loader, const std::vector<std::string>& externalNames, const std::vector<CompileDefine>* defines)
        : loader_(loader), defines_(defines)
    {
        static const char* const words[] = {
            "var", "fun", "int", "float", "bool", "string", "void", "int32", "float32", "any",
            "null", "true", "false", "if", "else", "while", "for", "foreach", "in", "return",
            "break", "continue", "import", "public", "export", "const", "struct", "class",
            "switch", "case", "default", "print", "tostring", "toint", "tofloat", "tosize",
            "sleep", "yield", "list", "map", "array", "array32", "set", "fn", "Vector2", "Vector3",
            "Vector4", "Quaternion", "TimeSpan", "Stopwatch", "AsyncRequest", "Coroutine", "math", "system", "coroutine"
        };
        preserved_.insert(words, words + sizeof(words) / sizeof(words[0]));
        preserved_.insert(externalNames.begin(), externalNames.end());
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
        std::string prefix = "__ncmodule_";
        for (;;)
        {
            bool collision = false;
            for (const std::unique_ptr<Unit>& unit : units_)
                for (const Token& token : unit->tokens)
                    if (token.identifier && token.text.compare(0, prefix.size(), prefix) == 0) collision = true;
            if (!collision) break;
            prefix += "x";
        }
        for (std::size_t i = 1; i < units_.size(); ++i) units_[i]->prefix = prefix + std::to_string(i) + "_";
        for (std::size_t i = 1; i < units_.size(); ++i)
            for (const Token& token : units_[i]->tokens)
                if (token.identifier && !preserved_.count(token.text))
                    output.symbolNames[units_[i]->prefix + token.text] = token.text;
        for (std::size_t i = 0; i < units_.size(); ++i) Rewrite(i);
        Writer writer(output);
        Emit(0, writer);
        writer.Finish(*units_[0]);
    }

private:
    const ModuleLoader& loader_;
    const std::vector<CompileDefine>* defines_;
    std::vector<std::unique_ptr<Unit>> units_;
    std::map<std::string, std::size_t> identities_;
    std::set<std::string> preserved_;

    void Visit(std::size_t index, unsigned depth)
    {
        Unit& unit = *units_[index];
        if (depth > 128) Fail(unit.input.sourceName, Token(), "module import nesting exceeds compiler limit");
        unit.visiting = true;
        for (Import& import : unit.imports)
        {
            if (import.builtin) continue;
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
            loaded->input = std::move(input);
            Analyze(*loaded, defines_);
            if (!loaded->tokens.empty() && loaded->tokens.back().text != ";" && loaded->tokens.back().text != "}")
                Fail(loaded->input.sourceName, loaded->tokens.back(), "incomplete statement at end of module source");
            import.target = units_.size();
            identities_[loaded->input.sourceName] = import.target;
            units_.push_back(std::move(loaded));
            Visit(import.target, depth + 1);
        }
        unit.visiting = false;
        unit.visited = true;
    }

    void Rewrite(std::size_t index)
    {
        Unit& unit = *units_[index];
        std::map<std::string, const Import*> aliases;
        std::set<std::size_t> importTokens;
        for (const Import& import : unit.imports)
        {
            if (!import.builtin && preserved_.count(import.alias))
                Fail(unit.input.sourceName, unit.tokens[import.first], "module alias conflicts with a built-in or host name: " + import.alias);
            if (!aliases.count(import.alias)) aliases[import.alias] = &import;
            for (std::size_t i = import.first; i <= import.last; ++i)
            {
                importTokens.insert(i);
                if (!import.builtin) unit.replacements[i] = "";
            }
        }
        for (std::size_t i = 0; i < unit.tokens.size(); ++i)
        {
            const Token& token = unit.tokens[i];
            if (importTokens.count(i) || !token.identifier || unit.fieldNames.count(i) || (i && At(unit.tokens, i - 1, "."))) continue;
            const std::map<std::string, const Import*>::const_iterator imported = aliases.find(token.text);
            if (imported != aliases.end())
            {
                const Import& import = *imported->second;
                if (i < import.last) Fail(unit.input.sourceName, token, "module must be imported before use: " + token.text);
                if (import.builtin && token.text == "coroutine" && At(unit.tokens, i + 1, "<")) continue;
                if (!At(unit.tokens, i + 1, ".") || i + 2 >= unit.tokens.size() || !unit.tokens[i + 2].identifier)
                    Fail(unit.input.sourceName, token, "a module alias cannot be hidden or used as a value: " + token.text);
                if (import.builtin) continue;
                const Unit& target = *units_[import.target];
                const Token& member = unit.tokens[i + 2];
                const std::map<std::string, bool>::const_iterator symbol = target.symbols.find(member.text);
                if (symbol == target.symbols.end()) Fail(unit.input.sourceName, member, "module has no member: " + member.text);
                if (!symbol->second) Fail(unit.input.sourceName, member, "module member is not public: " + member.text);
                unit.replacements[i] = target.prefix + member.text;
                unit.replacements[i + 1] = "";
                unit.replacements[i + 2] = "";
                i += 2;
                continue;
            }
            if ((token.text == "math" || token.text == "system" || token.text == "coroutine") && At(unit.tokens, i + 1, "."))
                Fail(unit.input.sourceName, token, token.text + " must be imported before use");
            if (index && unit.strippedExports.count(i)) unit.replacements[i] = "";
            else if (index && !preserved_.count(token.text)) unit.replacements[i] = unit.prefix + token.text;
        }
    }

    void Emit(std::size_t index, Writer& writer)
    {
        Unit& unit = *units_[index];
        if (unit.emitted) return;
        unit.emitted = true;
        std::map<std::size_t, const Import*> imports;
        for (const Import& import : unit.imports) if (!import.builtin) imports[import.first] = &import;
        std::size_t offset = unit.input.source.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
        for (std::size_t i = 0; i < unit.tokens.size(); ++i)
        {
            const Token& token = unit.tokens[i];
            writer.Append(unit.input.source.substr(offset, token.begin - offset), unit, offset);
            const std::map<std::size_t, const Import*>::const_iterator import = imports.find(i);
            if (import != imports.end())
            {
                writer.Newline(unit, token.begin);
                writer.Append("{}\n", unit, token.begin, true);
                Emit(import->second->target, writer);
                writer.Newline(unit, token.begin);
                writer.Append("{}\n", unit, token.begin, true);
            }
            const std::map<std::size_t, std::string>::const_iterator replacement = unit.replacements.find(i);
            if (replacement == unit.replacements.end()) writer.Append(token.text, unit, token.begin, token.expanded);
            else writer.Append(replacement->second, unit, token.begin, true);
            offset = token.end;
        }
        writer.Append(unit.input.source.substr(offset), unit, offset);
        writer.Newline(unit, unit.input.source.size());
    }
};

}

void ModuleBundle::Translate(Error& error) const
{
    if (error.line <= 0 || static_cast<std::size_t>(error.line) > lines.size()) return;
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
    const std::vector<std::string>& externalNames, const std::vector<CompileDefine>* defines)
{
    result = ModuleBundle();
    error = Error();
    try
    {
        Bundler bundler(loader, externalNames, defines);
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
