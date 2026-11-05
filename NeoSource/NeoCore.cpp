#include "NeoCore.h"
#include "NeoCoreModules.h"
#include "NeoCoreConsoleAccess.h"
#include "NeoCoreCompiler.h"
#include "NeoCoreScriptHost.h"
#include "NeoCoreImage.h"
#include "NeoCoreDebugAccess.h"
#include "NeoCoreDebugger.h"
#include "NeoCoreHostRuntime.h"
#include "NeoCoreHostBridge.h"
#include "NeoCoreInstanceContext.h"
#include "NeoCoreValueBoundary.h"
#include "NeoScript.h"
#include <stdexcept>
#include <cmath>
#include <limits>
#include <utility>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <unordered_map>

namespace NeoCore
{
void SetLogHandler(void (*print)(const char*), void (*error)(const char*))
{ NeoScript::SetLogHandler(print,error); }

Scalar Scalar::Int(std::int64_t value) { Scalar s; s.kind_ = Kind::Int; s.integer_ = value; return s; }
Scalar Scalar::Float(double value) { Scalar s; s.kind_ = Kind::Float; s.number_ = value; return s; }
Scalar Scalar::Bool(bool value) { Scalar s; s.kind_ = Kind::Bool; s.integer_ = value; return s; }
Scalar Scalar::String(const std::string& value) { Scalar s; s.kind_ = Kind::String; s.string_ = value; return s; }
Scalar Scalar::Null() { Scalar s; s.kind_ = Kind::Null; return s; }
Scalar Scalar::Vector(Kind kind, const float* components)
{
    if (!components || kind < Kind::Vector2 || kind > Kind::Quaternion)
        throw std::invalid_argument("invalid vector value");
    Scalar value;
    value.kind_ = kind;
    const int count = kind == Kind::Vector2 ? 2 : kind == Kind::Vector3 ? 3 : 4;
    std::copy(components, components + count, value.vector_);
    return value;
}
Scalar Scalar::Object(const std::string& typeName, std::uint64_t id)
{
    if (typeName.empty() || !id) throw std::invalid_argument("native value requires a type and nonzero ID");
    Scalar value;
    value.kind_ = Kind::Object;
    value.string_ = typeName;
    value.object_ = id;
    return value;
}
const float* Scalar::asVector() const
{
    if (kind_ < Kind::Vector2 || kind_ > Kind::Quaternion) throw std::logic_error("NeoCore value is not a vector");
    return vector_;
}
std::uint64_t Scalar::asObject() const
{
    if (kind_ != Kind::Object) throw std::logic_error("NeoCore value is not a native object");
    return object_;
}
const std::string& Scalar::objectType() const
{
    if (kind_ != Kind::Object) throw std::logic_error("NeoCore value is not a native object");
    return string_;
}
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
static bool RegisterJsonFunctions(NeoScript::IRuntime& runtime, std::string& error);

struct Runtime::Impl
{
    NeoScript::IRuntime* runtime = nullptr;
    std::shared_ptr<HostRuntime> host;
    std::shared_ptr<NeoScript::IHttpTransport> httpTransport;
    std::unique_ptr<Debugger> debugger;
    std::unordered_map<std::uint64_t, Instance*> instances;
    unsigned executing = 0;
    unsigned debugNotifications = 0;
    ~Impl()
    {
        debugger.reset();
        if (runtime) NeoScript::DestroyRuntime(runtime);
    }
};

struct Program::Impl
{
    std::shared_ptr<Runtime::Impl> runtimeState;
    StaticCompileResult checked;
    ModuleBundle bundle;
    std::string sourceName;
    bool includeDebugInfo = false;
    std::vector<std::uint8_t> imageBytecode;
    NeoScript::IRuntime* runtime = nullptr;
    NeoScript::ProgramHandle program;
    std::shared_ptr<HostRuntime> host;
    ~Impl()
    {
        if (!runtime) return;
        if (program) runtime->DestroyProgram(program);
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
        : module(std::move(shared)), checked(module->checked), sourceName(module->sourceName), runtime(module->runtime), debugLifetime(new int(0)) {}
    std::shared_ptr<Program::Impl> module;
    StaticCompileResult& checked;
    const std::string& sourceName;
    NeoScript::IRuntime* runtime;
    NeoScript::InstanceHandle instance;
    std::unique_ptr<HostInstance> host;
    BackendInstanceContext backendContext;
    std::shared_ptr<int> debugLifetime;
    std::uint64_t id = 0;
    void* userData = nullptr;
    bool initialized = false;
    bool initializing = false;
    bool suspended = false;
    unsigned executing = 0;
    Result initialization;
    std::uint32_t debugSliceBudget = 0;
    NeoScript::Invocation pending;
    // 직전 jsonDecodeOr/jsonDecodeLenientOr 의 실패 이유. 성공하면 비어 있다.
    std::string jsonError;
    const StaticFunctionSignature* pendingSignature = nullptr;
    StaticFunctionSignature pendingOwnedSignature;
    std::string pendingName;
    bool deferredCall = false;
    std::string deferredName;
    std::vector<Scalar> deferredArguments;
    ExecutionLimits deferredLimits;
    Scalar deferredFunction;

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
    ExecutionScope(unsigned& count, unsigned& runtimeCount) : count_(count), runtimeCount_(runtimeCount) { ++count_; ++runtimeCount_; }
    ~ExecutionScope() { --runtimeCount_; --count_; }
    unsigned& count_;
    unsigned& runtimeCount_;
};
Runtime::Runtime(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
Runtime::~Runtime() = default;
Program::Program(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
Program::~Program() = default;
Instance::Instance(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Instance::~Instance()
{
    if (impl_->module->runtimeState->debugger) impl_->module->runtimeState->debugger->Detach(*this);
    impl_->debugLifetime.reset();
    impl_->module->runtimeState->instances.erase(impl_->id);
}

std::unique_ptr<Runtime> Runtime::Create(Error& error, const CompileOptions& options)
{ return CreateImpl(nullptr, error, options); }
std::unique_ptr<Runtime> Runtime::Create(const HostBindings& bindings, Error& error, const CompileOptions& options)
{ return CreateImpl(&bindings, error, options); }
std::unique_ptr<Runtime> Runtime::CreateImpl(const HostBindings* bindings, Error& error, const CompileOptions& options)
{
    error = Error();
    if (bindings && !bindings->Validate(error.message)) return std::unique_ptr<Runtime>();
    std::shared_ptr<Impl> state(new Impl());
    state->httpTransport = options.httpTransport;
    NeoScript::RuntimeDesc desc;
    desc.httpTransport = options.httpTransport;
    state->runtime = NeoScript::CreateRuntime(desc);
    if (!state->runtime) { error.message = "could not create NeoCore runtime"; return std::unique_ptr<Runtime>(); }
    if (bindings)
    {
        state->host.reset(new HostRuntime(*bindings));
        // 바인딩 항목 주소와 인자 계획은 모든 프로그램이 공유하므로 한 번만 등록한다.
        if (!state->host->Link(*state->runtime, "$neocore_host_", error.message)) return std::unique_ptr<Runtime>();
    }
    if (!RegisterJsonFunctions(*state->runtime, error.message)) return std::unique_ptr<Runtime>();
    state->runtime->FreezeBindings();
    return std::unique_ptr<Runtime>(new Runtime(std::move(state)));
}
std::unique_ptr<Program> Runtime::Compile(const std::string& source, Error& error,
    const std::string& sourceName, bool debug, const CompileOptions& options)
{
    error = Error();
    error.sourceName = sourceName;
    if (impl_->executing) { error.message = "cannot compile while the runtime is executing"; return std::unique_ptr<Program>(); }
    if (options.httpTransport && options.httpTransport != impl_->httpTransport)
    { error.message = "HTTP transport is fixed when the runtime is created"; return std::unique_ptr<Program>(); }
    return Program::CompileImpl(source, error, sourceName, debug, impl_, options);
}
std::unique_ptr<Program> Runtime::LoadImage(const std::vector<std::uint8_t>& image, Error& error, const CompileOptions& options)
{
    error = Error();
    if (impl_->executing) { error.message = "cannot load an image while the runtime is executing"; return std::unique_ptr<Program>(); }
    if (options.httpTransport && options.httpTransport != impl_->httpTransport)
    { error.message = "HTTP transport is fixed when the runtime is created"; return std::unique_ptr<Program>(); }
    return Program::LoadImageImpl(image, error, impl_, options);
}
Instance* Runtime::FindInstance(std::uint64_t id) const
{
    const auto found = impl_->instances.find(id);
    return found == impl_->instances.end() ? nullptr : found->second;
}
Debugger* Runtime::GetDebugger()
{
    if (!impl_->debugger) impl_->debugger.reset(new Debugger(*this));
    return impl_->debugger.get();
}
std::int64_t Runtime::TrimMemory(bool force) { return impl_->runtime->TrimMemory(force); }
std::int64_t Runtime::CollectCycles(bool force) { return impl_->runtime->CollectCycles(force); }
void Runtime::SetEmptyPageHoldSeconds(float seconds) { impl_->runtime->SetEmptyPageHoldSeconds(seconds); }
void Runtime::SetTrimPagesPerCall(int pages) { impl_->runtime->SetTrimPagesPerCall(pages); }
std::uint64_t Instance::Id() const { return impl_->id; }
void Instance::SetUserData(void* value) { impl_->userData = value; }
void* Instance::GetUserData() const { return impl_->userData; }

bool DebugAccess::Get(Program& owner, NeoScript::IRuntime*& runtime,
    NeoScript::ProgramHandle& program, NeoScript::InstanceHandle& instance,
    const StaticCompileResult*& checked, const ModuleBundle*& bundle)
{
    runtime = nullptr;
    program = NeoScript::ProgramHandle();
    instance = NeoScript::InstanceHandle();
    checked = nullptr;
    bundle = nullptr;
    if (!owner.impl_ || !owner.defaultInstance_ || !owner.defaultInstance_->impl_) return false;
    runtime = owner.impl_->runtime;
    program = owner.impl_->program;
    instance = owner.defaultInstance_->impl_->instance;
    checked = &owner.impl_->checked;
    bundle = &owner.impl_->bundle;
    return runtime && program && instance;
}

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
    std::unique_ptr<Runtime> runtime = Runtime::Create(error, options);
    return runtime ? runtime->Compile(source, error, sourceName, debug, options) : std::unique_ptr<Program>();
}

std::unique_ptr<Program> Program::Compile(const std::string& source, const HostBindings& bindings,
    Error& error, const std::string& sourceName, bool debug, const CompileOptions& options)
{
    std::unique_ptr<Runtime> runtime = Runtime::Create(bindings, error, options);
    return runtime ? runtime->Compile(source, error, sourceName, debug, options) : std::unique_ptr<Program>();
}

std::unique_ptr<Program> ConsoleCompileAccess::Compile(const std::string& source, const HostBindings* bindings,
    Error& error, const std::string& sourceName, bool debug, const CompileOptions& options,
    const std::string& entrySourceName)
{
    error = Error();
    if (entrySourceName.empty())
    {
        error.sourceName = sourceName;
        error.message = "entry private access requires an entry source name";
        return std::unique_ptr<Program>();
    }
    // 예외는 이번 프로그램의 모듈 묶음에만 전달한다. 런타임이나 공개 옵션에는 저장하지 않는다.
    std::unique_ptr<Runtime> runtime = bindings ? Runtime::Create(*bindings, error, options) : Runtime::Create(error, options);
    return runtime ? Program::CompileImpl(source, error, sourceName, debug, runtime->impl_, options, &entrySourceName)
        : std::unique_ptr<Program>();
}

std::unique_ptr<Program> ConsoleCompileAccess::Check(const std::string& source, const HostBindings* bindings,
    Error& error, const std::string& sourceName, bool debug, const CompileOptions& options,
    const std::string* privateEntrySource, std::vector<CompileDiagnostic>& unusedWarnings)
{
    error = Error(); unusedWarnings.clear();
    std::unique_ptr<Runtime> runtime = bindings ? Runtime::Create(*bindings, error, options) : Runtime::Create(error, options);
    std::unique_ptr<Program> program = runtime ? Program::CompileImpl(source, error, sourceName, debug, runtime->impl_,
        options, privateEntrySource, &unusedWarnings) : std::unique_ptr<Program>();
    if (!program) unusedWarnings.clear();
    return program;
}

std::unique_ptr<Program> Program::CompileImpl(const std::string& source, Error& error,
    const std::string& sourceName, bool debug, const std::shared_ptr<Runtime::Impl>& runtime, const CompileOptions& options,
    const std::string* privateEntrySource, std::vector<CompileDiagnostic>* unusedWarnings)
{
    error = Error();
    std::shared_ptr<Impl> impl(new Impl());
    impl->runtimeState = runtime;
    impl->runtime = runtime->runtime;
    impl->host = runtime->host;
    impl->sourceName = sourceName;
    impl->includeDebugInfo = debug;
    std::vector<std::string> externalNames;
    for(const auto& define:options.defines) externalNames.push_back(define.name);
    if (impl->host)
    {
        const HostSchema& schema = impl->host->Schema();
        for (const auto& global : schema.Globals()) externalNames.push_back(global.name);
        for (const auto& type : schema.Types().NamedTypes())
            if (schema.Types().Get(type.second).kind == TypeKind::Native || schema.Types().Get(type.second).kind == TypeKind::Struct) externalNames.push_back(type.first);
    }
    if (!BundleModules(source, sourceName, options.moduleLoader, impl->bundle, error, externalNames, &options.defines,
        options.diagnosticLimit, privateEntrySource))
        return std::unique_ptr<Program>();
    const auto implementationSource = [&](int line) {
        return options.scriptHost && options.scriptHost->allowsSource && line > 0 &&
            static_cast<std::size_t>(line) <= impl->bundle.lines.size() &&
            options.scriptHost->allowsSource(impl->bundle.lines[static_cast<std::size_t>(line - 1)].sourceName);
    };
    if (!CompileStaticSource(impl->bundle.source, impl->checked, impl->host ? &impl->host->Schema() : nullptr,
        nullptr, options.diagnosticLimit, options.scriptHost.get(), implementationSource, &impl->bundle.symbolNames, unusedWarnings))
    {
        error.message = impl->checked.errorMessage;
        error.sourceName = sourceName;
        error.line = impl->checked.errorLine;
        error.column = impl->checked.errorColumn;
        impl->bundle.Translate(error);
        for (const CompileDiagnostic& diagnostic : impl->checked.diagnostics)
        {
            Error translated; translated.message = diagnostic.message; translated.sourceName = sourceName;
            translated.line = diagnostic.line; translated.column = diagnostic.column; impl->bundle.Translate(translated);
            CompileDiagnostic original; original.message = translated.message; original.sourceName = translated.sourceName;
            original.line = translated.line; original.column = translated.column; error.diagnostics.push_back(original);
        }
        return std::unique_ptr<Program>();
    }
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
    if (unusedWarnings)
    {
        for (CompileDiagnostic& warning : *unusedWarnings)
        {
            Error location; location.sourceName = sourceName; location.line = warning.line; location.column = warning.column;
            result->impl_->bundle.Translate(location);
            warning.sourceName = location.sourceName; warning.line = location.line; warning.column = location.column;
        }
        std::stable_sort(unusedWarnings->begin(), unusedWarnings->end(), [](const CompileDiagnostic& a, const CompileDiagnostic& b) {
            if (a.sourceName != b.sourceName) return a.sourceName < b.sourceName;
            return a.line != b.line ? a.line < b.line : a.column < b.column;
        });
    }
    return result;
}

bool Program::SaveImage(std::vector<std::uint8_t>& image, Error& error) const
{
    image.clear();
    error = Error();
    error.sourceName = impl_->sourceName;
    if (impl_->runtimeState->executing) { error.message = "cannot save an image while the runtime is executing"; return false; }
    if (impl_->imageBytecode.empty())
    {
        // 정적 검사 결과의 IR에서 저장용 코드를 만든다. 로드할 때 소스나 파서를 다시 실행하지 않는다.
        NeoScript::CompileDesc desc;
        desc.source = impl_->checked.loweredSource;
        desc.coreIntrinsicPrefix = impl_->checked.backendIntrinsicPrefix;
        desc.coreTypedIR = &impl_->checked.typedIR;
        desc.sourceName = impl_->sourceName;
        desc.includeDebugInfo = impl_->includeDebugInfo;
        const NeoScript::Error compiled = impl_->runtime->CompileToBytecode(desc, impl_->imageBytecode);
        if (!compiled.ok())
        {
            error = TranslateError(compiled, impl_->sourceName);
            impl_->bundle.Translate(error);
            return false;
        }
    }
    Image::Data data;
    data.checked = impl_->checked;
    data.bundle = impl_->bundle;
    data.sourceName = impl_->sourceName;
    data.debug = impl_->includeDebugInfo;
    data.bytecode = impl_->imageBytecode;
    return Image::Write(data, impl_->host ? &impl_->host->Schema() : nullptr, image, error);
}

std::unique_ptr<Program> Program::LoadImage(const std::vector<std::uint8_t>& image,
    Error& error, const CompileOptions& options)
{
    std::unique_ptr<Runtime> runtime = Runtime::Create(error, options);
    return runtime ? runtime->LoadImage(image, error, options) : std::unique_ptr<Program>();
}

std::unique_ptr<Program> Program::LoadImage(const std::vector<std::uint8_t>& image,
    const HostBindings& bindings, Error& error, const CompileOptions& options)
{
    std::unique_ptr<Runtime> runtime = Runtime::Create(bindings, error, options);
    return runtime ? runtime->LoadImage(image, error, options) : std::unique_ptr<Program>();
}

std::unique_ptr<Program> Program::LoadImageImpl(const std::vector<std::uint8_t>& image,
    Error& error, const std::shared_ptr<Runtime::Impl>& runtime, const CompileOptions&)
{
    error = Error();
    Image::Data data;
    if (!Image::Read(image, runtime->host ? &runtime->host->Schema() : nullptr, data, error)) return std::unique_ptr<Program>();
    if (runtime->host && data.checked.hostPrefix != "$neocore_host_")
    { error.message = "NeoCore host image has an incompatible binding prefix"; return std::unique_ptr<Program>(); }
    std::shared_ptr<Impl> impl(new Impl());
    impl->runtimeState = runtime;
    impl->runtime = runtime->runtime;
    impl->host = runtime->host;
    impl->checked = std::move(data.checked);
    impl->bundle = std::move(data.bundle);
    impl->sourceName = std::move(data.sourceName);
    impl->includeDebugInfo = data.debug;
    impl->imageBytecode = std::move(data.bytecode);
    NeoScript::Error backendError;
    impl->program = impl->runtime->LoadProgram(
        NeoScript::Span<const std::uint8_t>(impl->imageBytecode.data(), impl->imageBytecode.size()), &backendError);
    if (!impl->program || !backendError.ok())
    {
        error = TranslateError(backendError, impl->sourceName);
        if (error.ok()) error = impl->BackendError("could not load NeoCore bytecode");
        return std::unique_ptr<Program>();
    }
    std::vector<std::pair<std::string, std::size_t>> signatures;
    for (const StaticFunctionSignature& function : impl->checked.functions)
        if (function.exported) signatures.push_back(std::make_pair(function.name, function.parameterTypes.size()));
    if (!NeoCoreBackend::ValidateImageFunctions(*impl->runtime, impl->program, signatures, error.message))
        return std::unique_ptr<Program>();
    if (impl->includeDebugInfo && !NeoCoreBackend::RemapSourceLines(*impl->runtime, impl->program, impl->checked.hostWriteSourceLines))
    { error.message = "could not restore NeoCore image source locations"; return std::unique_ptr<Program>(); }
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
    result->impl_->backendContext.owner = result.get();
    result->impl_->backendContext.host = result->impl_->host ? result->impl_->host->UserData() : nullptr;
    desc.userData = &result->impl_->backendContext;
    result->impl_->instance = impl_->runtime->CreateInstance(impl_->program, desc);
    if (!result->impl_->instance ||
        !NeoCoreBackend::EnableCooperativeSleep(*impl_->runtime, result->impl_->instance))
    {
        error = result->impl_->BackendError("could not create NeoCore instance");
        return std::unique_ptr<Instance>();
    }
    static std::atomic<std::uint64_t> nextId(1);
    result->impl_->id = nextId.fetch_add(1, std::memory_order_relaxed);
    if (!result->impl_->id) throw std::overflow_error("NeoCore instance ID space exhausted");
    impl_->runtimeState->instances.emplace(result->impl_->id, result.get());
    return result;
}
Result Program::Run() { return defaultInstance_->Run(); }
Result Program::Call(const std::string& name, const std::vector<Scalar>& arguments, const ExecutionLimits& limits)
{
    return defaultInstance_->Call(name, arguments, limits);
}
Result Program::Resume(double deltaTime, std::uint32_t budget) { return defaultInstance_->Resume(deltaTime, budget); }
bool Program::IsSuspended() const { return defaultInstance_->IsSuspended(); }
bool Program::IsExecuting() const { return defaultInstance_->IsExecuting(); }
bool Program::Cancel(Error& error) { return defaultInstance_->Cancel(error); }
bool Program::Reset(Error& error) { return defaultInstance_->Reset(error); }
std::int64_t Program::TrimMemory(bool force) { return impl_->runtime->TrimMemory(force); }
std::int64_t Program::CollectCycles(bool force) { return impl_->runtime->CollectCycles(force); }
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
bool Instance::IsExecuting() const { return impl_->executing != 0; }
bool Instance::Cancel(Error& error)
{
    error = Error();
    if (impl_->executing) { error.message = "cannot cancel an executing instance"; return false; }
    if (!impl_->runtime->Cancel(impl_->instance)) { error = impl_->BackendError("could not cancel instance"); return false; }
    if (impl_->module->runtimeState->debugger) impl_->module->runtimeState->debugger->ClearStop(*this);
    impl_->pending = NeoScript::Invocation();
    impl_->pendingSignature = nullptr;
    impl_->pendingName.clear();
    impl_->deferredCall = false;
    impl_->deferredArguments.clear();
    impl_->deferredName.clear();
    impl_->suspended = false;
    if (impl_->initializing)
    {
        impl_->initializing = false;
        impl_->initialization = Failure("global initialization was cancelled; reset before use", impl_->sourceName);
    }
    return true;
}
bool Instance::Reset(Error& error)
{
    error = Error();
    if (impl_->executing) { error.message = "cannot reset an executing instance"; return false; }
    if (impl_->suspended && !Cancel(error)) return false;
    if (impl_->module->runtimeState->debugger) impl_->module->runtimeState->debugger->Detach(*this);
    impl_->debugLifetime.reset(new int(0));
    impl_->pending = NeoScript::Invocation();
    if (!impl_->runtime->ResetInstance(impl_->instance)) { error = impl_->BackendError("could not reset instance"); return false; }
    if (!NeoCoreBackend::EnableCooperativeSleep(*impl_->runtime, impl_->instance))
    { error = impl_->BackendError("could not reset cooperative execution"); return false; }
    impl_->initialized = impl_->initializing = impl_->suspended = false;
    impl_->initialization = Result();
    return true;
}

Result Instance::Run()
{
    if (impl_->module->runtimeState->debugNotifications)
        return Failure("cannot execute from a debugger stop notification", impl_->sourceName);
    if (impl_->initializing)
    {
        if (impl_->suspended) return impl_->initialization;
        return Failure("script reentry is not allowed before global initialization completes", impl_->sourceName);
    }
    if (impl_->suspended) return Failure("instance has a suspended call; resume it before starting another call", impl_->sourceName);
    if (impl_->initialized) return impl_->initialization;
    if (impl_->module->runtimeState->debugger) impl_->module->runtimeState->debugger->ClearStop(*this);
    impl_->initialized = true;
    impl_->initializing = true;
    ExecutionScope executing(impl_->executing, impl_->module->runtimeState->executing);
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
    case Scalar::Kind::Vector2: return types.Vector2();
    case Scalar::Kind::Vector3: return types.Vector3();
    case Scalar::Kind::Vector4: return types.Vector4();
    case Scalar::Kind::Quaternion: return types.Quaternion();
    case Scalar::Kind::Object: return InvalidType;
    case Scalar::Kind::List:
    case Scalar::Kind::Map:
    case Scalar::Kind::Set:
    case Scalar::Kind::Array:
    case Scalar::Kind::Function:
    case Scalar::Kind::Struct: return InvalidType;
    }
    return InvalidType;
}

#include "NeoCoreValueBoundary.inl"

static bool DispatchJsonEncode(NeoScript::CallContext& context, NeoScript::StringView)
{
    try
    {
        BackendInstanceContext* instance = static_cast<BackendInstanceContext*>(context.instanceUserData());
        if (!instance || !instance->owner || (context.argCount() != 3 && context.argCount() != 4) ||
            context.argType(0) != NeoScript::ValueType::Int || context.argType(2) != NeoScript::ValueType::Bool ||
            (context.argCount() == 4 && context.argType(3) != NeoScript::ValueType::Bool))
        {
            context.fail(0, "invalid internal JSON encode arguments");
            return false;
        }
        const std::int64_t type = NeoCoreBackend::ReadInt(context, 0);
        if (type <= 0 || static_cast<std::uint64_t>(type) > std::numeric_limits<TypeId>::max())
        {
            context.fail(0, "invalid internal JSON type");
            return false;
        }
        std::string text, error;
        if (!BoundaryAccess::EncodeJson(*instance->owner, context, 1, static_cast<TypeId>(type),
            NeoCoreBackend::ReadBool(context, 2), text, error))
        {
            if (context.argCount() == 4 && NeoCoreBackend::ReadBool(context, 3))
            {
                context.retNull();
                return true;
            }
            context.fail(0, error);
            return false;
        }
        context.retString(text);
        return true;
    }
    catch (const std::exception& failure) { context.fail(0, failure.what()); }
    catch (...) { context.fail(0, "JSON encode failed"); }
    return false;
}

static bool DispatchJsonDecode(NeoScript::CallContext& context, NeoScript::StringView)
{
    try
    {
        // 인자: (타입 ID, 텍스트, [대체값], 모드). 모드 비트 0 은 너그러운 해독, 비트 1 은 대체값 존재.
        BackendInstanceContext* instance = static_cast<BackendInstanceContext*>(context.instanceUserData());
        const std::size_t count = context.argCount();
        if (!instance || !instance->owner || (count != 3 && count != 4) ||
            context.argType(0) != NeoScript::ValueType::Int || context.argType(1) != NeoScript::ValueType::String ||
            context.argType(count - 1) != NeoScript::ValueType::Int)
        {
            context.fail(0, "invalid internal JSON decode arguments");
            return false;
        }
        const std::int64_t type = NeoCoreBackend::ReadInt(context, 0);
        if (type <= 0 || static_cast<std::uint64_t>(type) > std::numeric_limits<TypeId>::max())
        {
            context.fail(0, "invalid internal JSON type");
            return false;
        }
        const std::int64_t mode = NeoCoreBackend::ReadInt(context, count - 1);
        const bool lenient = (mode & 1) != 0;
        const bool hasFallback = (mode & 2) != 0;
        if (hasFallback != (count == 4))
        {
            context.fail(0, "invalid internal JSON decode mode");
            return false;
        }
        const std::string text = NeoCoreBackend::ReadString(context, 1);
        std::string error;
        if (BoundaryAccess::DecodeJson(*instance->owner, context, static_cast<TypeId>(type), text, lenient, hasFallback ? 2 : -1, error)) return true;
        context.fail(0, error);
    }
    catch (const std::exception& failure) { context.fail(0, failure.what()); }
    catch (...) { context.fail(0, "JSON decode failed"); }
    return false;
}

static bool DispatchJsonError(NeoScript::CallContext& context, NeoScript::StringView)
{
    try
    {
        BackendInstanceContext* instance = static_cast<BackendInstanceContext*>(context.instanceUserData());
        if (!instance || !instance->owner || context.argCount() != 0)
        {
            context.fail(0, "invalid internal JSON error arguments");
            return false;
        }
        context.retString(BoundaryAccess::JsonError(*instance->owner));
        return true;
    }
    catch (const std::exception& failure) { context.fail(0, failure.what()); }
    catch (...) { context.fail(0, "JSON error query failed"); }
    return false;
}

static bool RegisterJsonFunctions(NeoScript::IRuntime& runtime, std::string& error)
{
    NeoScript::NativeObjectDesc encode;
    encode.name = "$neocore_json_encode";
    encode.method = &DispatchJsonEncode;
    encode.declareGlobal = false;
    NeoScript::NativeObjectDesc decode;
    decode.name = "$neocore_json_decode";
    decode.method = &DispatchJsonDecode;
    decode.declareGlobal = false;
    NeoScript::NativeObjectDesc lastError;
    lastError.name = "$neocore_json_error";
    lastError.method = &DispatchJsonError;
    lastError.declareGlobal = false;
    if (runtime.RegisterObject(encode) && runtime.RegisterObject(decode) && runtime.RegisterObject(lastError)) return true;
    error = "could not register typed JSON functions";
    return false;
}

static TypeId BoundaryTypeByName(const TypeRegistry& types, const std::string& name)
{
    TypeId displayed = InvalidType;
    for(TypeId type=1; types.Get(type).kind!=TypeKind::Invalid; ++type)
    {
        if(types.DisplayName(type)==name) return type;
        if(types.DiagnosticName(type)==name)
        {
            if(displayed!=InvalidType) throw std::invalid_argument("ambiguous module type name: " + name);
            displayed=type;
        }
    }
    if(displayed!=InvalidType) return displayed;
    throw std::invalid_argument("type is not declared by this program: " + name);
}

Scalar Program::CreateValue(const std::string& typeName, Error& error)
{
    error = Error();
    try
    {
        const TypeId type=BoundaryTypeByName(impl_->checked.types,typeName);
        if (impl_->checked.types.Get(type).kind == TypeKind::Struct)
            return DefaultBoundaryValue(*impl_->runtime,impl_->checked.types,type,impl_);
        return BoundaryAccess::Wrap(*impl_->runtime,impl_->checked.types,type,impl_,
            NewCollection(*impl_->runtime,impl_->checked.types,type,impl_));
    }
    catch (const std::exception& failure) { error.message = failure.what(); }
    return Scalar();
}

Scalar Program::CreateValue(const std::string& typeName, const std::vector<Scalar>& fields, Error& error)
{
    error=Error();
    try
    {
        const auto& types=impl_->checked.types;
        const TypeId type=BoundaryTypeByName(types,typeName);
        const TypeDesc& desc=types.Get(type);
        if(desc.kind!=TypeKind::Struct) throw std::invalid_argument("field initialization requires a struct type");
        if(fields.size()!=desc.fields.size()) throw std::invalid_argument("struct field count does not match its declaration");
        const auto value=NewCollection(*impl_->runtime,types,type,impl_);
        for(std::size_t index=0; index<fields.size(); ++index)
            if(!NeoCoreBackend::ValueAppend(value,BoundaryAccess::Copy(*impl_->runtime,types,desc.fields[index].type,impl_,fields[index])))
                throw std::runtime_error("could not initialize struct field");
        return BoundaryAccess::Wrap(*impl_->runtime,types,type,impl_,value);
    }
    catch(const std::exception& failure) { error.message=failure.what(); }
    return Scalar();
}

Scalar Program::FindFunction(const std::string& name, Error& error)
{
    error=Error();
    try
    {
        const StaticFunctionSignature* signature=nullptr;
        for(const auto& candidate:impl_->checked.functions)
            if(candidate.name==name && candidate.exported) { signature=&candidate; break; }
        if(!signature) throw std::invalid_argument("exported function not found: "+name);
        const TypeId type=impl_->checked.types.FunctionOf(signature->returnType,signature->parameterTypes);
        return BoundaryAccess::Wrap(*impl_->runtime,impl_->checked.types,type,impl_,
            NeoCoreBackend::FindFunction(*impl_->runtime,impl_->program,name,impl_));
    }
    catch(const std::exception& failure) { error.message=failure.what(); }
    return Scalar();
}

Scalar Program::ParseJson(const std::string& typeName, const std::string& json, Error& error, bool strictIntegers)
{
    error=Error();
    try
    {
        if(defaultInstance_->IsExecuting()) throw std::logic_error("cannot parse JSON through the default instance during execution");
        const TypeId type=BoundaryTypeByName(impl_->checked.types,typeName);
        return BoundaryAccess::ParseJson(*defaultInstance_,type,json,strictIntegers,error.message);
    }
    catch(const std::exception& failure) { error.message=failure.what(); }
    return Scalar();
}

std::string Program::ToJson(const Scalar& value, Error& error, bool sortKeys)
{
    error=Error();
    try
    {
        const auto& types=impl_->checked.types;
        const TypeId type=value.bound_?BoundaryTypeByName(types,value.typeName()):ScalarType(types,value.kind());
        if(type==InvalidType || type==types.Void() || value.kind()==Scalar::Kind::Object)
            throw std::invalid_argument("value cannot be encoded as JSON");
        if(value.kind()==Scalar::Kind::Null) return "null";
        const auto raw=BoundaryAccess::Copy(*impl_->runtime,types,type,impl_,value);
        const auto normalized=ConvertJsonValue(*impl_->runtime,types,type,impl_,raw,false,false,"$",0);
        std::string result;
        std::vector<std::string> warnings;
        if(!NeoCoreBackend::EncodeJson(normalized,sortKeys,result,error.message,warnings)) return std::string();
        return result;
    }
    catch(const std::exception& failure) { error.message=failure.what(); }
    return std::string();
}

Result Instance::ReadGlobal(const std::string& name)
{
    try
    {
        if (impl_->executing || impl_->suspended) return Failure("cannot read globals while the instance is busy",impl_->sourceName);
        const StaticGlobalSignature* global = nullptr;
        for (const auto& candidate : impl_->checked.globals)
            if (candidate.name == name && candidate.exported) { global=&candidate; break; }
        if (!global) return Failure("exported global not found: " + name,impl_->sourceName);
        Result result=Run();
        if (!result.ok() || result.suspended()) return result;
        auto value=NeoCoreBackend::ReadGlobal(*impl_->runtime,impl_->instance,name,impl_->module);
        for(const auto& symbol:impl_->checked.debugSymbols)
            if(symbol.backendName==name && symbol.boxed) { value=NeoCoreBackend::ValueAt(value,0); break; }
        result.value=BoundaryAccess::Wrap(*impl_->runtime,impl_->checked.types,global->type,impl_->module,value);
        if(result.value.kind()==Scalar::Kind::Struct)
            result.value=BoundaryAccess::Wrap(*impl_->runtime,impl_->checked.types,global->type,impl_->module,
                BoundaryAccess::Copy(*impl_->runtime,impl_->checked.types,global->type,impl_->module,result.value));
        return result;
    }
    catch(const std::exception& failure) { return Failure(failure.what(),impl_->sourceName); }
}
Result Instance::WriteGlobal(const std::string& name, const Scalar& value)
{
    try
    {
        if (impl_->executing || impl_->suspended) return Failure("cannot write globals while the instance is busy",impl_->sourceName);
        const StaticGlobalSignature* global = nullptr;
        for (const auto& candidate : impl_->checked.globals)
            if (candidate.name == name && candidate.exported) { global=&candidate; break; }
        if (!global) return Failure("exported global not found: " + name,impl_->sourceName);
        if (!BoundaryAccess::Accepts(impl_->checked.types,global->type,value))
            return Failure("global value requires " + impl_->checked.types.DisplayName(global->type),impl_->sourceName);
        if(value.bound_ && (value.bound_->lifetime.get() != impl_->module.get() ||
            !NeoCoreBackend::CanPassValue(*impl_->runtime,impl_->instance,value.bound_->value)))
            return Failure("global value belongs to another program or contains an unavailable closure",impl_->sourceName);
        Result result=Run();
        if (!result.ok() || result.suspended()) return result;
        const auto stored=BoundaryAccess::Copy(*impl_->runtime,impl_->checked.types,global->type,impl_->module,value);
        bool boxed=false;
        for(const auto& symbol:impl_->checked.debugSymbols)
            if(symbol.backendName==name && symbol.boxed) { boxed=true; break; }
        const bool written=boxed ? NeoCoreBackend::ValueSet(NeoCoreBackend::ReadGlobal(*impl_->runtime,impl_->instance,name,impl_->module),0,stored)
            : NeoCoreBackend::WriteGlobal(*impl_->runtime,impl_->instance,name,stored);
        if (!written)
            return Failure("could not update exported global",impl_->sourceName);
        return result;
    }
    catch(const std::exception& failure) { return Failure(failure.what(),impl_->sourceName); }
}

Result Instance::Call(const std::string& name, const std::vector<Scalar>& arguments, const ExecutionLimits& limits)
{ return Invoke(name,nullptr,arguments,limits); }
Result Instance::Call(const Scalar& function, const std::vector<Scalar>& arguments, const ExecutionLimits& limits)
{ return Invoke("<callback>",&function,arguments,limits); }
Result Instance::Invoke(const std::string& name, const Scalar* function, const std::vector<Scalar>& arguments, const ExecutionLimits& limits)
{
    if (impl_->module->runtimeState->debugNotifications)
        return Failure("cannot execute from a debugger stop notification", impl_->sourceName);
    if (impl_->suspended) return Failure("instance has a suspended call; resume it before starting another call", impl_->sourceName);
    if (limits.timeoutMs < -1) return Failure("call timeout must be -1 or nonnegative", impl_->sourceName);
    Result result;
    result.status = ExecutionStatus::Failed;
    result.error.sourceName = impl_->sourceName;
    const StaticFunctionSignature* signature = nullptr;
    StaticFunctionSignature callable;
    if (function)
    {
        if(function->kind()!=Scalar::Kind::Function || !function->bound_ ||
            function->bound_->runtime!=impl_->runtime || function->bound_->lifetime.get()!=impl_->module.get())
            return Failure("callback does not belong to this program",impl_->sourceName);
        if(!NeoCoreBackend::CanPassValue(*impl_->runtime,impl_->instance,function->bound_->value))
            return Failure("callback is unavailable in this instance",impl_->sourceName);
        const TypeDesc& desc=function->bound_->types->Get(function->bound_->type);
        callable.name=name;
        callable.parameterTypes=desc.parameters;
        callable.returnType=desc.returnType;
        signature=&callable;
    }
    else for (const auto& candidate : impl_->checked.functions)
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
    const TypeId hostReturn = signature->returnType;
    const TypeKind hostKind = types.Get(hostReturn).kind;
    if (hostKind != TypeKind::Void && hostKind != TypeKind::Int && hostKind != TypeKind::Float &&
        hostKind != TypeKind::Bool && hostKind != TypeKind::String &&
        hostKind != TypeKind::Vector2 && hostKind != TypeKind::Vector3 && hostKind != TypeKind::Vector4 &&
        hostKind != TypeKind::Quaternion && hostKind != TypeKind::Native && hostKind != TypeKind::Array &&
        hostKind != TypeKind::Array32 && hostKind != TypeKind::List && hostKind != TypeKind::Map &&
        hostKind != TypeKind::Set && hostKind != TypeKind::Function && hostKind != TypeKind::Struct)
    {
        // 호스트 경계로 전달할 수 없는 반환형이면 전역 초기화나 함수 본문도 실행하지 않는다.
        result.error.message = "host API does not support return type " + types.DisplayName(signature->returnType);
        return result;
    }
    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
        if(arguments[i].bound_ && (arguments[i].bound_->lifetime.get()!=impl_->module.get() ||
            !NeoCoreBackend::CanPassValue(*impl_->runtime,impl_->instance,arguments[i].bound_->value)))
            return Failure("argument belongs to another program or contains an unavailable closure",impl_->sourceName);
        if (!BoundaryAccess::Accepts(types,signature->parameterTypes[i],arguments[i]) ||
            arguments[i].kind() == Scalar::Kind::Void)
        {
            result.error.message = "argument " + std::to_string(i + 1) + " requires " +
                types.DisplayName(signature->parameterTypes[i]);
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
        impl_->deferredFunction = function ? *function : Scalar();
        return initialized;
    }
    if (impl_->module->runtimeState->debugger) impl_->module->runtimeState->debugger->ClearStop(*this);
    NeoScript::Invocation call = function ? NeoCoreBackend::CallFunction(*impl_->runtime,impl_->instance,function->bound_->value)
        : impl_->runtime->Call(impl_->instance, name);
    if (!call)
    {
        result.error = impl_->BackendError("could not invoke " + name);
        return result;
    }
    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
        const TypeId target = signature->parameterTypes[i];
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
        case Scalar::Kind::Null:
            if (!NeoCoreBackend::AppendNull(call)) return Failure("could not append null argument", impl_->sourceName);
            break;
        case Scalar::Kind::Vector2: call.argVec(argument.asVector(), 2); break;
        case Scalar::Kind::Vector3: call.argVec(argument.asVector(), 3); break;
        case Scalar::Kind::Vector4:
        case Scalar::Kind::Quaternion: call.argVec(argument.asVector(), 4); break;
        case Scalar::Kind::Object:
        {
            const std::uint64_t id = argument.asObject();
            std::int64_t bits;
            std::memcpy(&bits, &id, sizeof(bits));
            call.argInt(bits);
            break;
        }
        case Scalar::Kind::List:
        case Scalar::Kind::Map:
        case Scalar::Kind::Set:
        case Scalar::Kind::Array:
        case Scalar::Kind::Function:
        case Scalar::Kind::Struct:
            try
            {
                if (!NeoCoreBackend::AppendValue(call,BoundaryAccess::Copy(*impl_->runtime,types,target,impl_->module,argument)))
                    return Failure("could not transfer typed argument",impl_->sourceName);
            }
            catch(const std::exception& failure) { return Failure(failure.what(),impl_->sourceName); }
            break;
        }
    }
    ExecutionScope executing(impl_->executing, impl_->module->runtimeState->executing);
    const std::uint32_t budget = limits.instructionBudget ? limits.instructionBudget : impl_->debugSliceBudget;
    const NeoScript::RunStatus status = call.timeout(limits.timeoutMs).budget(budget).invoke();
    if (status == NeoScript::RunStatus::Suspended || status == NeoScript::RunStatus::Yielded)
    {
        impl_->suspended = true;
        impl_->pending = std::move(call);
        impl_->pendingOwnedSignature = *signature;
        impl_->pendingSignature = &impl_->pendingOwnedSignature;
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
    TypeId expected = signature.returnType;
    const TypeKind expectedKind = types.Get(expected).kind;
    if (expectedKind == TypeKind::Array || expectedKind == TypeKind::Array32 || expectedKind == TypeKind::List ||
        expectedKind == TypeKind::Map || expectedKind == TypeKind::Set || expectedKind == TypeKind::Function || expectedKind == TypeKind::Struct)
    {
        try
        {
            result.value = BoundaryAccess::Wrap(*runtime,types,signature.returnType,module,NeoCoreBackend::CaptureResult(call,module));
            if (expectedKind == TypeKind::Struct && result.value.kind() != Scalar::Kind::Null)
                result.value = BoundaryAccess::Wrap(*runtime,types,expected,module,BoundaryAccess::Copy(*runtime,types,expected,module,result.value));
            result.status = ExecutionStatus::Completed;
        }
        catch (const std::exception& failure) { result.error.message = failure.what(); }
        return result;
    }
    switch (call.retType())
    {
    case NeoScript::ValueType::None:
        if (signature.returnType != types.Void()) result.value = Scalar::Null();
        break;
    case NeoScript::ValueType::Bool: result.value = Scalar::Bool(call.retBool()); break;
    case NeoScript::ValueType::Int:
        if (expectedKind == TypeKind::Native)
        {
            const std::int64_t bits = call.retInt();
            std::uint64_t objectId;
            std::memcpy(&objectId, &bits, sizeof(objectId));
            if (!objectId) { result.error.message = "backend returned an invalid object ID"; return result; }
            result.value = Scalar::Object(types.Get(expected).name, objectId);
        }
        else result.value = Scalar::Int(call.retInt());
        break;
    case NeoScript::ValueType::Float: result.value = Scalar::Float(call.retFloat()); break;
    case NeoScript::ValueType::String: result.value = Scalar::String(call.retString().str()); break;
    case NeoScript::ValueType::Vec2:
    case NeoScript::ValueType::Vec3:
    case NeoScript::ValueType::Vec4:
    {
        float components[4] = {};
        const int dimension = expectedKind == TypeKind::Vector2 ? 2 : expectedKind == TypeKind::Vector3 ? 3 : 4;
        if (!call.retVec(components, dimension)) { result.error.message = "backend vector dimension mismatch"; return result; }
        const Scalar::Kind kind = expectedKind == TypeKind::Quaternion ? Scalar::Kind::Quaternion :
            dimension == 2 ? Scalar::Kind::Vector2 : dimension == 3 ? Scalar::Kind::Vector3 : Scalar::Kind::Vector4;
        result.value = Scalar::Vector(kind, components);
        break;
    }
    default: result.error.message = "backend returned an unsupported host value"; return result;
    }
    // 실제 값의 타입도 대조하여 변환 누락을 호스트로 조용히 전달하지 않는다.
    TypeId actual = result.value.kind() == Scalar::Kind::Object
        ? types.LookupNamed(result.value.objectType()) : ScalarType(types, result.value.kind());
    expected = signature.returnType;
    if (expected != actual && !(types.AllowsNull(expected) && actual == types.Null()))
        result.error.message = "backend result type disagrees with the static signature of " + name;
    result.status = result.error.ok() ? ExecutionStatus::Completed : ExecutionStatus::Failed;
    return result;
}


Result Instance::Resume(double deltaTime, std::uint32_t instructionBudget)
{
    if (impl_->module->runtimeState->debugNotifications)
        return Failure("cannot execute from a debugger stop notification", impl_->sourceName);
    if (!std::isfinite(deltaTime) || deltaTime < 0.0 || deltaTime > (std::numeric_limits<float>::max)())
        return Failure("resume delta time must be finite and nonnegative", impl_->sourceName);
    if (impl_->executing) return Failure("cannot resume an executing instance", impl_->sourceName);
    if (!impl_->suspended) return Failure("instance has no suspended execution", impl_->sourceName);
    NeoScript::ResumeDesc desc;
    desc.deltaTime = static_cast<float>(deltaTime);
    desc.instructionBudget = instructionBudget ? instructionBudget : impl_->debugSliceBudget;
    ExecutionScope executing(impl_->executing, impl_->module->runtimeState->executing);
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
        Scalar function = std::move(impl_->deferredFunction);
        impl_->deferredCall = false;
        if (result.ok()) return function.kind() == Scalar::Kind::Function
            ? Call(function,arguments,impl_->deferredLimits) : Call(name, arguments, impl_->deferredLimits);
    }
    return result;
}

}

#include "NeoCoreDebugger.inl"
