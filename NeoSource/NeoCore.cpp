#include "NeoCore.h"
#include "NeoCoreCompiler.h"
#include "NeoCoreHostRuntime.h"
#include "NeoCoreHostBridge.h"
#include "NeoScript.h"
#include <stdexcept>
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

static Error TranslateError(const NeoScript::Error& backend, const std::string& name);

struct Program::Impl
{
    StaticCompileResult checked;
    std::string sourceName;
    NeoScript::IRuntime* runtime = nullptr;
    NeoScript::ProgramHandle program;
    NeoScript::InstanceHandle instance;
    bool initialized = false;
    bool initializing = false;
    Result initialization;
    std::unique_ptr<HostRuntime> host;

    ~Impl()
    {
        if (!runtime) return;
        if (instance) runtime->DestroyInstance(instance);
        if (program) runtime->DestroyProgram(program);
        NeoScript::DestroyRuntime(runtime);
    }

    Error BackendError(const std::string& fallback)
    {
        NeoScript::Error backend;
        if (instance && NeoCoreBackend::ReadLastCallError(*runtime, instance, backend))
            return TranslateError(backend, sourceName);
        Error error;
        error.sourceName = sourceName;
        NeoScript::StringView text;
        error.message = runtime->TakeLastError(text) ? text.str() : fallback;
        return error;
    }
};

Program::Program(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
    if (impl_->host) impl_->host->SetProgram(this);
}
Program::~Program() = default;

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
    const std::string& sourceName, bool debug)
{
    return CompileImpl(source, error, sourceName, debug, nullptr);
}

std::unique_ptr<Program> Program::Compile(const std::string& source, const HostBindings& bindings,
    Error& error, const std::string& sourceName, bool debug)
{
    return CompileImpl(source, error, sourceName, debug, &bindings);
}

std::unique_ptr<Program> Program::CompileImpl(const std::string& source, Error& error,
    const std::string& sourceName, bool debug, const HostBindings* bindings)
{
    error = Error();
    std::unique_ptr<Impl> impl(new Impl());
    impl->sourceName = sourceName;
    if (bindings)
    {
        if (!bindings->Validate(error.message)) { error.sourceName = sourceName; return std::unique_ptr<Program>(); }
        impl->host.reset(new HostRuntime(*bindings));
    }
    if (!CompileStaticSource(source, impl->checked, true, impl->host ? &impl->host->Schema() : nullptr))
    {
        error.message = impl->checked.errorMessage;
        error.sourceName = sourceName;
        error.line = impl->checked.errorLine;
        error.column = impl->checked.errorColumn;
        return std::unique_ptr<Program>();
    }
    impl->runtime = NeoScript::CreateRuntime();
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
    NeoScript::CompileResult compiled = impl->runtime->Compile(desc);
    impl->program = compiled.program;
    if (!impl->program || !compiled.error.ok())
    {
        error = TranslateError(compiled.error, sourceName);
        if (error.ok()) error = impl->BackendError("backend compilation failed");
        return std::unique_ptr<Program>();
    }
    if (debug && !NeoCoreBackend::RemapSourceLines(*impl->runtime, impl->program, impl->checked.hostWriteSourceLines))
    {
        error.sourceName = sourceName;
        error.message = "could not map host property diagnostics to source";
        return std::unique_ptr<Program>();
    }
    NeoScript::InstanceDesc instanceDesc;
    instanceDesc.runGlobalInit = false;
    impl->instance = impl->runtime->CreateInstance(impl->program, instanceDesc);
    if (!impl->instance)
    {
        error = impl->BackendError("could not create NeoCore instance");
        return std::unique_ptr<Program>();
    }
    return std::unique_ptr<Program>(new Program(std::move(impl)));
}

bool Program::SetHostObject(const std::string& name, ObjectHandle object, Error& error)
{
    error = Error();
    error.sourceName = impl_->sourceName;
    if (!impl_->host) { error.message = "program has no host bindings"; return false; }
    return impl_->host->SetGlobal(name, object, error.message);
}

Result Program::Run()
{
    if (impl_->initializing)
    {
        Result result;
        result.error.sourceName = impl_->sourceName;
        result.error.message = "script reentry is not allowed before global initialization completes";
        return result;
    }
    if (impl_->initialized) return impl_->initialization;
    impl_->initialized = true;
    // 타입이 정해진 전역 슬롯도 초기화 도중에는 아직 값을 받지 못했을 수 있다.
    impl_->initializing = true;
    if (impl_->runtime->RunGlobalInit(impl_->instance) != NeoScript::RunStatus::Completed)
        impl_->initialization.error = impl_->BackendError("global initialization did not complete");
    impl_->initializing = false;
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

Result Program::Call(const std::string& name, const std::vector<Scalar>& arguments)
{
    Result result;
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
    if (call.invoke() != NeoScript::RunStatus::Completed)
    {
        result.error = TranslateError(call.error(), impl_->sourceName);
        if (result.error.ok()) result.error = impl_->BackendError("function did not complete");
        return result;
    }
    switch (call.retType())
    {
    case NeoScript::ValueType::None:
        if (signature->returnType != types.Void()) result.value = Scalar::Null();
        break;
    case NeoScript::ValueType::Bool: result.value = Scalar::Bool(call.retBool()); break;
    case NeoScript::ValueType::Int: result.value = Scalar::Int(call.retInt()); break;
    case NeoScript::ValueType::Float: result.value = Scalar::Float(call.retFloat()); break;
    case NeoScript::ValueType::String: result.value = Scalar::String(call.retString().str()); break;
    default: result.error.message = "backend returned an unsupported host value"; return result;
    }
    // 실제 값의 타입도 대조하여 변환 누락을 호스트로 조용히 전달하지 않는다.
    TypeId actual = ScalarType(types, result.value.kind());
    TypeId expected = signature->returnType;
    if (types.IsNullable(expected) && actual != types.Null()) expected = types.Get(expected).element;
    if (expected != actual && !(types.IsNullable(expected) && actual == types.Null()))
        result.error.message = "backend result type disagrees with the static signature of " + name;
    return result;
}

}
