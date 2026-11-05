#include <cassert>
#include "NeoCoreHostRuntime.h"
#include "NeoCoreHostBridge.h"
#include "NeoCoreInstanceContext.h"
#include "NeoCoreValueBoundary.h"
#include "NeoScript.h"
#include <cassert>
#include <cstring>
#include <exception>
#include <utility>

namespace NeoCore
{

HostBindings::HostBindings(const HostSchema& schema, HostResolver resolver, void* userData)
    : schema_(schema), resolver_(resolver), userData_(userData),
      functions_(schema.Bindings().size()), globals_(schema.Globals().size()),
      globalBound_(schema.Globals().size(), false) {}

bool HostBindings::Bind(HostBindingId binding, HostFunction function)
{
    error_.clear();
    if (!binding || binding > functions_.size() || !function)
    {
        error_ = "invalid host function binding";
        return false;
    }
    functions_[binding - 1] = std::move(function);
    return true;
}

bool HostBindings::BindGlobal(const std::string& name, ObjectHandle object)
{
    error_.clear();
    const auto& globals = schema_.Globals();
    for (std::size_t i = 0; i < globals.size(); ++i)
    {
        if (globals[i].name != name) continue;
        globals_[i] = object;
        globalBound_[i] = true;
        return true;
    }
    error_ = "unknown host global: " + name;
    return false;
}

bool HostBindings::Validate(std::string& error) const
{
    if (!schema_.Validate(error)) return false;
    if ((!schema_.Bindings().empty() || !schema_.Globals().empty()) && !resolver_)
    {
        error = "host bindings require an object resolver";
        return false;
    }
    for (std::size_t i = 0; i < functions_.size(); ++i)
        if (!functions_[i])
        {
            error = "host member is not linked: " + schema_.Bindings()[i].name;
            return false;
        }
    for (std::size_t i = 0; i < globals_.size(); ++i)
        if (!globalBound_[i])
        {
            error = "host global is not bound: " + schema_.Globals()[i].name;
            return false;
        }
    error.clear();
    return true;
}

namespace
{
// 값의 타입은 바인딩 서명에 있다. 인자마다 VM 태그를 판별하여 변환하지 않는다.
struct HostArgument
{
    std::int64_t integer = 0;
    double number = 0;
    bool boolean = false;
    bool null = false;
    std::string string;
    float vector[4] = {};
    ObjectHandle object;
    TypeId objectType = InvalidType;
    void* pointer = nullptr;
    bool resolved = false;
    Scalar owned;
};

typedef void (*ArgumentReader)(const NeoScript::CallContext&, std::size_t, HostArgument&);
struct ArgumentPlan
{
    ArgumentReader read = nullptr;
    TypeId objectType = InvalidType;
    bool nullable = false;
    bool owned = false;
    TypeId type = InvalidType;
};

static std::int64_t HandleBits(ObjectHandle object)
{
    std::int64_t result;
    static_assert(sizeof(result) == sizeof(object.value), "object IDs require 64-bit storage");
    std::memcpy(&result, &object.value, sizeof(result));
    return result;
}
static ObjectHandle ReadHandle(std::int64_t bits)
{
    ObjectHandle result;
    std::memcpy(&result.value, &bits, sizeof(bits));
    return result;
}
static void ReadInteger(const NeoScript::CallContext& ctx, std::size_t i, HostArgument& a) { a.integer = NeoCoreBackend::ReadInt(ctx, i); }
static void ReadNumber(const NeoScript::CallContext& ctx, std::size_t i, HostArgument& a) { a.number = NeoCoreBackend::ReadFloat(ctx, i); }
static void ReadBoolean(const NeoScript::CallContext& ctx, std::size_t i, HostArgument& a) { a.boolean = NeoCoreBackend::ReadBool(ctx, i); }
static void ReadText(const NeoScript::CallContext& ctx, std::size_t i, HostArgument& a) { a.string = NeoCoreBackend::ReadString(ctx, i); }
static void ReadVector(const NeoScript::CallContext& ctx, std::size_t i, HostArgument& a) { NeoCoreBackend::ReadVector(ctx, i, a.vector); }
static void ReadObject(const NeoScript::CallContext& ctx, std::size_t i, HostArgument& a) { a.object = ReadHandle(NeoCoreBackend::ReadInt(ctx, i)); }

enum class HostResultKind { Void, Int, Float, Bool, String, Vector2, Vector3, Vector4, Quaternion, Object, Null, Owned };
struct HostResult
{
    HostResultKind kind = HostResultKind::Void;
    HostArgument value;
};
}

struct HostRuntimeAccess
{
    struct State;
    struct Entry
    {
        State* state = nullptr;
        const HostBinding* binding = nullptr;
        std::size_t global = 0;
        std::vector<ArgumentPlan> arguments;
        TypeKind returnKind = TypeKind::Void;
        bool nullableReturn = false;
    };
    struct State
    {
        explicit State(const HostBindings& value) : bindings(value) {}
        HostBindings bindings;
        std::vector<Entry> entries;
    };
    struct InstanceState
    {
        InstanceState(State& shared, Instance& instance)
            : state(&shared), owner(&instance), globals(shared.bindings.globals_) {}
        State* state;
        Instance* owner;
        std::vector<ObjectHandle> globals;
    };
    static bool Dispatch(NeoScript::CallContext& ctx, NeoScript::StringView);
    static bool Link(State& state, NeoScript::IRuntime& runtime, const std::string& prefix, std::string& error);
    static bool WriteResult(NeoScript::CallContext& ctx, Instance& instance, const Entry& entry, const HostResult& result, std::string& error);
    static void* Resolve(State& state, TypeId type, ObjectHandle object)
    {
        return object ? state.bindings.resolver_(state.bindings.userData_, type, object) : nullptr;
    }
    static void* UserData(State& state) { return state.bindings.userData_; }
};

struct HostCall::Impl
{
    HostRuntimeAccess::Entry* entry = nullptr;
    Instance* instance = nullptr;
    ObjectHandle receiver;
    void* receiverPointer = nullptr;
    bool receiverResolved = false;
    std::vector<HostArgument> arguments;
    HostResult result;
    std::string error;

    void Fail(const std::string& message) { if (error.empty()) error = message.empty() ? "host call failed" : message; }
    void* ResolveReceiver()
    {
        receiverPointer = HostRuntimeAccess::Resolve(*entry->state, entry->binding->ownerType, receiver);
        receiverResolved = true;
        if (!receiverPointer) Fail("host object is no longer valid for " + entry->binding->name);
        return receiverPointer;
    }
    void* ResolveObject(std::size_t index)
    {
        HostArgument& argument = arguments.at(index);
        assert(argument.objectType != InvalidType);
        if (argument.null) { argument.pointer = nullptr; argument.resolved = true; return nullptr; }
        argument.pointer = HostRuntimeAccess::Resolve(*entry->state, argument.objectType, argument.object);
        argument.resolved = true;
        if (!argument.pointer) Fail("host object argument " + std::to_string(index + 1) + " is no longer valid for " + entry->binding->name);
        return argument.pointer;
    }
    void InvalidatePointers()
    {
        receiverPointer = nullptr;
        receiverResolved = false;
        for (auto& argument : arguments) { argument.pointer = nullptr; argument.resolved = false; }
    }
};

HostCall::HostCall(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
HostCall::~HostCall() = default;
void* HostCall::UserData() const { return HostRuntimeAccess::UserData(*impl_->entry->state); }
Instance& HostCall::CallerInstance() const { return *impl_->instance; }
void* HostCall::InstanceUserData() const { return impl_->instance->GetUserData(); }
ObjectHandle HostCall::ReceiverId() const { return impl_->receiver; }
void* HostCall::Receiver() { return impl_->receiverResolved ? impl_->receiverPointer : impl_->ResolveReceiver(); }
void* HostCall::ResolveReceiver() { return impl_->ResolveReceiver(); }
std::size_t HostCall::ArgumentCount() const { return impl_->arguments.size(); }
bool HostCall::IsNull(std::size_t i) const { return impl_->arguments.at(i).null; }
Scalar HostCall::Argument(std::size_t i) const
{
    const HostArgument& argument = impl_->arguments.at(i);
    if (argument.null) return Scalar::Null();
    const TypeRegistry& types = impl_->entry->state->bindings.Schema().Types();
    const TypeId type = impl_->entry->binding->parameters.at(i);
    switch (types.Get(type).kind)
    {
    case TypeKind::Int: return Scalar::Int(argument.integer);
    case TypeKind::Float: return Scalar::Float(argument.number);
    case TypeKind::Bool: return Scalar::Bool(argument.boolean);
    case TypeKind::String: return Scalar::String(argument.string);
    case TypeKind::Vector2: return Scalar::Vector(Scalar::Kind::Vector2,argument.vector);
    case TypeKind::Vector3: return Scalar::Vector(Scalar::Kind::Vector3,argument.vector);
    case TypeKind::Vector4: return Scalar::Vector(Scalar::Kind::Vector4,argument.vector);
    case TypeKind::Quaternion: return Scalar::Vector(Scalar::Kind::Quaternion,argument.vector);
    case TypeKind::Native: return Scalar::Object(types.Get(type).name,argument.object.value);
    default: return argument.owned;
    }
}
std::int64_t HostCall::Int(std::size_t i) const { return impl_->arguments.at(i).integer; }
double HostCall::Float(std::size_t i) const { return impl_->arguments.at(i).number; }
bool HostCall::Bool(std::size_t i) const { return impl_->arguments.at(i).boolean; }
const std::string& HostCall::String(std::size_t i) const { return impl_->arguments.at(i).string; }
Vector2Value HostCall::Vector2(std::size_t i) const
{
    const float* v = impl_->arguments.at(i).vector;
    return Vector2Value(v[0], v[1]);
}
Vector3Value HostCall::Vector3(std::size_t i) const
{
    const float* v = impl_->arguments.at(i).vector;
    return Vector3Value(v[0], v[1], v[2]);
}
Vector4Value HostCall::Vector4(std::size_t i) const
{
    const float* v = impl_->arguments.at(i).vector;
    return Vector4Value(v[0], v[1], v[2], v[3]);
}
QuaternionValue HostCall::Quaternion(std::size_t i) const
{
    const float* v = impl_->arguments.at(i).vector;
    return QuaternionValue(v[0], v[1], v[2], v[3]);
}
ObjectHandle HostCall::ObjectId(std::size_t i) const { return impl_->arguments.at(i).object; }
void* HostCall::Object(std::size_t i) { return impl_->arguments.at(i).resolved ? impl_->arguments.at(i).pointer : impl_->ResolveObject(i); }
void* HostCall::ResolveObject(std::size_t i) { return impl_->ResolveObject(i); }
void HostCall::ReturnInt(std::int64_t value) { impl_->result.kind = HostResultKind::Int; impl_->result.value.integer = value; }
void HostCall::ReturnFloat(double value) { impl_->result.kind = HostResultKind::Float; impl_->result.value.number = value; }
void HostCall::ReturnBool(bool value) { impl_->result.kind = HostResultKind::Bool; impl_->result.value.boolean = value; }
void HostCall::ReturnString(const std::string& value) { impl_->result.kind = HostResultKind::String; impl_->result.value.string = value; }
void HostCall::ReturnVector2(Vector2Value value)
{
    impl_->result.kind = HostResultKind::Vector2;
    float* v = impl_->result.value.vector;
    v[0] = value.x; v[1] = value.y;
}
void HostCall::ReturnVector3(Vector3Value value)
{
    impl_->result.kind = HostResultKind::Vector3;
    float* v = impl_->result.value.vector;
    v[0] = value.x; v[1] = value.y; v[2] = value.z;
}
void HostCall::ReturnVector4(Vector4Value value)
{
    impl_->result.kind = HostResultKind::Vector4;
    float* v = impl_->result.value.vector;
    v[0] = value.x; v[1] = value.y; v[2] = value.z; v[3] = value.w;
}
void HostCall::ReturnQuaternion(QuaternionValue value)
{
    impl_->result.kind = HostResultKind::Quaternion;
    float* v = impl_->result.value.vector;
    v[0] = value.w; v[1] = value.x; v[2] = value.y; v[3] = value.z;
}
void HostCall::ReturnObject(ObjectHandle value) { impl_->result.kind = value ? HostResultKind::Object : HostResultKind::Null; impl_->result.value.object = value; }
void HostCall::ReturnNull() { impl_->result.kind = HostResultKind::Null; }
void HostCall::ReturnValue(const Scalar& value) { impl_->result.kind = HostResultKind::Owned; impl_->result.value.owned = value; }
bool HostCall::ReturnJson(const std::string& text, bool strictIntegers)
{
    if (!impl_->error.empty()) return false;
    std::string error;
    Scalar value = BoundaryAccess::ParseJson(*impl_->instance, impl_->entry->binding->returnType,
        text, strictIntegers, error);
    if (!error.empty()) { impl_->Fail(error); return false; }
    ReturnValue(value);
    return true;
}
void HostCall::Fail(const std::string& message) { impl_->Fail(message); }
Result HostCall::CallScript(const std::string& name, const std::vector<Scalar>& arguments)
{
    // 호스트가 빌린 포인터는 중첩 실행을 넘겨 보관하지 않는다. 새 접근은 다시 ID를 조회한다.
    impl_->InvalidatePointers();
    Result result = impl_->instance->Call(name, arguments);
    impl_->InvalidatePointers();
    if (!result.ok()) impl_->Fail(result.error.message);
    return result;
}
Result HostCall::CallScript(const Scalar& function, const std::vector<Scalar>& arguments)
{
    impl_->InvalidatePointers();
    Result result = impl_->instance->Call(function, arguments);
    impl_->InvalidatePointers();
    if (!result.ok()) impl_->Fail(result.error.message);
    return result;
}

bool HostRuntimeAccess::WriteResult(NeoScript::CallContext& ctx, Instance& instance, const Entry& entry, const HostResult& result, std::string& error)
{
    if (result.kind == HostResultKind::Owned)
        return BoundaryAccess::Return(instance,ctx,entry.binding->returnType,result.value.owned,error);
    if (result.kind == HostResultKind::Null && entry.nullableReturn) { ctx.retNull(); return true; }
    switch (entry.returnKind)
    {
    case TypeKind::Void:
        if (result.kind == HostResultKind::Void) { ctx.retNull(); return true; }
        break;
    case TypeKind::Int:
        if (result.kind == HostResultKind::Int) { ctx.retInt(result.value.integer); return true; }
        break;
    case TypeKind::Float:
        if (result.kind == HostResultKind::Float) { ctx.retFloat(result.value.number); return true; }
        break;
    case TypeKind::Bool:
        if (result.kind == HostResultKind::Bool) { ctx.retBool(result.value.boolean); return true; }
        break;
    case TypeKind::String:
        if (result.kind == HostResultKind::String) { ctx.retString(result.value.string); return true; }
        break;
    case TypeKind::Vector2:
        if (result.kind == HostResultKind::Vector2) { ctx.retVec2(result.value.vector[0], result.value.vector[1]); return true; }
        break;
    case TypeKind::Vector3:
        if (result.kind == HostResultKind::Vector3) { ctx.retVec3(result.value.vector[0], result.value.vector[1], result.value.vector[2]); return true; }
        break;
    case TypeKind::Vector4:
        if (result.kind == HostResultKind::Vector4) { ctx.retVec4(result.value.vector[0], result.value.vector[1], result.value.vector[2], result.value.vector[3]); return true; }
        break;
    case TypeKind::Quaternion:
        if (result.kind == HostResultKind::Quaternion)
        {
            if (ctx.retQuat(result.value.vector)) return true;
            error = "could not write quaternion host result for " + entry.binding->name;
            return false;
        }
        break;
    case TypeKind::Native:
        if (result.kind == HostResultKind::Object && result.value.object) { ctx.retInt(HandleBits(result.value.object)); return true; }
        break;
    default: break;
    }
    error = "host result disagrees with the declared signature of " + entry.binding->name;
    return false;
}

bool HostRuntimeAccess::Dispatch(NeoScript::CallContext& ctx, NeoScript::StringView)
{
    Entry& entry = *static_cast<Entry*>(ctx.userData());
    try
    {
        BackendInstanceContext* context = static_cast<BackendInstanceContext*>(ctx.instanceUserData());
        InstanceState* instance = context ? static_cast<InstanceState*>(context->host) : nullptr;
        if (!instance || instance->state != entry.state)
        {
            ctx.fail(0, "invalid NeoCore instance binding context");
            return false;
        }
        if (!entry.binding)
        {
            const ObjectHandle object = instance->globals[entry.global];
            if (object) ctx.retInt(HandleBits(object)); else ctx.retNull();
            return true;
        }
        // 이 비공개 래퍼의 인자 수는 프런트엔드가 확정한다. 외부 호스트 진입의 검사는 별도다.
        assert(ctx.argCount() == entry.arguments.size() + 1);
        // 호스트 객체 값은 null 일 수 있다. null 수신자의 슬롯은 ID 로 읽을 수 없으므로 먼저 거부한다.
        if (NeoCoreBackend::IsNull(ctx, 0))
        {
            ctx.fail(0, "cannot access '" + entry.binding->name + "' on a null host object");
            return false;
        }
        std::unique_ptr<HostCall::Impl> impl(new HostCall::Impl());
        impl->entry = &entry;
        impl->instance = instance->owner;
        impl->receiver = ReadHandle(NeoCoreBackend::ReadInt(ctx, 0));
        impl->arguments.resize(entry.arguments.size());
        for (std::size_t i = 0; i < entry.arguments.size(); ++i)
        {
            const ArgumentPlan& plan = entry.arguments[i];
            HostArgument& argument = impl->arguments[i];
            argument.objectType = plan.objectType;
            argument.null = plan.nullable && NeoCoreBackend::IsNull(ctx, i + 1);
            if (!argument.null)
            {
                if (plan.owned) argument.owned = BoundaryAccess::Capture(*instance->owner,ctx,i+1,plan.type);
                else plan.read(ctx, i + 1, argument);
            }
        }
        // 인자 전체를 먼저 스냅샷한다. 이후 ID 조회와 콜백 사이에는 VM 슬롯을 빌려 읽지 않는다.
        impl->ResolveReceiver();
        for (std::size_t i = 0; i < impl->arguments.size() && impl->error.empty(); ++i)
            if (impl->arguments[i].objectType != InvalidType) impl->ResolveObject(i);
        HostCall call(std::move(impl));
        bool success = call.impl_->error.empty();
        if (success) success = entry.state->bindings.functions_[entry.binding->id - 1](call);
        if (!success && call.impl_->error.empty()) call.impl_->Fail("host call failed: " + entry.binding->name);
        if (call.impl_->error.empty()) WriteResult(ctx, *instance->owner, entry, call.impl_->result, call.impl_->error);
        if (!call.impl_->error.empty()) { ctx.fail(0, call.impl_->error); return false; }
        return true;
    }
    catch (const std::exception& error) { ctx.fail(0, std::string("host exception: ") + error.what()); }
    catch (...) { ctx.fail(0, "host exception"); }
    return false;
}

bool HostRuntimeAccess::Link(State& state, NeoScript::IRuntime& runtime, const std::string& prefix, std::string& error)
{
    if (!state.bindings.Validate(error)) return false;
    const HostSchema& schema = state.bindings.schema_;
    const TypeRegistry& types = schema.Types();
    const std::size_t count = schema.Bindings().size();
    state.entries.resize(count + schema.Globals().size());
    for (std::size_t i = 0; i < state.entries.size(); ++i)
    {
        Entry& entry = state.entries[i];
        entry.state = &state;
        std::string name = prefix;
        if (i < count)
        {
            entry.binding = &schema.Bindings()[i];
            name += "b" + std::to_string(entry.binding->id);
            TypeId result = entry.binding->returnType;
            entry.nullableReturn = types.AllowsNull(result);
            entry.returnKind = types.Get(result).kind;
            for (TypeId parameter : entry.binding->parameters)
            {
                ArgumentPlan plan;
                plan.type = parameter;
                // null 을 담을 수 있는 타입의 인자는 null 을 그대로 호스트에 넘긴다. 호스트가 판단한다.
                plan.nullable = types.AllowsNull(parameter);
                switch (types.Get(parameter).kind)
                {
                case TypeKind::Int: plan.read = &ReadInteger; break;
                case TypeKind::Float: plan.read = &ReadNumber; break;
                case TypeKind::Bool: plan.read = &ReadBoolean; break;
                case TypeKind::String: plan.read = &ReadText; break;
                case TypeKind::Vector2:
                case TypeKind::Vector3:
                case TypeKind::Vector4:
                case TypeKind::Quaternion: plan.read = &ReadVector; break;
                case TypeKind::Array:
                case TypeKind::Array32:
                case TypeKind::List:
                case TypeKind::Map:
                case TypeKind::Set:
                case TypeKind::Function:
                case TypeKind::Struct: plan.owned = true; break;
                case TypeKind::Native: plan.read = &ReadObject; plan.objectType = parameter; break;
                default: error = "unsupported host parameter type"; return false;
                }
                entry.arguments.push_back(plan);
            }
        }
        else
        {
            entry.global = i - count;
            name += "g" + std::to_string(entry.global);
        }
        NeoScript::NativeObjectDesc desc;
        desc.name = name;
        desc.method = &Dispatch;
        desc.userData = &entry;
        if (!runtime.RegisterObject(desc)) { error = "could not link host binding: " + name; return false; }
    }
    return true;
}

struct HostRuntime::Impl
{
    explicit Impl(const HostBindings& bindings) : state(bindings) {}
    HostRuntimeAccess::State state;
};
HostRuntime::HostRuntime(const HostBindings& bindings) : impl_(new Impl(bindings)) {}
HostRuntime::~HostRuntime() = default;
const HostSchema& HostRuntime::Schema() const { return impl_->state.bindings.Schema(); }
bool HostRuntime::Link(NeoScript::IRuntime& runtime, const std::string& prefix, std::string& error)
{
    return HostRuntimeAccess::Link(impl_->state, runtime, prefix, error);
}
struct HostInstance::Impl
{
    Impl(HostRuntimeAccess::State& shared, Instance& instance) : state(shared, instance) {}
    HostRuntimeAccess::InstanceState state;
};
HostInstance::HostInstance(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
HostInstance::~HostInstance() = default;
void* HostInstance::UserData() { return &impl_->state; }
std::unique_ptr<HostInstance> HostRuntime::CreateInstance(Instance& instance)
{
    std::unique_ptr<HostInstance::Impl> context(new HostInstance::Impl(impl_->state, instance));
    return std::unique_ptr<HostInstance>(new HostInstance(std::move(context)));
}
bool HostInstance::SetGlobal(const std::string& name, ObjectHandle object, std::string& error)
{
    error.clear();
    const HostSchema& schema = impl_->state.state->bindings.Schema();
    const auto& globals = schema.Globals();
    for (std::size_t i = 0; i < globals.size(); ++i)
    {
        if (globals[i].name != name) continue;
        impl_->state.globals[i] = object;
        return true;
    }
    error = "unknown host global: " + name;
    return false;
}

}
