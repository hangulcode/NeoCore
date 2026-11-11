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
    case '2': return types.Vector2();
    case '3': return types.Vector3();
    case '4': return types.Vector4();
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
    case 'q': return types.Quaternion();
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
        {"math", "abs", 'f', 1, 1, "f"},
        {"math", "acos", 'f', 1, 1, "f"},
        {"math", "asin", 'f', 1, 1, "f"},
        {"math", "atan", 'f', 1, 1, "f"},
        {"math", "ceil", 'f', 1, 1, "f"},
        {"math", "floor", 'f', 1, 1, "f"},
        {"math", "round", 'f', 1, 1, "f"},
        {"math", "sin", 'f', 1, 1, "f"},
        {"math", "cos", 'f', 1, 1, "f"},
        {"math", "tan", 'f', 1, 1, "f"},
        {"math", "log", 'f', 1, 1, "f"},
        {"math", "log10", 'f', 1, 1, "f"},
        {"math", "exp", 'f', 1, 1, "f"},
        {"math", "pow", 'f', 2, 2, "ff"},
        {"math", "deg", 'f', 1, 1, "f"},
        {"math", "rad", 'f', 1, 1, "f"},
        {"math", "sqrt", 'f', 1, 1, "f"},
        {"math", "atan2", 'f', 2, 2, "ff"},
        {"math", "hypot", 'f', 2, 2, "ff"},
        {"math", "fmod", 'f', 2, 2, "ff"},
        {"math", "trunc", 'f', 1, 1, "f"},
        {"math", "fract", 'f', 1, 1, "f"},
        {"math", "sinh", 'f', 1, 1, "f"},
        {"math", "cosh", 'f', 1, 1, "f"},
        {"math", "tanh", 'f', 1, 1, "f"},
        {"math", "Sign", 'f', 1, 1, "f"},
        {"math", "Min", 'f', 2, 2, "ff"},
        {"math", "Max", 'f', 2, 2, "ff"},
        {"math", "Vector2", '2', 2, 2, "ff"},
        {"math", "Vector3", '3', 3, 3, "fff"},
        {"math", "Vector4", '4', 4, 4, "ffff"},
        {"math", "Quaternion", 'q', 4, 4, "ffff"},
        {"math", "Clamp01", 'f', 1, 1, "f"},
        {"math", "Clamp", 'f', 3, 3, "fff"},
        {"math", "SmoothStep01", 'f', 1, 1, "f"},
        {"math", "Lerp", 'f', 3, 3, "fff"},
        {"math", "Lerp3", '3', 3, 3, "33f"},
        {"math", "Repeat", 'f', 2, 2, "ff"},
        {"math", "PingPong", 'f', 2, 2, "ff"},
        {"math", "WrapAngle", 'f', 1, 1, "f"},
        {"math", "DeltaAngle", 'f', 2, 2, "ff"},
        {"math", "LerpAngle", 'f', 3, 3, "fff"},
        {"math", "MoveToward", 'f', 3, 3, "fff"},
        {"math", "MoveTowardAngle", 'f', 3, 3, "fff"},
        {"math", "InverseLerp", 'f', 3, 3, "fff"},
        {"math", "Remap", 'f', 5, 5, "fffff"},
        {"math", "SmoothStep", 'f', 3, 3, "fff"},
        {"math", "Distance2", 'f', 4, 4, "ffff"},
        {"math", "Dot3", 'f', 2, 2, "33"},
        {"math", "Length3", 'f', 1, 1, "3"},
        {"math", "Distance3", 'f', 2, 2, "33"},
        {"math", "DistanceSquared3", 'f', 2, 2, "33"},
        {"math", "Normalize3", '3', 2, 2, "33"},
        {"math", "Cross3", '3', 2, 2, "33"},
        {"math", "RotateVectorByQuat", '3', 2, 2, "q3"},
        {"math", "quat_from_basis", 'q', 3, 3, "333"},
        {"math", "quat_slerp", 'q', 3, 3, "qqf"},
        {"math", "srand", 'v', 1, 1, "i"},
        {"math", "rand", 'i', 0, 0, ""},
        {"math", "tostr", 's', 1, 1, "f"},
        {"math", "fixed", 's', 2, 2, "fi"},
        {"math", "Rand01", 'f', 0, 0, ""},
        {"math", "RandRange", 'f', 2, 2, "ff"},
        {"math", "Hash32", 'i', 1, 1, "i"},
        {"math", "ColorRGB", 'i', 3, 3, "fff"},
        {"math", "ColorARGB", 'i', 4, 4, "ffff"},
        {"system", "clock", 'f', 0, 0, ""},
        {"system", "time", 'i', 0, 0, ""},
        {"system", "time_ms", 'i', 0, 0, ""},
        {"system", "date", 's', 2, 2, "si"},
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
