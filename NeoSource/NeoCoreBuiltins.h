#pragma once

#include "NeoCoreTypes.h"

namespace NeoCore
{

// 이 표에 등록한 라이브러리 경계만 정적 프런트엔드에 공개한다.
struct BuiltinSignature
{
    const char* owner;
    const char* name;
    char result;
    unsigned minimum;
    unsigned maximum;
    const char* parameters;
};

inline TypeId BuiltinSignatureType(TypeRegistry& types, char code)
{
    switch (code)
    {
    case 'a': return types.LookupNamed("AsyncRequest");
    case 'b': return types.Bool();
    case 'f': return types.Float();
    case 'h':
    {
        std::vector<TypeId> parameters;
        parameters.push_back(types.Bool()); parameters.push_back(types.String());
        return types.FunctionOf(types.Void(), parameters);
    }
    case 'i': return types.Int();
    case 'l': return types.ListOf(types.String());
    case 'm': return types.MapOf(types.String(), types.Int());
    case 'p': return types.LookupNamed("TimeSpan");
    case 'r': return types.NullableOf(types.ListOf(types.String()));
    case 's': return types.String();
    case 'v': return types.Void();
    case 'w': return types.LookupNamed("Stopwatch");
    default: return InvalidType;
    }
}

inline const BuiltinSignature* GetBuiltinSignatures(std::size_t& count)
{
    static const BuiltinSignature signatures[] = {
        {"string", "sub", 's', 2, 2, "ii"},
        {"string", "codepoint", 'i', 1, 1, "i"},
        {"string", "find", 'i', 1, 1, "s"},
        {"string", "upper", 's', 0, 0, ""},
        {"string", "lower", 's', 0, 0, ""},
        {"string", "trim", 's', 0, 0, ""},
        {"string", "ltrim", 's', 0, 0, ""},
        {"string", "rtrim", 's', 0, 0, ""},
        {"string", "replace", 's', 2, 2, "ss"},
        {"string", "replaceAll", 's', 2, 2, "ss"},
        {"string", "split", 'l', 1, 1, "s"},
        {"string", "regex_match", 'b', 1, 2, "sb"},
        {"string", "regex_search", 'r', 1, 2, "sb"},
        {"string", "regex_replace", 's', 2, 3, "ssb"},
        {"TimeSpan", "total_milliseconds", 'i', 0, 0, ""},
        {"TimeSpan", "total_seconds", 'f', 0, 0, ""},
        {"TimeSpan", "parts", 'm', 0, 0, ""},
        {"TimeSpan", "format", 's', 0, 0, ""},
        {"TimeSpan", "add", 'p', 1, 1, "p"},
        {"TimeSpan", "subtract", 'p', 1, 1, "p"},
        {"TimeSpan", "compare", 'i', 1, 1, "p"},
        {"Stopwatch", "elapsed", 'p', 0, 0, ""},
        {"Stopwatch", "elapsed_ms", 'i', 0, 0, ""},
        {"Stopwatch", "elapsed_us", 'i', 0, 0, ""},
        {"Stopwatch", "elapsed_seconds", 'f', 0, 0, ""},
        {"Stopwatch", "start", 'v', 0, 0, ""},
        {"Stopwatch", "stop", 'v', 0, 0, ""},
        {"Stopwatch", "reset", 'v', 0, 0, ""},
        {"Stopwatch", "restart", 'v', 0, 0, ""},
        {"Stopwatch", "is_running", 'b', 0, 0, ""},
        {"AsyncRequest", "get", 'v', 3, 3, "ish"},
        {"AsyncRequest", "post", 'v', 4, 4, "issh"},
        {"AsyncRequest", "add_header", 'v', 2, 2, "ss"},
        {"AsyncRequest", "wait", 'b', 0, 0, ""},
        {"AsyncRequest", "close", 'v', 0, 0, ""},
        {"math", "sin", 'f', 1, 1, "f"},
        {"math", "cos", 'f', 1, 1, "f"},
        {"math", "sqrt", 'f', 1, 1, "f"},
        {"math", "abs", 'f', 1, 1, "f"},
        {"math", "floor", 'f', 1, 1, "f"},
        {"math", "ceil", 'f', 1, 1, "f"},
        {"math", "pow", 'f', 2, 2, "ff"},
        {"system", "clock", 'f', 0, 0, ""},
        {"system", "time", 'i', 0, 0, ""},
        {"system", "time_ms", 'i', 0, 0, ""},
        {"system", "utc_date", 's', 2, 2, "si"},
        {"system", "utc_time_ms", 'i', 7, 7, "iiiiiii"},
        {"system", "utc_parts", 'm', 1, 1, "i"},
        {"system", "utc_format_ms", 's', 1, 1, "i"},
        {"system", "utc_parse_ms", 'i', 1, 1, "s"},
        {"system", "time_span", 'p', 5, 5, "iiiii"},
        {"system", "time_span_ms", 'p', 1, 1, "i"},
        {"system", "time_span_seconds", 'p', 1, 1, "f"},
        {"system", "stopwatch", 'w', 0, 0, ""},
        {"system", "async_create", 'a', 0, 0, ""},
        {"system", "aysnc_create", 'a', 0, 0, ""}
    };
    count = sizeof(signatures) / sizeof(signatures[0]);
    return signatures;
}

inline const BuiltinSignature* FindBuiltinSignature(const std::string& owner, const std::string& name)
{
    std::size_t count = 0;
    const BuiltinSignature* signatures = GetBuiltinSignatures(count);
    for (std::size_t i = 0; i < count; ++i)
        if (owner == signatures[i].owner && name == signatures[i].name) return &signatures[i];
    return nullptr;
}

}
