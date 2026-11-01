#pragma once

#include "NeoHttp.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace NeoCore
{
struct Error;
struct ScriptHostCompileConfig;
struct ModuleSource
{
    std::string source;
    std::string sourceName;
};
typedef std::function<bool(const std::string&, const std::string&, ModuleSource&, Error&)> ModuleLoader;

typedef NeoScript::HttpRequest HttpRequest;
typedef NeoScript::HttpResponse HttpResponse;
typedef NeoScript::IHttpTransport IHttpTransport;

enum class CompileDefineKind { Identifier, Int, Float, String, Bool, Null };
struct CompileDefine
{
    std::string name;
    CompileDefineKind kind = CompileDefineKind::Int;
    std::string text;
    CompileDefine() = default;
    CompileDefine(const std::string& defineName, CompileDefineKind defineKind, const std::string& literal)
        : name(defineName), kind(defineKind), text(literal) {}
};

struct CompileOptions
{
    ModuleLoader moduleLoader;
    std::shared_ptr<IHttpTransport> httpTransport;
    bool emitAsm = false;
    std::vector<CompileDefine> defines;
    std::size_t diagnosticLimit = 100;
    std::shared_ptr<const ScriptHostCompileConfig> scriptHost;
};

struct ExecutionLimits
{
    std::int32_t timeoutMs = -1;
    std::uint32_t instructionBudget = 0;
};
}
