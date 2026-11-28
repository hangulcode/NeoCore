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
typedef std::function<std::string(const std::string& name, const std::string& importer)> ModuleCacheKey;

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

// Post는 다른 스레드에 작업을 제출한다. 제출한 스레드에서 실행하거나 완료를 기다리지 않는다.
class IJobExecutor
{
public:
    virtual ~IJobExecutor() {}
    virtual unsigned Concurrency() const = 0;
    virtual void Post(std::function<void()> task) = 0;
};

struct CompileOptions
{
    ModuleLoader moduleLoader;
    std::shared_ptr<IJobExecutor> jobExecutor;
    std::shared_ptr<IHttpTransport> httpTransport;
    bool emitAsm = false;
    std::vector<CompileDefine> defines;
    std::size_t diagnosticLimit = 100;
    std::shared_ptr<const ScriptHostCompileConfig> scriptHost;
    // 한 번의 컴파일에서 같은 키는 같은 모듈을 뜻한다. 빈 키는 매번 로더로 확인한다.
    ModuleCacheKey moduleCacheKey;
};

struct ExecutionLimits
{
    std::int32_t timeoutMs = -1;
    std::uint32_t instructionBudget = 0;
};
}
