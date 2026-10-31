#include "NeoCore.h"
#include "NeoCoreModules.h"
#include "NeoCoreCompiler.h"
#include "NeoCoreHostRuntime.h"
#include "NeoCoreHostBridge.h"
#include "NeoScript.h"
#include <stdexcept>
#include <cmath>
#include <limits>
#include <utility>

namespace NeoCore
{

Scalar Scalar::Int(std::int64_t value) { Scalar s; s.kind_ = Kind::Int; s.integer_ = value; return s; }
Scalar Scalar::Float(double value) { Scalar s; s.kind_ = Kind::Float; s.number_ = value; return s; }
Scalar Scalar::Bool(bool value) { Scalar s; s.kind_ = Kind::Bool; s.integer_ = value; return s; }
Scalar Scalar::String(const std::string& value) { Scalar s; s.kind_ = Kind::String; s.string_ = value; return s; }
Scalar Scalar::Null() { Scalar s; s.kind_ = Kind::Null; return s; }
std::int64_t Scalar::asInt() const
{
    if (kind_ != Kind::Int) throw std::logic_error("NeoCore scalar is not int");
    return integer_;
}
double Scalar::asFloat() const
{
    if (kind_ != Kind::Float) throw std::logic_error("NeoCore scalar is not float");
    return number_;
}
bool Scalar::asBool() const
{
    if (kind_ != Kind::Bool) throw std::logic_error("NeoCore scalar is not bool");
    return integer_ != 0;
}
const std::string& Scalar::asString() const
{
    if (kind_ != Kind::String) throw std::logic_error("NeoCore scalar is not string");
    return string_;
}

AllocationStats GetAllocationStats()
{
    NeoScript::AllocStats backend;
    NeoScript::GetAllocStats(backend);
    AllocationStats result;
    result.strings = backend.strings;
    result.maps = backend.maps;
    result.lists = backend.lists;
    result.sets = backend.sets;
    result.coroutines = backend.coroutines;
    result.modules = backend.modules;
    result.asyncs = backend.asyncs;
    result.vectors = backend.vectors;
    result.arrays = backend.arrays;
    result.closures = backend.closures;
    result.bufferBytes = backend.bufferBytes;
    result.poolBytes = backend.poolBytes;
    result.stringIdleBytes = backend.stringIdleBytes;
    return result;
}

static Error TranslateError(const NeoScript::Error& backend, const std::string& name);

struct Program::Impl
{
    StaticCompileResult checked;
    ModuleBundle bundle;
    std::string sourceName;
    NeoScript::IRuntime* runtime = nullptr;
    NeoScript::ProgramHandle program;
    std::unique_ptr<HostRuntime> host;
    ~Impl()
    {
        if (!runtime) return;
        if (program) runtime->DestroyProgram(program);
        NeoScript::DestroyRuntime(runtime);
    }
    Error BackendError(const std::string& fallback)
    {
        Error error;
        error.sourceName = sourceName;
        NeoScript::StringView text;
        error.message = runtime->TakeLastError(text) ? text.str() : fallback;
        return error;
    }
};

static Result Failure(const std::string& message, const std::string& source)
{
    Result result;
    result.status = ExecutionStatus::Failed;
    result.error.message = message;
    result.error.sourceName = source;
    return result;
}

struct Instance::Impl
{
    explicit Impl(std::shared_ptr<Program::Impl> shared)
        : module(std::move(shared)), checked(module->checked), sourceName(module->sourceName), runtime(module->runtime) {}
    std::shared_ptr<Program::Impl> module;
    StaticCompileResult& checked;
    const std::string& sourceName;
    NeoScript::IRuntime* runtime;
    NeoScript::InstanceHandle instance;
    std::unique_ptr<HostInstance> host;
    bool initialized = false;
    bool initializing = false;
    bool suspended = false;
    unsigned executing = 0;
    Result initialization;
    NeoScript::Invocation pending;
    const StaticFunctionSignature* pendingSignature = nullptr;
    std::string pendingName;
    bool deferredCall = false;
    std::string deferredName;
    std::vector<Scalar> deferredArguments;
    ExecutionLimits deferredLimits;

    ~Impl()
    {
        // 대기 중 반환값 소유자를 먼저 반납한 뒤 실행 컨텍스트와 바인딩을 파괴한다.
        pending = NeoScript::Invocation();
        if (instance)
        {
            if (suspended) runtime->Cancel(instance);
            runtime->DestroyInstance(instance);
        }
    }
    Error BackendError(const std::string& fallback)
    {
        NeoScript::Error backend;
        if (instance && NeoCoreBackend::ReadLastCallError(*runtime, instance, backend))
        {
            Error error = TranslateError(backend, sourceName);
            module->bundle.Translate(error);
            return error;
        }
        return module->BackendError(fallback);
    }
    Result ReadResult(NeoScript::Invocation& call, const StaticFunctionSignature& signature, const std::string& name);
};

// 재진입 호출은 허용하되 실행 중인 인스턴스를 Resume으로 다시 진행하지 않는다.
struct ExecutionScope
{
    explicit ExecutionScope(unsigned& count) : count_(count) { ++count_; }
    ~ExecutionScope() { --count_; }
    unsigned& count_;
};
Program::Program(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
Program::~Program() = default;
Instance::Instance(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Instance::~Instance() = default;

static Error TranslateError(const NeoScript::Error& backend, const std::string& name)
{
    Error error;
    error.message = backend.message;
    error.sourceName = backend.sourceName.empty() ? name : backend.sourceName;
    error.line = static_cast<int>(backend.line);
    error.column = static_cast<int>(backend.column);
    return error;
}

std::unique_ptr<Program> Program::Compile(const std::string& source, Error& error,
    const std::string& sourceName, bool debug, const CompileOptions& options)
{
    return CompileImpl(source, error, sourceName, debug, nullptr, options);
}

std::unique_ptr<Program> Program::Compile(const std::string& source, const HostBindings& bindings,
    Error& error, const std::string& sourceName, bool debug, const CompileOptions& options)
{
    return CompileImpl(source, error, sourceName, debug, &bindings, options);
}

std::unique_ptr<Program> Program::CompileImpl(const std::string& source, Error& error,
    const std::string& sourceName, bool debug, const HostBindings* bindings, const CompileOptions& options)
{
    error = Error();
    std::shared_ptr<Impl> impl(new Impl());
    impl->sourceName = sourceName;
    if (bindings)
    {
        if (!bindings->Validate(error.message)) { error.sourceName = sourceName; return std::unique_ptr<Program>(); }
        impl->host.reset(new HostRuntime(*bindings));
    }
    std::vector<std::string> externalNames;
    if (bindings)
    {
        const HostSchema& schema = bindings->Schema();
        for (const auto& global : schema.Globals()) externalNames.push_back(global.name);
        for (const auto& type : schema.Types().NamedTypes())
            if (schema.Types().Get(type.second).kind == TypeKind::Native) externalNames.push_back(type.first);
    }
    if (!BundleModules(source, sourceName, options.moduleLoader, impl->bundle, error, externalNames))
        return std::unique_ptr<Program>();
    if (!CompileStaticSource(impl->bundle.source, impl->checked, impl->host ? &impl->host->Schema() : nullptr))
    {
        error.message = impl->checked.errorMessage;
        error.sourceName = sourceName;
        error.line = impl->checked.errorLine;
        error.column = impl->checked.errorColumn;
        impl->bundle.Translate(error);
        return std::unique_ptr<Program>();
    }
    NeoScript::RuntimeDesc runtimeDesc;
    runtimeDesc.httpTransport = options.httpTransport;
    impl->runtime = NeoScript::CreateRuntime(runtimeDesc);
    if (!impl->runtime)
    {
        error.message = "could not create NeoCore runtime";
        error.sourceName = sourceName;
        return std::unique_ptr<Program>();
    }
    if (impl->host && !impl->host->Link(*impl->runtime, impl->checked.hostPrefix, error.message))
    {
        error.sourceName = sourceName;
        return std::unique_ptr<Program>();
    }
    impl->runtime->FreezeBindings();
    NeoScript::CompileDesc desc;
    desc.source = impl->checked.loweredSource;
    desc.coreIntrinsicPrefix = impl->checked.backendIntrinsicPrefix;
    desc.coreTypedIR = &impl->checked.typedIR;
    desc.sourceName = sourceName;
    desc.includeDebugInfo = debug;
    desc.emitAsm = options.emitAsm;
    NeoScript::CompileResult compiled = impl->runtime->Compile(desc);
    impl->program = compiled.program;
    if (!impl->program || !compiled.error.ok())
    {
        error = TranslateError(compiled.error, sourceName);
        impl->bundle.Translate(error);
        if (error.ok()) error = impl->BackendError("backend compilation failed");
        return std::unique_ptr<Program>();
    }
    if (debug && !NeoCoreBackend::RemapSourceLines(*impl->runtime, impl->program, impl->checked.hostWriteSourceLines))
    {
        error.sourceName = sourceName;
        error.message = "could not map host property diagnostics to source";
        return std::unique_ptr<Program>();
    }
    std::unique_ptr<Program> result(new Program(std::move(impl)));
    result->defaultInstance_ = result->CreateInstance(error);
    if (!result->defaultInstance_) return std::unique_ptr<Program>();
    return result;
}

std::unique_ptr<Instance> Program::CreateInstance(Error& error)
{
    error = Error();
    std::unique_ptr<Instance::Impl> state(new Instance::Impl(impl_));
    std::unique_ptr<Instance> result(new Instance(std::move(state)));
    if (impl_->host) result->impl_->host = impl_->host->CreateInstance(*result);
    NeoScript::InstanceDesc desc;
    desc.runGlobalInit = false;
    desc.userData = result->impl_->host ? result->impl_->host->UserData() : nullptr;
    result->impl_->instance = impl_->runtime->CreateInstance(impl_->program, desc);
    if (!result->impl_->instance ||
        !NeoCoreBackend::EnableCooperativeSleep(*impl_->runtime, result->impl_->instance))
    {
        error = result->impl_->BackendError("could not create NeoCore instance");
        return std::unique_ptr<Instance>();
    }
    return result;
}
Result Program::Run() { return defaultInstance_->Run(); }
Result Program::Call(const std::string& name, const std::vector<Scalar>& arguments, const ExecutionLimits& limits)
{
    return defaultInstance_->Call(name, arguments, limits);
}
Result Program::Resume(double deltaTime, std::uint32_t budget) { return defaultInstance_->Resume(deltaTime, budget); }
bool Program::IsSuspended() const { return defaultInstance_->IsSuspended(); }
std::int64_t Program::TrimMemory(bool force) { return impl_->runtime->TrimMemory(force); }
void Program::SetEmptyPageHoldSeconds(float seconds) { impl_->runtime->SetEmptyPageHoldSeconds(seconds); }
void Program::SetTrimPagesPerCall(int pages) { impl_->runtime->SetTrimPagesPerCall(pages); }
bool Program::SetHostObject(const std::string& name, ObjectHandle object, Error& error)
{
    return defaultInstance_->SetHostObject(name, object, error);
}
bool Instance::SetHostObject(const std::string& name, ObjectHandle object, Error& error)
{
    error = Error();
    error.sourceName = impl_->sourceName;
    if (!impl_->host) { error.message = "program has no host bindings"; return false; }
    return impl_->host->SetGlobal(name, object, error.message);
}
bool Instance::IsSuspended() const { return impl_->suspended; }

Result Instance::Run()
{
    if (impl_->initializing)
    {
        if (impl_->suspended) return impl_->initialization;
        return Failure("script reentry is not allowed before global initialization completes", impl_->sourceName);
    }
    if (impl_->suspended) return Failure("instance has a suspended call; resume it before starting another call", impl_->sourceName);
    if (impl_->initialized) return impl_->initialization;
    impl_->initialized = true;
    impl_->initializing = true;
    ExecutionScope executing(impl_->executing);
    const NeoScript::RunStatus status = impl_->runtime->RunGlobalInit(impl_->instance);
    if (status == NeoScript::RunStatus::Suspended || status == NeoScript::RunStatus::Yielded)
    {
        impl_->suspended = true;
        impl_->initialization.status = ExecutionStatus::Suspended;
    }
    else
    {
        impl_->initializing = false;
        if (status != NeoScript::RunStatus::Completed)
        {
            impl_->initialization.status = ExecutionStatus::Failed;
            impl_->initialization.error = impl_->BackendError("global initialization did not complete");
        }
    }
    return impl_->initialization;
}

static TypeId ScalarType(const TypeRegistry& types, Scalar::Kind kind)
{
    switch (kind)
    {
    case Scalar::Kind::Void: return types.Void();
    case Scalar::Kind::Int: return types.Int();
    case Scalar::Kind::Float: return types.Float();
    case Scalar::Kind::Bool: return types.Bool();
    case Scalar::Kind::String: return types.String();
    case Scalar::Kind::Null: return types.Null();
    }
    return InvalidType;
}

Result Instance::Call(const std::string& name, const std::vector<Scalar>& arguments, const ExecutionLimits& limits)
{
    if (impl_->suspended) return Failure("instance has a suspended call; resume it before starting another call", impl_->sourceName);
    if (limits.timeoutMs < -1) return Failure("call timeout must be -1 or nonnegative", impl_->sourceName);
    Result result;
    result.status = ExecutionStatus::Failed;
    result.error.sourceName = impl_->sourceName;
    const StaticFunctionSignature* signature = nullptr;
    for (const auto& candidate : impl_->checked.functions)
        if (candidate.name == name && candidate.exported) { signature = &candidate; break; }
    if (!signature)
    {
        result.error.message = "exported function not found: " + name;
        return result;
    }
    if (signature->parameterTypes.size() != arguments.size())
    {
        result.error.message = "argument count mismatch for " + name;
        return result;
    }
    const TypeRegistry& types = impl_->checked.types;
    TypeId hostReturn = signature->returnType;
    if (types.IsNullable(hostReturn)) hostReturn = types.Get(hostReturn).element;
    const TypeKind hostKind = types.Get(hostReturn).kind;
    if (hostKind != TypeKind::Void && hostKind != TypeKind::Int && hostKind != TypeKind::Float &&
        hostKind != TypeKind::Bool && hostKind != TypeKind::String)
    {
        // 스칼라 경계로 전달할 수 없는 반환형이면 전역 초기화나 함수 본문도 실행하지 않는다.
        result.error.message = "host scalar API does not support return type " + types.DisplayName(signature->returnType);
        return result;
    }
    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
        if (!types.CanAssign(signature->parameterTypes[i], ScalarType(types, arguments[i].kind())) ||
            arguments[i].kind() == Scalar::Kind::Void)
        {
            result.error.message = "argument " + std::to_string(i + 1) + " requires " +
                types.DisplayName(signature->parameterTypes[i]);
            return result;
        }
        // 기존 호스트 호출 어댑터에는 null 인자를 쓰는 진입점이 아직 없다.
        if (arguments[i].kind() == Scalar::Kind::Null)
        {
            result.error.message = "null host arguments are not supported by this milestone";
            return result;
        }
    }
    Result initialized = Run();
    if (!initialized.ok()) return initialized;
    if (initialized.suspended())
    {
        // 초기화가 양보해도 최초 요청의 함수와 소유 인자 값을 버리지 않는다.
        impl_->deferredCall = true;
        impl_->deferredName = name;
        impl_->deferredArguments = arguments;
        impl_->deferredLimits = limits;
        return initialized;
    }
    NeoScript::Invocation call = impl_->runtime->Call(impl_->instance, name);
    if (!call)
    {
        result.error = impl_->BackendError("could not invoke " + name);
        return result;
    }
    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
        TypeId target = signature->parameterTypes[i];
        if (types.IsNullable(target)) target = types.Get(target).element;
        const Scalar& argument = arguments[i];
        switch (argument.kind())
        {
        case Scalar::Kind::Void: break;
        case Scalar::Kind::Int:
            if (target == types.Float()) call.argFloat(static_cast<double>(argument.asInt()));
            else call.argInt(argument.asInt());
            break;
        case Scalar::Kind::Float: call.argFloat(argument.asFloat()); break;
        case Scalar::Kind::Bool: call.argBool(argument.asBool()); break;
        case Scalar::Kind::String: call.argString(argument.asString()); break;
        case Scalar::Kind::Null: break;
        }
    }
    ExecutionScope executing(impl_->executing);
    const NeoScript::RunStatus status = call.timeout(limits.timeoutMs).budget(limits.instructionBudget).invoke();
    if (status == NeoScript::RunStatus::Suspended || status == NeoScript::RunStatus::Yielded)
    {
        impl_->suspended = true;
        impl_->pending = std::move(call);
        impl_->pendingSignature = signature;
        impl_->pendingName = name;
        result.status = ExecutionStatus::Suspended;
        return result;
    }
    if (status != NeoScript::RunStatus::Completed)
    {
        result.error = TranslateError(call.error(), impl_->sourceName);
        impl_->module->bundle.Translate(result.error);
        if (result.error.ok()) result.error = impl_->BackendError("function did not complete");
        return result;
    }
    return impl_->ReadResult(call, *signature, name);
}

Result Instance::Impl::ReadResult(NeoScript::Invocation& call, const StaticFunctionSignature& signature, const std::string& name)
{
    Result result;
    result.status = ExecutionStatus::Failed;
    result.error.sourceName = sourceName;
    const TypeRegistry& types = checked.types;
    switch (call.retType())
    {
    case NeoScript::ValueType::None:
        if (signature.returnType != types.Void()) result.value = Scalar::Null();
        break;
    case NeoScript::ValueType::Bool: result.value = Scalar::Bool(call.retBool()); break;
    case NeoScript::ValueType::Int: result.value = Scalar::Int(call.retInt()); break;
    case NeoScript::ValueType::Float: result.value = Scalar::Float(call.retFloat()); break;
    case NeoScript::ValueType::String: result.value = Scalar::String(call.retString().str()); break;
    default: result.error.message = "backend returned an unsupported host value"; return result;
    }
    // 실제 값의 타입도 대조하여 변환 누락을 호스트로 조용히 전달하지 않는다.
    TypeId actual = ScalarType(types, result.value.kind());
    TypeId expected = signature.returnType;
    if (types.IsNullable(expected) && actual != types.Null()) expected = types.Get(expected).element;
    if (expected != actual && !(types.IsNullable(expected) && actual == types.Null()))
        result.error.message = "backend result type disagrees with the static signature of " + name;
    result.status = result.error.ok() ? ExecutionStatus::Completed : ExecutionStatus::Failed;
    return result;
}


Result Instance::Resume(double deltaTime, std::uint32_t instructionBudget)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0.0 || deltaTime > (std::numeric_limits<float>::max)())
        return Failure("resume delta time must be finite and nonnegative", impl_->sourceName);
    if (impl_->executing) return Failure("cannot resume an executing instance", impl_->sourceName);
    if (!impl_->suspended) return Failure("instance has no suspended execution", impl_->sourceName);
    NeoScript::ResumeDesc desc;
    desc.deltaTime = static_cast<float>(deltaTime);
    desc.instructionBudget = instructionBudget;
    ExecutionScope executing(impl_->executing);
    impl_->suspended = false;
    const NeoScript::RunStatus status = impl_->runtime->Resume(impl_->instance, desc);
    Result result;
    if (status == NeoScript::RunStatus::Suspended || status == NeoScript::RunStatus::Yielded)
    {
        impl_->suspended = true;
        result.status = ExecutionStatus::Suspended;
        return result;
    }
    impl_->suspended = false;
    if (status != NeoScript::RunStatus::Completed)
    {
        result.status = ExecutionStatus::Failed;
        result.error = impl_->BackendError("resumed execution did not complete");
    }
    else if (impl_->pendingSignature)
        result = impl_->ReadResult(impl_->pending, *impl_->pendingSignature, impl_->pendingName);
    if (impl_->initializing)
    {
        impl_->initializing = false;
        impl_->initialization = result;
    }
    impl_->pending = NeoScript::Invocation();
    impl_->pendingSignature = nullptr;
    impl_->pendingName.clear();
    if (impl_->deferredCall)
    {
        const std::string name = std::move(impl_->deferredName);
        std::vector<Scalar> arguments = std::move(impl_->deferredArguments);
        impl_->deferredCall = false;
        if (result.ok()) return Call(name, arguments, impl_->deferredLimits);
    }
    return result;
}

}
