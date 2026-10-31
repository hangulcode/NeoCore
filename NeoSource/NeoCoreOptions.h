#pragma once

#include "NeoHttp.h"
#include <cstdint>
#include <functional>
#include <string>

namespace NeoCore
{
struct Error;
struct ModuleSource
{
    std::string source;
    std::string sourceName;
};
typedef std::function<bool(const std::string&, const std::string&, ModuleSource&, Error&)> ModuleLoader;

typedef NeoScript::HttpRequest HttpRequest;
typedef NeoScript::HttpResponse HttpResponse;
typedef NeoScript::IHttpTransport IHttpTransport;

struct CompileOptions
{
    ModuleLoader moduleLoader;
    std::shared_ptr<IHttpTransport> httpTransport;
    bool emitAsm = false;
};

struct ExecutionLimits
{
    std::int32_t timeoutMs = -1;
    std::uint32_t instructionBudget = 0;
};
}
