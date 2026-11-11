#include "NeoCoreApi.h"

#include <cassert>
#include <unordered_map>

namespace NeoCore
{

namespace
{

struct ApiTableEntry
{
    const char* owner;
    const char* declaration;
    std::uint32_t flags;
};

// 본체 열은 런타임(NeoLib.cpp)이 같은 표에서 따로 읽는다. 여기서는 서명만 쓴다.
const ApiTableEntry apiTable[] = {
#define NEOCORE_API(owner, declaration, flags, body) { owner, declaration, static_cast<std::uint32_t>(flags) },
#include "NeoCoreApiTable.inc"
#undef NEOCORE_API
};

bool IdentifierStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'; }
bool IdentifierPart(char c) { return IdentifierStart(c) || (c >= '0' && c <= '9'); }

struct Registry
{
    std::vector<ApiDeclaration> declarations;
    std::vector<ApiTypeNode> nodes;
    std::unordered_map<std::string, int> keys;

    // 선언 문자열 해석기. 표는 소스에 고정되어 있으므로 잘못된 선언은 개발 중에 assert 로 드러난다.
    struct Reader
    {
        Registry& registry;
        const char* text;
        std::size_t at;

        void Space() { while (text[at] == ' ') ++at; }
        bool Take(char c) { Space(); if (text[at] != c) return false; ++at; return true; }
        bool TakeText(const char* word)
        {
            Space();
            std::size_t i = 0;
            while (word[i] && text[at + i] == word[i]) ++i;
            if (word[i]) return false;
            at += i;
            return true;
        }
        std::string Identifier()
        {
            Space();
            const std::size_t begin = at;
            if (IdentifierStart(text[at])) { ++at; while (IdentifierPart(text[at])) ++at; }
            return std::string(text + begin, at - begin);
        }

        std::uint32_t Add(const ApiTypeNode& node)
        {
            registry.nodes.push_back(node);
            return static_cast<std::uint32_t>(registry.nodes.size() - 1);
        }

        // 자식은 연속으로 놓여야 하므로 먼저 따로 모은 뒤 한꺼번에 붙인다.
        std::uint32_t Composite(ApiTypeKind kind, const std::string& name, const std::vector<ApiTypeNode>& children)
        {
            ApiTypeNode node;
            node.kind = kind;
            node.name = name;
            node.first = static_cast<std::uint32_t>(registry.nodes.size());
            node.count = static_cast<std::uint32_t>(children.size());
            for (const ApiTypeNode& child : children) registry.nodes.push_back(child);
            return Add(node);
        }

        ApiTypeNode Type()
        {
            const std::size_t begin = at;
            const std::string name = Identifier();
            assert(!name.empty());
            ApiTypeNode node;
            node.name = name;
            if (name == "fn")
            {
                const bool open = Take('(');
                assert(open); (void)open;
                std::vector<ApiTypeNode> parameters;
                if (!Take(')'))
                {
                    do { parameters.push_back(Type()); } while (Take(','));
                    const bool close = Take(')');
                    assert(close); (void)close;
                }
                const bool arrow = TakeText("->");
                assert(arrow); (void)arrow;
                std::vector<ApiTypeNode> children;
                children.push_back(Type());
                children.insert(children.end(), parameters.begin(), parameters.end());
                node.kind = ApiTypeKind::Function;
                node.first = static_cast<std::uint32_t>(registry.nodes.size());
                node.count = static_cast<std::uint32_t>(children.size());
                for (const ApiTypeNode& child : children) registry.nodes.push_back(child);
                return node;
            }
            std::vector<ApiTypeNode> arguments;
            if (Take('<'))
            {
                do { arguments.push_back(Type()); } while (Take(','));
                const bool close = Take('>');
                assert(close); (void)close;
            }
            ApiTypeKind kind = ApiTypeKind::Open;
            if (arguments.empty())
            {
                if (name == "Self") kind = ApiTypeKind::Self;
                else if (name == "T") kind = ApiTypeKind::Element;
                else if (name == "K") kind = ApiTypeKind::Key;
                else if (name == "V") kind = ApiTypeKind::Value;
                else if (name == "void" || name == "int" || name == "float" || name == "bool" || name == "string" ||
                    name == "Vector2" || name == "Vector3" || name == "Vector4" || name == "Quaternion" ||
                    name == "TimeSpan" || name == "Stopwatch" || name == "AsyncRequest") kind = ApiTypeKind::Named;
            }
            else if (arguments.size() == 1 && name == "list") kind = ApiTypeKind::List;
            else if (arguments.size() == 1 && name == "array") kind = ApiTypeKind::Array;
            else if (arguments.size() == 1 && name == "array32") kind = ApiTypeKind::Array32;
            else if (arguments.size() == 1 && name == "set") kind = ApiTypeKind::Set;
            else if (arguments.size() == 2 && name == "map") kind = ApiTypeKind::Map;
            node.kind = kind;
            if (kind == ApiTypeKind::Open)
            {
                // 표시용으로 원문을 그대로 남긴다(coroutine<F>, scalar 등).
                node.name.assign(text + begin, at - begin);
                return node;
            }
            node.first = static_cast<std::uint32_t>(registry.nodes.size());
            node.count = static_cast<std::uint32_t>(arguments.size());
            for (const ApiTypeNode& child : arguments) registry.nodes.push_back(child);
            return node;
        }

        void Declaration(ApiDeclaration& declaration)
        {
            declaration.result = Add(Type());
            declaration.name = Identifier();
            assert(!declaration.name.empty());
            Space();
            if (text[at] == '<')
            {
                const std::size_t begin = at;
                while (text[at] && text[at] != '>') ++at;
                assert(text[at] == '>');
                ++at;
                declaration.typeParameters.assign(text + begin, at - begin);
            }
            const bool open = Take('(');
            assert(open); (void)open;
            bool optional = false;
            if (!Take(')'))
            {
                do
                {
                    if (TakeText("..."))
                    {
                        declaration.flags |= ApiVariadic;
                        continue;
                    }
                    ApiParameter parameter;
                    Space();
                    // out 은 타입 이름이 될 수 없으므로 뒤에 타입이 이어지면 방향 표식이다.
                    const std::size_t mark = at;
                    if (Identifier() == "out" && text[at] == ' ') parameter.mode = ApiParameterMode::Out;
                    else at = mark;
                    parameter.type = Add(Type());
                    parameter.name = Identifier();
                    assert(!parameter.name.empty());
                    if (Take('?')) { parameter.optional = true; optional = true; }
                    else
                    {
                        // 생략 가능한 매개변수는 뒤쪽에만 올 수 있다.
                        assert(!optional);
                        declaration.minimum = declaration.parameters.size() + 1;
                    }
                    if (parameter.mode == ApiParameterMode::Out)
                    {
                        assert(declaration.parameters.size() < 32 && !parameter.optional);
                        declaration.outMask |= 1u << declaration.parameters.size();
                    }
                    declaration.parameters.push_back(parameter);
                } while (Take(','));
                const bool close = Take(')');
                assert(close); (void)close;
            }
            Space();
            assert(text[at] == 0);
        }
    };

    Registry()
    {
        const std::size_t count = sizeof(apiTable) / sizeof(apiTable[0]);
        declarations.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            ApiDeclaration declaration;
            declaration.index = static_cast<std::uint32_t>(i);
            declaration.owner = apiTable[i].owner;
            declaration.flags = apiTable[i].flags;
            Reader reader = { *this, apiTable[i].declaration, 0 };
            reader.Declaration(declaration);
            declaration.key = declaration.owner.empty() ? declaration.name : declaration.owner + "." + declaration.name;
            const bool unique = keys.insert(std::make_pair(declaration.key, static_cast<int>(i))).second;
            assert(unique); (void)unique;
            declarations.push_back(declaration);
        }
    }
};

const Registry& GetRegistry()
{
    static const Registry registry;
    return registry;
}

}

const std::vector<ApiDeclaration>& CoreApi() { return GetRegistry().declarations; }

const ApiDeclaration* FindCoreApi(const std::string& owner, const std::string& name)
{
    const int index = FindCoreApiIndex(owner.empty() ? name : owner + "." + name);
    return index < 0 ? nullptr : &GetRegistry().declarations[static_cast<std::size_t>(index)];
}

int FindCoreApiIndex(const std::string& key)
{
    const Registry& registry = GetRegistry();
    const auto found = registry.keys.find(key);
    return found == registry.keys.end() ? -1 : found->second;
}

bool IsCoreApiModule(const std::string& owner)
{
    return owner == "math" || owner == "system" || owner == "coroutine";
}

std::string CoreApiOwner(const TypeRegistry& types, TypeId receiver)
{
    const TypeDesc& type = types.Get(receiver);
    switch (type.kind)
    {
    case TypeKind::String: return "string";
    case TypeKind::Array:
    case TypeKind::Array32: return "array";
    case TypeKind::List: return "list";
    case TypeKind::Map: return "map";
    case TypeKind::Set: return "set";
    case TypeKind::BuiltinObject: return type.name;
    default: return std::string();
    }
}

TypeId ResolveApiType(TypeRegistry& types, std::uint32_t index, TypeId receiver)
{
    const ApiTypeNode& node = GetRegistry().nodes[index];
    const TypeDesc& owner = types.Get(receiver);
    switch (node.kind)
    {
    case ApiTypeKind::Named: return types.LookupNamed(node.name);
    case ApiTypeKind::Self: return receiver;
    case ApiTypeKind::Element: return owner.element;
    case ApiTypeKind::Key: return owner.key;
    case ApiTypeKind::Value: return owner.value;
    case ApiTypeKind::List:
    case ApiTypeKind::Array:
    case ApiTypeKind::Array32:
    case ApiTypeKind::Set:
    {
        const TypeId element = ResolveApiType(types, node.first, receiver);
        if (element == InvalidType) return InvalidType;
        return node.kind == ApiTypeKind::List ? types.ListOf(element)
            : node.kind == ApiTypeKind::Array ? types.ArrayOf(element)
            : node.kind == ApiTypeKind::Array32 ? types.Array32Of(element) : types.SetOf(element);
    }
    case ApiTypeKind::Map:
    {
        const TypeId key = ResolveApiType(types, node.first, receiver);
        const TypeId value = ResolveApiType(types, node.first + 1, receiver);
        return key == InvalidType || value == InvalidType ? InvalidType : types.MapOf(key, value);
    }
    case ApiTypeKind::Function:
    {
        const TypeId result = ResolveApiType(types, node.first, receiver);
        if (result == InvalidType) return InvalidType;
        std::vector<TypeId> parameters;
        for (std::uint32_t i = 1; i < node.count; ++i)
        {
            parameters.push_back(ResolveApiType(types, node.first + i, receiver));
            if (parameters.back() == InvalidType) return InvalidType;
        }
        return types.FunctionOf(result, parameters);
    }
    default: return InvalidType;
    }
}

ApiCallSignature ResolveApiSignature(TypeRegistry& types, const ApiDeclaration& declaration, TypeId receiver)
{
    ApiCallSignature signature;
    signature.result = ResolveApiType(types, declaration.result, receiver);
    signature.minimum = declaration.minimum;
    signature.display = declaration.key;
    for (const ApiParameter& parameter : declaration.parameters)
    {
        signature.parameters.push_back(ResolveApiType(types, parameter.type, receiver));
        signature.modes.push_back(parameter.mode);
        signature.names.push_back(parameter.name);
    }
    return signature;
}

std::string FormatApiType(std::uint32_t index)
{
    const Registry& registry = GetRegistry();
    const ApiTypeNode& node = registry.nodes[index];
    if (node.kind == ApiTypeKind::Function)
    {
        std::string text = "fn(";
        for (std::uint32_t i = 1; i < node.count; ++i)
        {
            if (i > 1) text += ", ";
            text += FormatApiType(node.first + i);
        }
        return text + ") -> " + FormatApiType(node.first);
    }
    std::string text = node.name;
    if (node.kind != ApiTypeKind::Open && node.count)
    {
        text += "<";
        for (std::uint32_t i = 0; i < node.count; ++i)
        {
            if (i) text += ", ";
            text += FormatApiType(node.first + i);
        }
        text += ">";
    }
    return text;
}

std::string FormatApiParameter(const ApiParameter& parameter)
{
    std::string text = parameter.mode == ApiParameterMode::Out ? "out " : "";
    text += FormatApiType(parameter.type) + " " + parameter.name;
    if (parameter.optional) text += " (optional)";
    return text;
}

std::string FormatApiDeclaration(const ApiDeclaration& declaration)
{
    std::string text = FormatApiType(declaration.result) + " " + declaration.key + declaration.typeParameters + "(";
    for (std::size_t i = 0; i < declaration.parameters.size(); ++i)
    {
        if (i) text += ", ";
        text += FormatApiParameter(declaration.parameters[i]);
    }
    if (declaration.Is(ApiVariadic)) text += declaration.parameters.empty() ? "..." : ", ...";
    return text + ")";
}

}
