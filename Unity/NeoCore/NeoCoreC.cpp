#include "NeoCoreC.h"
#include "NeoCore.h"
#include "NeoCoreHost.h"

#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <limits>
#include <cstring>

struct CallbackOwner
{
    void* user = nullptr;
    NcRelease release = nullptr;
    ~CallbackOwner() { if (release) release(user); }
};
struct CallbackBinding
{
    NcHostCallback callback = nullptr;
    std::shared_ptr<CallbackOwner> owner;
};
struct OptionsState
{
    std::vector<NeoCore::CompileDefine> defines;
    NeoCore::HostSchema schema;
    std::map<uint32_t, CallbackBinding> callbacks;
    std::map<std::string, uint64_t> globals;
    NcObjectResolver resolver = nullptr;
    std::shared_ptr<CallbackOwner> resolverOwner;
    NcModuleLoader loader = nullptr;
    std::shared_ptr<CallbackOwner> loaderOwner;
    NcHttpTransport transport = nullptr;
    std::shared_ptr<CallbackOwner> transportOwner;
};
struct NcOptions { OptionsState state; std::string error; };
struct NcModuleOutput { NeoCore::ModuleSource value; std::string error; bool supplied = false; };
struct NcHttpRequest { const NeoCore::HttpRequest* value; };
struct NcHttpResponse { NeoCore::HttpResponse* value; };
struct NcHostCall
{
    NeoCore::HostCall* value;
    const NeoCore::TypeRegistry* types;
    const NeoCore::HostBinding* binding;
};

struct NcProgram { std::shared_ptr<OptionsState> options; std::unique_ptr<NeoCore::Program> value; };
struct NcInstance { std::shared_ptr<OptionsState> options; std::unique_ptr<NeoCore::Instance> value; };
struct NcArguments { std::vector<NeoCore::Scalar> values; };
struct NcResult { NeoCore::Result value; };
struct NcValue { NeoCore::Scalar value; mutable std::string typeName; };
struct NcBytes { std::vector<std::uint8_t> value; };

namespace
{
NcLogCallback logCallback=nullptr;
std::shared_ptr<CallbackOwner> logOwner;
void WriteLog(const char* text, bool error)
{
    const auto owner=logOwner;
    const auto callback=logCallback;
    if(callback && owner) { try { callback(owner->user,error?1:0,text,static_cast<uint32_t>(std::strlen(text))); } catch(...) {} }
}
void PrintLog(const char* text) { WriteLog(text,false); }
void ErrorLog(const char* text) { WriteLog(text,true); }
std::string ReadText(const char* text, uint32_t length)
{
    if (!text && length) throw std::invalid_argument("null UTF-8 input with nonzero length");
    return length ? std::string(text, length) : std::string();
}

NcResult* Failure(const char* message)
{
    try
    {
        std::unique_ptr<NcResult> result(new NcResult());
        result->value.status = NeoCore::ExecutionStatus::Failed;
        result->value.error.message = message;
        return result.release();
    }
    catch (...) { return 0; }
}

NcResult* CopyResult(const NeoCore::Result& value)
{
    std::unique_ptr<NcResult> result(new NcResult());
    result->value = value;
    return result.release();
}

NcResult* CopyError(const NeoCore::Error& error)
{
    NeoCore::Result result;
    result.error = error;
    if (!error.ok()) result.status = NeoCore::ExecutionStatus::Failed;
    return CopyResult(result);
}

const char* TextResult(const std::string& text, uint32_t* length)
{
    if (length) *length = static_cast<uint32_t>(text.size());
    return text.c_str();
}

const char* EmptyText(uint32_t* length)
{
    if (length) *length = 0;
    return "";
}

std::shared_ptr<CallbackOwner> Own(void* user, NcRelease release)
{
    std::shared_ptr<CallbackOwner> result(new CallbackOwner());
    result->user = user;
    result->release = release;
    return result;
}
void* ResolveObject(void* user, NeoCore::TypeId type, NeoCore::ObjectHandle object)
{
    OptionsState& state = *static_cast<OptionsState*>(user);
    return state.resolver ? state.resolver(state.resolverOwner->user, type, object.value) : nullptr;
}
class HttpAdapter : public NeoCore::IHttpTransport
{
public:
    explicit HttpAdapter(const std::shared_ptr<OptionsState>& state) : state_(state) {}
    NeoCore::HttpResponse Send(const NeoCore::HttpRequest& request) override
    {
        NeoCore::HttpResponse result;
        NcHttpRequest input = { &request };
        NcHttpResponse output = { &result };
        try { state_->transport(state_->transportOwner->user, &input, &output); }
        catch (...) { result.success = false; result.error = "HTTP transport callback failed"; }
        return result;
    }
private:
    std::shared_ptr<OptionsState> state_;
};
NeoCore::CompileOptions BuildOptions(const std::shared_ptr<OptionsState>& state)
{
    NeoCore::CompileOptions result;
    result.defines=state->defines;
    if (state->loader)
        result.moduleLoader = [state](const std::string& name, const std::string& importer,
            NeoCore::ModuleSource& source, NeoCore::Error& error) {
            NcModuleOutput output;
            const bool accepted = state->loader(state->loaderOwner->user, name.data(), static_cast<uint32_t>(name.size()),
                importer.data(), static_cast<uint32_t>(importer.size()), &output) != 0;
            if (!accepted || !output.supplied || !output.error.empty())
            { error.message = output.error.empty() ? "module loader did not supply source" : output.error; return false; }
            source = std::move(output.value);
            return true;
        };
    if (state->transport) result.httpTransport.reset(new HttpAdapter(state));
    return result;
}
NeoCore::HostBindings BuildBindings(const std::shared_ptr<OptionsState>& state)
{
    NeoCore::HostBindings bindings(state->schema, state->resolver ? &ResolveObject : nullptr, state.get());
    for (const auto& item : state->callbacks)
    {
        const uint32_t id = item.first;
        const CallbackBinding callback = item.second;
        bindings.Bind(id, [state, callback, id](NeoCore::HostCall& call) {
            NcHostCall context = { &call, &state->schema.Types(), state->schema.FindBinding(id) };
            return callback.callback(callback.owner->user, &context) != 0;
        });
    }
    for (const auto& item : state->globals) bindings.BindGlobal(item.first, NeoCore::ObjectHandle(item.second));
    return bindings;
}
NeoCore::TypeKind ArgumentKind(NcHostCall* call, uint32_t index)
{
    if (!call || !call->value || !call->binding || index >= call->binding->parameters.size())
        throw std::invalid_argument("host argument index is out of range");
    NeoCore::TypeId type = call->binding->parameters[index];
    if (call->types->IsNullable(type)) type = call->types->Get(type).element;
    return call->types->Get(type).kind;
}
void RequireArgument(NcHostCall* call, uint32_t index, NeoCore::TypeKind expected)
{
    if (ArgumentKind(call, index) != expected || call->value->IsNull(index))
        throw std::invalid_argument("host argument accessor disagrees with the declared signature");
}
void HostFailure(NcHostCall* call, const char* error)
{
    if (call && call->value) { try { call->value->Fail(error); } catch (...) {} }
}
NcValue* CopyValue(const NeoCore::Scalar& value)
{
    std::unique_ptr<NcValue> result(new NcValue());
    result->value = value;
    return result.release();
}
NcResult* ValueResult(const NeoCore::Scalar& value)
{
    NeoCore::Result result;
    result.value = value;
    return CopyResult(result);
}
const NeoCore::Scalar& RequireValue(const NcValue* value)
{ if (!value) throw std::invalid_argument("value is null"); return value->value; }
std::size_t ValueIndex(uint64_t index)
{
    if (index > (std::numeric_limits<std::size_t>::max)()) throw std::out_of_range("index is not representable on this platform");
    return static_cast<std::size_t>(index);
}
template<class Operation> NcResult* ValueOperation(Operation operation)
{
    try { return ValueResult(operation()); }
    catch (const std::exception& error) { return Failure(error.what()); }
    catch (...) { return Failure("typed value operation failed"); }
}
}

extern "C"
{
NcProgram* NcCompile(const char* source, uint32_t sourceLength,
    const char* sourceName, uint32_t nameLength, int32_t debug, NcResult** result)
{
    return NcCompileWithOptions(source, sourceLength, sourceName, nameLength, debug, nullptr, result);
}

NcProgram* NcCompileWithOptions(const char* source, uint32_t sourceLength,
    const char* sourceName, uint32_t nameLength, int32_t debug, const NcOptions* options, NcResult** result)
{
    if (!result) return 0;
    *result = 0;
    try
    {
        NeoCore::Error error;
        std::unique_ptr<NcProgram> program(new NcProgram());
        program->options.reset(new OptionsState(options ? options->state : OptionsState()));
        const NeoCore::CompileOptions compileOptions = BuildOptions(program->options);
        const NeoCore::HostBindings bindings = BuildBindings(program->options);
        program->value = NeoCore::Program::Compile(ReadText(source, sourceLength), bindings, error,
            ReadText(sourceName, nameLength), debug != 0, compileOptions);
        *result = CopyError(error);
        return program->value ? program.release() : 0;
    }
    catch (const std::exception& error) { *result = Failure(error.what()); }
    catch (...) { *result = Failure("unexpected native compile failure"); }
    return 0;
}

void NcProgramDestroy(NcProgram* program) { delete program; }
uint32_t NcGetAbiVersion(void) { return NEOCORE_C_ABI_VERSION; }
int32_t NcSetLogHandler(NcLogCallback callback, void* userData, NcRelease release)
{
    try
    {
        const auto owner=callback?Own(userData,release):std::shared_ptr<CallbackOwner>();
        NeoCore::SetLogHandler(callback?&PrintLog:nullptr,callback?&ErrorLog:nullptr);
        logCallback=callback;
        logOwner=owner;
        return 1;
    }
    catch(...) { return 0; }
}
int64_t NcProgramCollectCycles(NcProgram* program, int32_t force)
{ try { return program?program->value->CollectCycles(force!=0):0; } catch(...) { return 0; } }
int64_t NcAllocationStat(int32_t index)
{
    try
    {
        const auto stats=NeoCore::GetAllocationStats();
        switch(index)
        {
        case 0:return stats.strings;
        case 1:return stats.maps;
        case 2:return stats.lists;
        case 3:return stats.sets;
        case 4:return stats.coroutines;
        case 5:return stats.modules;
        case 6:return stats.asyncs;
        case 7:return stats.vectors;
        case 8:return stats.arrays;
        case 9:return stats.closures;
        case 10:return stats.bufferBytes;
        case 11:return stats.poolBytes;
        case 12:return stats.stringIdleBytes;
        default:return 0;
        }
    }
    catch(...) { return 0; }
}
int64_t NcProgramTrimMemory(NcProgram* program, int32_t force)
{ try { return program ? program->value->TrimMemory(force != 0) : 0; } catch (...) { return 0; } }
void NcProgramSetEmptyPageHoldSeconds(NcProgram* program, float seconds)
{ try { if (program) program->value->SetEmptyPageHoldSeconds(seconds); } catch (...) {} }
void NcProgramSetTrimPagesPerCall(NcProgram* program, int32_t pages)
{ try { if (program) program->value->SetTrimPagesPerCall(pages); } catch (...) {} }

NcInstance* NcInstanceCreate(NcProgram* program, NcResult** result)
{
    if (!result) return 0;
    *result = 0;
    try
    {
        if (!program) throw std::invalid_argument("program is null");
        NeoCore::Error error;
        std::unique_ptr<NcInstance> instance(new NcInstance());
        instance->options = program->options;
        instance->value = program->value->CreateInstance(error);
        *result = CopyError(error);
        return instance->value ? instance.release() : 0;
    }
    catch (const std::exception& error) { *result = Failure(error.what()); }
    catch (...) { *result = Failure("unexpected native instance failure"); }
    return 0;
}

void NcInstanceDestroy(NcInstance* instance)
{ NcInstanceTryDestroy(instance); }
int32_t NcInstanceTryDestroy(NcInstance* instance)
{ if (instance && instance->value->IsExecuting()) return 0; delete instance; return 1; }

NcResult* NcRun(NcInstance* instance)
{
    try
    {
        if (!instance) return Failure("instance is null");
        return CopyResult(instance->value->Run());
    }
    catch (const std::exception& error) { return Failure(error.what()); }
    catch (...) { return Failure("unexpected native execution failure"); }
}

NcResult* NcCall(NcInstance* instance, const char* name, uint32_t nameLength,
    const NcArguments* arguments)
{ return NcCallLimited(instance, name, nameLength, arguments, -1, 0); }

NcResult* NcCallLimited(NcInstance* instance, const char* name, uint32_t nameLength,
    const NcArguments* arguments, int32_t timeoutMs, uint32_t instructionBudget)
{
    try
    {
        if (!instance) return Failure("instance is null");
        const std::vector<NeoCore::Scalar> empty;
        NeoCore::ExecutionLimits limits;
        limits.timeoutMs = timeoutMs;
        limits.instructionBudget = instructionBudget;
        return CopyResult(instance->value->Call(ReadText(name, nameLength),
            arguments ? arguments->values : empty, limits));
    }
    catch (const std::exception& error) { return Failure(error.what()); }
    catch (...) { return Failure("unexpected native call failure"); }
}

NcResult* NcResume(NcInstance* instance, double deltaTime)
{ return NcResumeBudget(instance, deltaTime, 0); }
NcResult* NcResumeBudget(NcInstance* instance, double deltaTime, uint32_t instructionBudget)
{
    try
    {
        if (!instance) return Failure("instance is null");
        return CopyResult(instance->value->Resume(deltaTime, instructionBudget));
    }
    catch (const std::exception& error) { return Failure(error.what()); }
    catch (...) { return Failure("unexpected native resume failure"); }
}

NcResult* NcCancel(NcInstance* instance)
{
    try { if (!instance) return Failure("instance is null"); NeoCore::Error error; instance->value->Cancel(error); return CopyError(error); }
    catch (const std::exception& error) { return Failure(error.what()); }
    catch (...) { return Failure("native cancellation failed"); }
}
NcResult* NcReset(NcInstance* instance)
{
    try { if (!instance) return Failure("instance is null"); NeoCore::Error error; instance->value->Reset(error); return CopyError(error); }
    catch (const std::exception& error) { return Failure(error.what()); }
    catch (...) { return Failure("native reset failed"); }
}
int32_t NcIsSuspended(const NcInstance* instance) { return instance && instance->value->IsSuspended(); }
int32_t NcIsRunning(const NcInstance* instance) { return instance && instance->value->IsExecuting(); }
uint64_t NcInstanceId(const NcInstance* instance) { return instance ? instance->value->Id() : 0; }
NcResult* NcInstanceSetHostObject(NcInstance* instance, const char* name, uint32_t length, uint64_t id)
{
    try
    {
        if (!instance) return Failure("instance is null");
        NeoCore::Error error;
        instance->value->SetHostObject(ReadText(name, length), NeoCore::ObjectHandle(id), error);
        return CopyError(error);
    }
    catch (const std::exception& error) { return Failure(error.what()); }
    catch (...) { return Failure("native host object binding failed"); }
}

NcArguments* NcArgumentsCreate(void)
{
    try { return new NcArguments(); }
    catch (...) { return 0; }
}
void NcArgumentsDestroy(NcArguments* arguments) { delete arguments; }

int32_t NcArgumentsInt(NcArguments* arguments, int64_t value)
{
    try { if (!arguments) return 0; arguments->values.push_back(NeoCore::Scalar::Int(value)); return 1; }
    catch (...) { return 0; }
}
int32_t NcArgumentsFloat(NcArguments* arguments, double value)
{
    try { if (!arguments) return 0; arguments->values.push_back(NeoCore::Scalar::Float(value)); return 1; }
    catch (...) { return 0; }
}
int32_t NcArgumentsBool(NcArguments* arguments, int32_t value)
{
    try { if (!arguments) return 0; arguments->values.push_back(NeoCore::Scalar::Bool(value != 0)); return 1; }
    catch (...) { return 0; }
}
int32_t NcArgumentsString(NcArguments* arguments, const char* value, uint32_t length)
{
    try { if (!arguments) return 0; arguments->values.push_back(NeoCore::Scalar::String(ReadText(value, length))); return 1; }
    catch (...) { return 0; }
}
int32_t NcArgumentsNull(NcArguments* arguments)
{
    try { if (!arguments) return 0; arguments->values.push_back(NeoCore::Scalar::Null()); return 1; }
    catch (...) { return 0; }
}
int32_t NcArgumentsVector(NcArguments* arguments, int32_t kind, const float* components)
{
    try
    {
        if (!arguments || kind < NC_VECTOR2 || kind > NC_QUATERNION) return 0;
        arguments->values.push_back(NeoCore::Scalar::Vector(static_cast<NeoCore::Scalar::Kind>(kind), components));
        return 1;
    }
    catch (...) { return 0; }
}
int32_t NcArgumentsObject(NcArguments* arguments, const char* typeName, uint32_t length, uint64_t id)
{
    try { if (!arguments) return 0; arguments->values.push_back(NeoCore::Scalar::Object(ReadText(typeName, length), id)); return 1; }
    catch (...) { return 0; }
}
int32_t NcArgumentsValue(NcArguments* arguments, const NcValue* value)
{ try { if (!arguments || !value) return 0; arguments->values.push_back(value->value); return 1; } catch (...) { return 0; } }

void NcResultDestroy(NcResult* result) { delete result; }
int32_t NcResultStatus(const NcResult* result)
{
    if (!result) return NC_FAILED;
    switch (result->value.status)
    {
    case NeoCore::ExecutionStatus::Completed: return NC_COMPLETED;
    case NeoCore::ExecutionStatus::Suspended: return NC_SUSPENDED;
    case NeoCore::ExecutionStatus::Failed: return NC_FAILED;
    }
    return NC_FAILED;
}
int32_t NcResultKind(const NcResult* result)
{
    if (!result) return NC_VOID;
    switch (result->value.value.kind())
    {
    case NeoCore::Scalar::Kind::Void: return NC_VOID;
    case NeoCore::Scalar::Kind::Int: return NC_INT;
    case NeoCore::Scalar::Kind::Float: return NC_FLOAT;
    case NeoCore::Scalar::Kind::Bool: return NC_BOOL;
    case NeoCore::Scalar::Kind::String: return NC_STRING;
    case NeoCore::Scalar::Kind::Null: return NC_NULL;
    case NeoCore::Scalar::Kind::Vector2: return NC_VECTOR2;
    case NeoCore::Scalar::Kind::Vector3: return NC_VECTOR3;
    case NeoCore::Scalar::Kind::Vector4: return NC_VECTOR4;
    case NeoCore::Scalar::Kind::Quaternion: return NC_QUATERNION;
    case NeoCore::Scalar::Kind::Object: return NC_OBJECT;
    case NeoCore::Scalar::Kind::List: return NC_LIST;
    case NeoCore::Scalar::Kind::Map: return NC_MAP;
    case NeoCore::Scalar::Kind::Set: return NC_SET;
    case NeoCore::Scalar::Kind::Array: return NC_ARRAY;
    case NeoCore::Scalar::Kind::Function: return NC_FUNCTION;
    case NeoCore::Scalar::Kind::Struct: return NC_STRUCT;
    }
    return NC_VOID;
}
int64_t NcResultInt(const NcResult* result)
{
    return result && NcResultKind(result) == NC_INT ? result->value.value.asInt() : 0;
}
double NcResultFloat(const NcResult* result)
{
    return result && NcResultKind(result) == NC_FLOAT ? result->value.value.asFloat() : 0;
}
int32_t NcResultBool(const NcResult* result)
{
    return result && NcResultKind(result) == NC_BOOL && result->value.value.asBool() ? 1 : 0;
}
int32_t NcResultVector(const NcResult* result, float* components, uint32_t capacity)
{
    const int32_t kind = NcResultKind(result);
    if (!components || kind < NC_VECTOR2 || kind > NC_QUATERNION) return 0;
    const uint32_t count = kind == NC_VECTOR2 ? 2 : kind == NC_VECTOR3 ? 3 : 4;
    if (capacity < count) return 0;
    std::copy(result->value.value.asVector(), result->value.value.asVector() + count, components);
    return 1;
}
uint64_t NcResultObject(const NcResult* result)
{ return NcResultKind(result) == NC_OBJECT ? result->value.value.asObject() : 0; }
const char* NcResultObjectType(const NcResult* result, uint32_t* length)
{ return NcResultKind(result) == NC_OBJECT ? TextResult(result->value.value.objectType(), length) : EmptyText(length); }
const char* NcResultString(const NcResult* result, uint32_t* length)
{
    return result && NcResultKind(result) == NC_STRING
        ? TextResult(result->value.value.asString(), length) : EmptyText(length);
}
const char* NcResultError(const NcResult* result, uint32_t* length)
{
    return result ? TextResult(result->value.error.message, length) : EmptyText(length);
}
const char* NcResultSource(const NcResult* result, uint32_t* length)
{
    return result ? TextResult(result->value.error.sourceName, length) : EmptyText(length);
}
int32_t NcResultLine(const NcResult* result) { return result ? result->value.error.line : 0; }
int32_t NcResultColumn(const NcResult* result) { return result ? result->value.error.column : 0; }
NcValue* NcResultValue(const NcResult* result)
{ try { return result ? CopyValue(result->value.value) : nullptr; } catch (...) { return nullptr; } }
NcValue* NcArgumentsValueAt(const NcArguments* arguments, uint32_t index)
{ try { return arguments && index < arguments->values.size() ? CopyValue(arguments->values[index]) : nullptr; } catch (...) { return nullptr; } }
void NcValueDestroy(NcValue* value) { delete value; }
NcValue* NcValueClone(const NcValue* value)
{ try { return value ? CopyValue(value->value.Clone()) : nullptr; } catch (...) { return nullptr; } }
int32_t NcValueKind(const NcValue* value) { return value ? static_cast<int32_t>(value->value.kind()) : NC_VOID; }
const char* NcValueType(const NcValue* value, uint32_t* length)
{
    try { if (!value) return EmptyText(length); value->typeName=value->value.typeName(); return TextResult(value->typeName,length); }
    catch (...) { return EmptyText(length); }
}
int64_t NcValueInt(const NcValue* value) { return NcValueKind(value)==NC_INT?value->value.asInt():0; }
double NcValueFloat(const NcValue* value) { return NcValueKind(value)==NC_FLOAT?value->value.asFloat():0; }
int32_t NcValueBool(const NcValue* value) { return NcValueKind(value)==NC_BOOL && value->value.asBool(); }
const char* NcValueString(const NcValue* value, uint32_t* length)
{ return NcValueKind(value)==NC_STRING?TextResult(value->value.asString(),length):EmptyText(length); }
int32_t NcValueVector(const NcValue* value, float* components, uint32_t capacity)
{
    const int32_t kind=NcValueKind(value);
    if(!components || kind<NC_VECTOR2 || kind>NC_QUATERNION) return 0;
    const uint32_t count=kind==NC_VECTOR2?2:kind==NC_VECTOR3?3:4;
    if(capacity<count) return 0;
    std::copy(value->value.asVector(),value->value.asVector()+count,components);
    return 1;
}
uint64_t NcValueObject(const NcValue* value) { return NcValueKind(value)==NC_OBJECT?value->value.asObject():0; }
uint64_t NcValueCount(const NcValue* value)
{ try { return RequireValue(value).Count(); } catch (...) { return 0; } }
int32_t NcValueSameStorage(const NcValue* first, const NcValue* second)
{ return first && second && first->value.SameStorage(second->value); }
NcResult* NcValueCreate(NcProgram* program, const char* typeName, uint32_t length)
{
    try
    {
        if(!program) return Failure("program is null");
        NeoCore::Error error;
        NeoCore::Scalar value=program->value->CreateValue(ReadText(typeName,length),error);
        return error.ok()?ValueResult(value):CopyError(error);
    }
    catch(const std::exception& error) { return Failure(error.what()); }
    catch(...) { return Failure("typed value creation failed"); }
}
NcResult* NcValueCreateFields(NcProgram* program, const char* typeName, uint32_t length, const NcArguments* fields)
{
    return ValueOperation([&] {
        if(!program || !fields) throw std::invalid_argument("program and fields are required");
        NeoCore::Error error;
        const auto value=program->value->CreateValue(ReadText(typeName,length),fields->values,error);
        if(!error.ok()) throw std::invalid_argument(error.message);
        return value;
    });
}
NcResult* NcParseJson(NcProgram* program, const char* typeName, uint32_t typeLength, const char* json, uint32_t jsonLength, int32_t strictIntegers)
{
    return ValueOperation([&] {
        if(!program) throw std::invalid_argument("program is null");
        NeoCore::Error error;
        const auto value=program->value->ParseJson(ReadText(typeName,typeLength),ReadText(json,jsonLength),error,strictIntegers!=0);
        if(!error.ok()) throw std::invalid_argument(error.message);
        return value;
    });
}
NcResult* NcFindFunction(NcProgram* program, const char* name, uint32_t length)
{
    return ValueOperation([&] {
        if(!program) throw std::invalid_argument("program is null");
        NeoCore::Error error;
        const auto function=program->value->FindFunction(ReadText(name,length),error);
        if(!error.ok()) throw std::invalid_argument(error.message);
        return function;
    });
}
NcResult* NcToJson(NcProgram* program, const NcValue* value, int32_t sortKeys)
{
    return ValueOperation([&] {
        if(!program) throw std::invalid_argument("program is null");
        NeoCore::Error error;
        const auto json=program->value->ToJson(RequireValue(value),error,sortKeys!=0);
        if(!error.ok()) throw std::invalid_argument(error.message);
        return NeoCore::Scalar::String(json);
    });
}
NcResult* NcValueAt(const NcValue* value, uint64_t index)
{ return ValueOperation([&] { return RequireValue(value).At(ValueIndex(index)); }); }
NcResult* NcValueKeyAt(const NcValue* value, uint64_t index)
{ return ValueOperation([&] { return RequireValue(value).KeyAt(ValueIndex(index)); }); }
NcResult* NcValueGet(const NcValue* value, const NcValue* key)
{ return ValueOperation([&] { return RequireValue(value).Get(RequireValue(key)); }); }
NcResult* NcValueContains(const NcValue* value, const NcValue* key)
{ return ValueOperation([&] { return NeoCore::Scalar::Bool(RequireValue(value).Contains(RequireValue(key))); }); }
NcResult* NcValueSet(const NcValue* value, uint64_t index, const NcValue* element)
{ return ValueOperation([&] { RequireValue(value).Set(ValueIndex(index),RequireValue(element)); return NeoCore::Scalar(); }); }
NcResult* NcValuePut(const NcValue* value, const NcValue* key, const NcValue* element)
{ return ValueOperation([&] { RequireValue(value).Put(RequireValue(key),RequireValue(element)); return NeoCore::Scalar(); }); }
NcResult* NcValueAppend(const NcValue* value, const NcValue* element)
{ return ValueOperation([&] { RequireValue(value).Append(RequireValue(element)); return NeoCore::Scalar(); }); }
NcResult* NcValueRemove(const NcValue* value, const NcValue* key)
{ return ValueOperation([&] { return NeoCore::Scalar::Bool(RequireValue(value).Remove(RequireValue(key))); }); }
NcResult* NcValueResize(const NcValue* value, uint64_t count)
{ return ValueOperation([&] { RequireValue(value).Resize(ValueIndex(count)); return NeoCore::Scalar(); }); }
NcResult* NcValueReserve(const NcValue* value, uint64_t count)
{ return ValueOperation([&] { RequireValue(value).Reserve(ValueIndex(count)); return NeoCore::Scalar(); }); }
NcResult* NcValueRemoveAt(const NcValue* value, uint64_t index)
{ return ValueOperation([&] { RequireValue(value).RemoveAt(ValueIndex(index)); return NeoCore::Scalar(); }); }
#define NC_ARRAY_COPY(suffix, cppType) \
NcResult* NcArrayRead##suffix(const NcValue* value, uint64_t offset, cppType* output, uint64_t count) \
{ return ValueOperation([&] { RequireValue(value).ReadArray(output,ValueIndex(count),ValueIndex(offset)); return NeoCore::Scalar(); }); } \
NcResult* NcArrayWrite##suffix(const NcValue* value, uint64_t offset, const cppType* input, uint64_t count) \
{ return ValueOperation([&] { RequireValue(value).WriteArray(input,ValueIndex(count),ValueIndex(offset)); return NeoCore::Scalar(); }); }
NC_ARRAY_COPY(Int64,int64_t)
NC_ARRAY_COPY(Float64,double)
NC_ARRAY_COPY(Int32,int32_t)
NC_ARRAY_COPY(Float32,float)
#undef NC_ARRAY_COPY
NcResult* NcValueClear(const NcValue* value)
{ return ValueOperation([&] { RequireValue(value).Clear(); return NeoCore::Scalar(); }); }
NcResult* NcCallFunction(NcInstance* instance, const NcValue* function, const NcArguments* arguments,
    int32_t timeoutMs, uint32_t instructionBudget)
{
    try
    {
        if(!instance) return Failure("instance is null");
        NeoCore::ExecutionLimits limits;
        limits.timeoutMs=timeoutMs;
        limits.instructionBudget=instructionBudget;
        const std::vector<NeoCore::Scalar> empty;
        return CopyResult(instance->value->Call(RequireValue(function),arguments?arguments->values:empty,limits));
    }
    catch(const std::exception& error) { return Failure(error.what()); }
    catch(...) { return Failure("callback invocation failed"); }
}
NcResult* NcReadGlobal(NcInstance* instance, const char* name, uint32_t length)
{
    try { return instance?CopyResult(instance->value->ReadGlobal(ReadText(name,length))):Failure("instance is null"); }
    catch(const std::exception& error) { return Failure(error.what()); }
    catch(...) { return Failure("global read failed"); }
}
NcResult* NcWriteGlobal(NcInstance* instance, const char* name, uint32_t length, const NcValue* value)
{
    try { return instance?CopyResult(instance->value->WriteGlobal(ReadText(name,length),RequireValue(value))):Failure("instance is null"); }
    catch(const std::exception& error) { return Failure(error.what()); }
    catch(...) { return Failure("global write failed"); }
}
NcBytes* NcSaveImage(NcProgram* program, NcResult** result)
{
    if(!result) return nullptr;
    *result=nullptr;
    try
    {
        if(!program) { *result=Failure("program is null"); return nullptr; }
        std::unique_ptr<NcBytes> bytes(new NcBytes());
        NeoCore::Error error;
        const bool saved=program->value->SaveImage(bytes->value,error);
        *result=CopyError(error);
        return saved?bytes.release():nullptr;
    }
    catch(const std::exception& error) { *result=Failure(error.what()); }
    catch(...) { *result=Failure("image save failed"); }
    return nullptr;
}
NcProgram* NcLoadImage(const uint8_t* bytes, uint64_t length, const NcOptions* options, NcResult** result)
{
    if(!result) return nullptr;
    *result=nullptr;
    try
    {
        if(!bytes && length) throw std::invalid_argument("image data is null");
        std::vector<std::uint8_t> image;
        if(length) image.assign(bytes,bytes+ValueIndex(length));
        std::unique_ptr<NcProgram> program(new NcProgram());
        program->options.reset(new OptionsState(options?options->state:OptionsState()));
        const NeoCore::HostBindings bindings=BuildBindings(program->options);
        NeoCore::Error error;
        program->value=NeoCore::Program::LoadImage(image,bindings,error,BuildOptions(program->options));
        *result=CopyError(error);
        return program->value?program.release():nullptr;
    }
    catch(const std::exception& error) { *result=Failure(error.what()); }
    catch(...) { *result=Failure("image load failed"); }
    return nullptr;
}
void NcBytesDestroy(NcBytes* bytes) { delete bytes; }
const uint8_t* NcBytesData(const NcBytes* bytes, uint64_t* length)
{ if(length) *length=bytes?bytes->value.size():0; return bytes && !bytes->value.empty()?bytes->value.data():nullptr; }

NcOptions* NcOptionsCreate(void) { try { return new NcOptions(); } catch (...) { return nullptr; } }
void NcOptionsDestroy(NcOptions* options) { delete options; }
int32_t NcOptionsSetResolver(NcOptions* options, NcObjectResolver resolver, void* userData, NcRelease release)
{
    try
    {
        if (!options || !resolver) return 0;
        options->state.resolverOwner = Own(userData, release);
        options->state.resolver = resolver;
        return 1;
    }
    catch (...) { return 0; }
}
int32_t NcOptionsSetModuleLoader(NcOptions* options, NcModuleLoader loader, void* userData, NcRelease release)
{
    try
    {
        if (!options || !loader) return 0;
        options->state.loaderOwner = Own(userData, release);
        options->state.loader = loader;
        return 1;
    }
    catch (...) { return 0; }
}
int32_t NcOptionsSetHttpTransport(NcOptions* options, NcHttpTransport transport, void* userData, NcRelease release)
{
    try
    {
        if (!options || !transport) return 0;
        options->state.transportOwner = Own(userData, release);
        options->state.transport = transport;
        return 1;
    }
    catch (...) { return 0; }
}
uint32_t NcTypeBuiltin(int32_t kind)
{
    switch (kind)
    {
    case NC_VOID: return static_cast<uint32_t>(NeoCore::TypeKind::Void);
    case NC_INT: return static_cast<uint32_t>(NeoCore::TypeKind::Int);
    case NC_FLOAT: return static_cast<uint32_t>(NeoCore::TypeKind::Float);
    case NC_BOOL: return static_cast<uint32_t>(NeoCore::TypeKind::Bool);
    case NC_STRING: return static_cast<uint32_t>(NeoCore::TypeKind::String);
    case NC_NULL: return static_cast<uint32_t>(NeoCore::TypeKind::Null);
    case NC_VECTOR2: return static_cast<uint32_t>(NeoCore::TypeKind::Vector2);
    case NC_VECTOR3: return static_cast<uint32_t>(NeoCore::TypeKind::Vector3);
    case NC_VECTOR4: return static_cast<uint32_t>(NeoCore::TypeKind::Vector4);
    case NC_QUATERNION: return static_cast<uint32_t>(NeoCore::TypeKind::Quaternion);
    default: return NeoCore::InvalidType;
    }
}
int32_t NcOptionsDefine(NcOptions* options, const char* name, uint32_t nameLength, int32_t kind, const char* text, uint32_t textLength)
{
    try
    {
        if(!options || kind<NC_DEFINE_IDENTIFIER || kind>NC_DEFINE_NULL) return 0;
        NeoCore::CompileDefine define;
        define.name=ReadText(name,nameLength);
        define.kind=static_cast<NeoCore::CompileDefineKind>(kind);
        define.text=ReadText(text,textLength);
        options->state.defines.push_back(std::move(define));
        options->error.clear();
        return 1;
    }
    catch(const std::exception& error) { if(options) options->error=error.what(); return 0; }
    catch(...) { return 0; }
}
uint32_t NcTypeNullable(NcOptions* options, uint32_t element)
{ try { return options ? options->state.schema.Types().NullableOf(element) : 0; } catch (...) { return 0; } }
uint32_t NcTypeCollection(NcOptions* options, int32_t kind, uint32_t element, uint32_t key, uint32_t value)
{
    try
    {
        if(!options) return 0;
        NeoCore::TypeRegistry& types=options->state.schema.Types();
        switch(kind)
        {
        case NC_LIST: return types.ListOf(element);
        case NC_MAP: return types.MapOf(key,value);
        case NC_SET: return types.SetOf(element);
        case NC_ARRAY: return types.ArrayOf(element);
        case NC_ARRAY32: return types.Array32Of(element);
        default: return 0;
        }
    }
    catch(...) { return 0; }
}
uint32_t NcTypeFunction(NcOptions* options, uint32_t resultType, const uint32_t* parameters, uint32_t count)
{
    try
    {
        if(!options || (!parameters && count)) return 0;
        std::vector<NeoCore::TypeId> arguments;
        if(count) arguments.assign(parameters,parameters+count);
        return options->state.schema.Types().FunctionOf(resultType,arguments);
    }
    catch(...) { return 0; }
}
uint32_t NcSchemaObject(NcOptions* options, const char* name, uint32_t length)
{
    try
    {
        if (!options) return 0;
        const uint32_t id = options->state.schema.AddObject(ReadText(name, length));
        options->error = options->state.schema.LastError();
        return id;
    }
    catch (const std::exception& error) { if (options) options->error = error.what(); return 0; }
    catch (...) { return 0; }
}
uint32_t NcSchemaStruct(NcOptions* options, const char* name, uint32_t length)
{
    try
    {
        if(!options) return 0;
        const uint32_t type=options->state.schema.AddStruct(ReadText(name,length));
        options->error=options->state.schema.LastError();
        return type;
    }
    catch(const std::exception& error) { if(options) options->error=error.what(); return 0; }
    catch(...) { return 0; }
}
int32_t NcSchemaStructFields(NcOptions* options, uint32_t type, const NcField* fields, uint32_t count)
{
    try
    {
        if(!options || (!fields && count)) return 0;
        std::vector<NeoCore::StructField> values;
        for(uint32_t i=0;i<count;++i) values.push_back(NeoCore::StructField(ReadText(fields[i].name,fields[i].length),fields[i].type));
        const bool result=options->state.schema.DefineStruct(type,values);
        options->error=options->state.schema.LastError();
        return result?1:0;
    }
    catch(const std::exception& error) { if(options) options->error=error.what(); return 0; }
    catch(...) { return 0; }
}
uint32_t NcSchemaMethod(NcOptions* options, uint32_t owner, const char* name, uint32_t length,
    uint32_t resultType, const uint32_t* parameters, uint32_t count,
    NcHostCallback callback, void* userData, NcRelease release)
{
    try
    {
        if (!options || !callback || (!parameters && count)) return 0;
        NeoCore::HostSchema schema = options->state.schema;
        std::vector<NeoCore::TypeId> arguments;
        if (count) arguments.assign(parameters, parameters + count);
        const uint32_t id = schema.AddMethod(owner, ReadText(name, length), resultType, arguments);
        if (!id) { options->error = schema.LastError(); return 0; }
        CallbackBinding binding;
        binding.callback = callback;
        binding.owner = Own(userData, nullptr);
        options->state.callbacks[id] = binding;
        options->state.schema = std::move(schema);
        binding.owner->release = release;
        options->error.clear();
        return id;
    }
    catch (const std::exception& error) { if (options) options->error = error.what(); return 0; }
    catch (...) { return 0; }
}
uint32_t NcSchemaEvent(NcOptions* options, uint32_t owner, const char* eventName, uint32_t eventLength,
    uint32_t callbackType, const char* registrationMethod, uint32_t methodLength,
    NcHostCallback callback, void* userData, NcRelease release)
{
    try
    {
        if (!options || !callback) return 0;
        NeoCore::HostSchema schema = options->state.schema;
        const uint32_t id = schema.AddEvent(owner, ReadText(eventName, eventLength), callbackType,
            ReadText(registrationMethod, methodLength));
        if (!id) { options->error = schema.LastError(); return 0; }
        std::map<uint32_t, CallbackBinding> callbacks = options->state.callbacks;
        CallbackBinding binding;
        binding.callback = callback;
        binding.owner = Own(userData, nullptr);
        callbacks[id] = binding;
        options->state.callbacks = std::move(callbacks);
        options->state.schema = std::move(schema);
        binding.owner->release = release;
        options->error.clear();
        return id;
    }
    catch (const std::exception& error) { if (options) options->error = error.what(); return 0; }
    catch (...) { return 0; }
}
int32_t NcSchemaProperty(NcOptions* options, uint32_t owner, const char* name, uint32_t length,
    uint32_t type, NcHostCallback getter, NcHostCallback setter, void* userData, NcRelease release)
{
    try
    {
        if (!options || (!getter && !setter)) return 0;
        NeoCore::HostSchema schema = options->state.schema;
        const std::string propertyName = ReadText(name, length);
        if (!schema.AddProperty(owner, propertyName, type, getter != nullptr, setter != nullptr))
        { options->error = schema.LastError(); return 0; }
        const NeoCore::HostProperty* property = schema.FindProperty(owner, propertyName);
        std::map<uint32_t, CallbackBinding> callbacks = options->state.callbacks;
        CallbackBinding binding;
        binding.owner = Own(userData, nullptr);
        if (getter) { binding.callback = getter; callbacks[property->getter] = binding; }
        if (setter) { binding.callback = setter; callbacks[property->setter] = binding; }
        options->state.callbacks = std::move(callbacks);
        options->state.schema = std::move(schema);
        binding.owner->release = release;
        options->error.clear();
        return 1;
    }
    catch (const std::exception& error) { if (options) options->error = error.what(); return 0; }
    catch (...) { return 0; }
}
int32_t NcSchemaGlobal(NcOptions* options, const char* name, uint32_t length, uint32_t type, uint64_t id)
{
    try
    {
        if (!options) return 0;
        const std::string globalName = ReadText(name, length);
        NeoCore::HostSchema schema = options->state.schema;
        if (!schema.AddGlobal(globalName, type) || (!id && !schema.Types().IsNullable(type)))
        { options->error = schema.LastError().empty() ? "nonnullable host global needs a nonzero ID" : schema.LastError(); return 0; }
        options->state.globals[globalName] = id;
        options->state.schema = std::move(schema);
        options->error.clear();
        return 1;
    }
    catch (const std::exception& error) { if (options) options->error = error.what(); return 0; }
    catch (...) { return 0; }
}
const char* NcOptionsError(const NcOptions* options, uint32_t* length)
{ return options ? TextResult(options->error, length) : EmptyText(length); }
uint32_t NcHostArgumentCount(const NcHostCall* call)
{ return call && call->value ? static_cast<uint32_t>(call->value->ArgumentCount()) : 0; }
int32_t NcHostIsNull(const NcHostCall* call, uint32_t index)
{ try { return call && call->value && index < call->value->ArgumentCount() && call->value->IsNull(index); } catch (...) { return 0; } }
NcValue* NcHostArgument(NcHostCall* call, uint32_t index)
{
    try { return call?CopyValue(call->value->Argument(index)):nullptr; }
    catch(const std::exception& error) { HostFailure(call,error.what()); return nullptr; }
    catch(...) { HostFailure(call,"host value capture failed"); return nullptr; }
}
void NcHostReturnValue(NcHostCall* call, const NcValue* value)
{ try { if(call) call->value->ReturnValue(RequireValue(value)); } catch(...) { HostFailure(call,"host value return failed"); } }
NcResult* NcHostCallFunction(NcHostCall* call, const NcValue* function, const NcArguments* arguments)
{
    try
    {
        if(!call) return Failure("host context is null");
        const std::vector<NeoCore::Scalar> empty;
        return CopyResult(call->value->CallScript(RequireValue(function),arguments?arguments->values:empty));
    }
    catch(const std::exception& error) { HostFailure(call,error.what()); return Failure(error.what()); }
    catch(...) { HostFailure(call,"nested callback failed"); return Failure("nested callback failed"); }
}
int64_t NcHostInt(NcHostCall* call, uint32_t index)
{
    try { RequireArgument(call, index, NeoCore::TypeKind::Int); return call->value->Int(index); }
    catch (const std::exception& error) { HostFailure(call, error.what()); return 0; }
    catch (...) { HostFailure(call, "host argument access failed"); return 0; }
}
double NcHostFloat(NcHostCall* call, uint32_t index)
{
    try { RequireArgument(call, index, NeoCore::TypeKind::Float); return call->value->Float(index); }
    catch (const std::exception& error) { HostFailure(call, error.what()); return 0; }
    catch (...) { HostFailure(call, "host argument access failed"); return 0; }
}
int32_t NcHostBool(NcHostCall* call, uint32_t index)
{
    try { RequireArgument(call, index, NeoCore::TypeKind::Bool); return call->value->Bool(index); }
    catch (const std::exception& error) { HostFailure(call, error.what()); return 0; }
    catch (...) { HostFailure(call, "host argument access failed"); return 0; }
}
const char* NcHostString(NcHostCall* call, uint32_t index, uint32_t* length)
{
    try { RequireArgument(call, index, NeoCore::TypeKind::String); return TextResult(call->value->String(index), length); }
    catch (const std::exception& error) { HostFailure(call, error.what()); return EmptyText(length); }
    catch (...) { HostFailure(call, "host argument access failed"); return EmptyText(length); }
}
int32_t NcHostVector(NcHostCall* call, uint32_t index, float* output, uint32_t capacity)
{
    try
    {
        const NeoCore::TypeKind kind = ArgumentKind(call, index);
        if (!output || call->value->IsNull(index)) throw std::invalid_argument("invalid vector output or null argument");
        const uint32_t count = kind == NeoCore::TypeKind::Vector2 ? 2 : kind == NeoCore::TypeKind::Vector3 ? 3 : 4;
        if (capacity < count) throw std::invalid_argument("vector output is too small");
        switch (kind)
        {
        case NeoCore::TypeKind::Vector2:
        { const auto v = call->value->Vector2(index); output[0]=v.x; output[1]=v.y; break; }
        case NeoCore::TypeKind::Vector3:
        { const auto v = call->value->Vector3(index); output[0]=v.x; output[1]=v.y; output[2]=v.z; break; }
        case NeoCore::TypeKind::Vector4:
        { const auto v = call->value->Vector4(index); output[0]=v.x; output[1]=v.y; output[2]=v.z; output[3]=v.w; break; }
        case NeoCore::TypeKind::Quaternion:
        { const auto v = call->value->Quaternion(index); output[0]=v.w; output[1]=v.x; output[2]=v.y; output[3]=v.z; break; }
        default: throw std::invalid_argument("host argument is not a vector");
        }
        return 1;
    }
    catch (const std::exception& error) { HostFailure(call, error.what()); return 0; }
    catch (...) { HostFailure(call, "host vector access failed"); return 0; }
}
uint64_t NcHostObject(NcHostCall* call, uint32_t index)
{
    try { RequireArgument(call, index, NeoCore::TypeKind::Native); return call->value->ObjectId(index).value; }
    catch (const std::exception& error) { HostFailure(call, error.what()); return 0; }
    catch (...) { HostFailure(call, "host object access failed"); return 0; }
}
uint64_t NcHostReceiverId(const NcHostCall* call) { return call && call->value ? call->value->ReceiverId().value : 0; }
uint64_t NcHostCallerInstanceId(const NcHostCall* call)
{ return call && call->value ? call->value->CallerInstance().Id() : 0; }
void* NcHostResolveReceiver(NcHostCall* call)
{ try { return call && call->value ? call->value->ResolveReceiver() : nullptr; } catch (...) { HostFailure(call, "host resolver failed"); return nullptr; } }
void* NcHostResolveObject(NcHostCall* call, uint32_t index)
{
    try { RequireArgument(call, index, NeoCore::TypeKind::Native); return call->value->ResolveObject(index); }
    catch (const std::exception& error) { HostFailure(call, error.what()); return nullptr; }
    catch (...) { HostFailure(call, "host resolver failed"); return nullptr; }
}
void NcHostReturnInt(NcHostCall* call, int64_t value) { if (call) call->value->ReturnInt(value); }
void NcHostReturnFloat(NcHostCall* call, double value) { if (call) call->value->ReturnFloat(value); }
void NcHostReturnBool(NcHostCall* call, int32_t value) { if (call) call->value->ReturnBool(value != 0); }
void NcHostReturnString(NcHostCall* call, const char* value, uint32_t length)
{ try { if (call) call->value->ReturnString(ReadText(value, length)); } catch (...) { HostFailure(call, "host string result failed"); } }
void NcHostReturnVector(NcHostCall* call, int32_t kind, const float* v)
{
    if (!call) return;
    if (!v) { HostFailure(call, "vector result is null"); return; }
    switch (kind)
    {
    case NC_VECTOR2: call->value->ReturnVector2(NeoCore::Vector2Value(v[0],v[1])); break;
    case NC_VECTOR3: call->value->ReturnVector3(NeoCore::Vector3Value(v[0],v[1],v[2])); break;
    case NC_VECTOR4: call->value->ReturnVector4(NeoCore::Vector4Value(v[0],v[1],v[2],v[3])); break;
    case NC_QUATERNION: call->value->ReturnQuaternion(NeoCore::QuaternionValue(v[0],v[1],v[2],v[3])); break;
    default: HostFailure(call, "invalid vector result kind"); break;
    }
}
void NcHostReturnObject(NcHostCall* call, uint64_t id) { if (call) call->value->ReturnObject(NeoCore::ObjectHandle(id)); }
void NcHostReturnNull(NcHostCall* call) { if (call) call->value->ReturnNull(); }
void NcHostFail(NcHostCall* call, const char* error, uint32_t length)
{ try { if (call) call->value->Fail(ReadText(error, length)); } catch (...) { HostFailure(call, "host callback failed"); } }
NcResult* NcHostCallScript(NcHostCall* call, const char* name, uint32_t length, const NcArguments* arguments)
{
    try
    {
        if (!call) return Failure("host call context is null");
        const std::vector<NeoCore::Scalar> empty;
        return CopyResult(call->value->CallScript(ReadText(name, length), arguments ? arguments->values : empty));
    }
    catch (const std::exception& error) { HostFailure(call, error.what()); return Failure(error.what()); }
    catch (...) { HostFailure(call, "nested script call failed"); return Failure("nested script call failed"); }
}
void NcModuleSource(NcModuleOutput* output, const char* source, uint32_t length, const char* name, uint32_t nameLength)
{
    if (!output) return;
    try { output->value.source = ReadText(source,length); output->value.sourceName = ReadText(name,nameLength); output->supplied = true; }
    catch (...) { output->error = "module source could not be copied"; }
}
void NcModuleError(NcModuleOutput* output, const char* error, uint32_t length)
{ try { if (output) output->error = ReadText(error,length); } catch (...) {} }
const char* NcHttpMethod(const NcHttpRequest* request, uint32_t* length)
{ return request ? TextResult(request->value->method,length) : EmptyText(length); }
const char* NcHttpUrl(const NcHttpRequest* request, uint32_t* length)
{ return request ? TextResult(request->value->url,length) : EmptyText(length); }
const char* NcHttpBody(const NcHttpRequest* request, uint32_t* length)
{ return request ? TextResult(request->value->body,length) : EmptyText(length); }
uint32_t NcHttpHeaderCount(const NcHttpRequest* request) { return request ? static_cast<uint32_t>(request->value->headers.size()) : 0; }
const char* NcHttpHeaderName(const NcHttpRequest* request, uint32_t index, uint32_t* length)
{ return request && index < request->value->headers.size() ? TextResult(request->value->headers[index].first,length) : EmptyText(length); }
const char* NcHttpHeaderValue(const NcHttpRequest* request, uint32_t index, uint32_t* length)
{ return request && index < request->value->headers.size() ? TextResult(request->value->headers[index].second,length) : EmptyText(length); }
int32_t NcHttpTimeout(const NcHttpRequest* request) { return request ? request->value->timeoutMs : 0; }
int32_t NcHttpIsCancelled(const NcHttpRequest* request) { return !request || request->value->IsCancelled(); }
void NcHttpComplete(NcHttpResponse* response, int32_t success, const char* body, uint32_t length,
    const char* error, uint32_t errorLength)
{
    if (!response) return;
    try
    {
        response->value->body = ReadText(body,length);
        response->value->error = ReadText(error,errorLength);
        response->value->success = success != 0;
    }
    catch (...) { response->value->success = false; }
}
}
