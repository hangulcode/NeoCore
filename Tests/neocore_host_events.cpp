#include "NeoCore.h"
#include "NeoCoreCompiler.h"
#include "NeoCoreHost.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace NeoCore;
int checks = 0;

void Check(bool condition, const std::string& message)
{
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void Completed(const Result& result, const char* message)
{
    Check(result.ok() && result.status == ExecutionStatus::Completed,
        std::string(message) + ": " + result.error.message);
}

void Integer(const Result& result, std::int64_t expected, const char* message)
{
    Completed(result, message);
    Check(result.value.kind() == Scalar::Kind::Int && result.value.asInt() == expected, message);
}

void SchemaContracts()
{
    HostSchema schema;
    TypeRegistry& types = schema.Types();
    const TypeId entity = schema.AddObject("Entity");
    const TypeId update = types.FunctionOf(types.Void(), {types.Float()});
    const TypeId query = types.FunctionOf(types.Int(), {types.Int()});
    const HostBindingId first = schema.AddEvent(entity, "OnUpdate", update);
    const HostBindingId second = schema.AddEvent(entity, "OnQuery", query);
    Check(first == 1 && second == 2, "events have distinct normal binding IDs");
    const HostBinding* binding = schema.FindEvent(entity, "AddEvent", "OnUpdate");
    Check(binding && binding->id == first && binding->kind == HostBindingKind::Method &&
        binding->eventName == "OnUpdate" && binding->returnType == types.Void() &&
        binding->parameters == std::vector<TypeId>({types.String(), update}),
        "event contract stores a typed callback and original string argument");
    Check(!schema.FindEvent(entity, "Subscribe", "OnUpdate") &&
        !schema.FindEvent(entity, "AddEvent", "Missing"), "event lookup uses receiver, method and name");
    Check(schema.AddEvent(entity, "OnUpdate", update) == InvalidHostBinding &&
        schema.Bindings().size() == 2, "duplicate event fails without adding a binding");
    Check(schema.AddEvent(entity, "Bad", types.Int()) == InvalidHostBinding &&
        schema.AddEvent(entity, "Bad", types.NullableOf(update)) == InvalidHostBinding &&
        schema.AddEvent(entity, "Bad", InvalidType) == InvalidHostBinding &&
        schema.AddEvent(entity, "", update) == InvalidHostBinding &&
        schema.AddEvent(entity, std::string("bad\0name", 8), update) == InvalidHostBinding &&
        schema.AddEvent(types.Int(), "Bad", update) == InvalidHostBinding &&
        schema.AddEvent(entity, "Bad", update, "int") == InvalidHostBinding &&
        schema.Bindings().size() == 2, "invalid event contracts fail atomically");
    Check(schema.AddMethod(entity, "AddEvent", types.Void(), {}) == InvalidHostBinding &&
        !schema.AddProperty(entity, "AddEvent", types.Int()), "events reserve their registration member name");
    Check(schema.AddMethod(entity, "Ordinary", types.Void(), {}) != InvalidHostBinding &&
        schema.AddEvent(entity, "OnUpdate", update, "Ordinary") == InvalidHostBinding,
        "event registration cannot overlap an ordinary method");
    Check(schema.AddProperty(entity, "Property", types.Int()) &&
        schema.AddEvent(entity, "OnUpdate", update, "Property") == InvalidHostBinding,
        "event registration cannot overlap a property");
    Check(schema.AddEvent(entity, "OnUpdate", query, "Subscribe") != InvalidHostBinding,
        "custom registration methods have independent event namespaces");
    const TypeId other = schema.AddObject("OtherEntity");
    Check(schema.AddEvent(other, "OnUpdate", query) != InvalidHostBinding,
        "different receiver types may assign different callback signatures");
    std::string error;
    Check(schema.Validate(error), "event schema validates: " + error);
    HostSchema copy = schema;
    Check(copy.Validate(error) && copy.FindEvent(entity, "AddEvent", "OnQuery")->id == second,
        "schema copies preserve event metadata");
}

struct Event
{
    std::uint64_t receiver = 0;
    std::uint64_t caller = 0;
    std::string name;
    Scalar callback;
};

struct Host
{
    HostSchema schema;
    TypeId entity = InvalidType;
    std::map<std::uint64_t, std::int64_t> objects;
    std::vector<Event> events;
    std::unique_ptr<HostBindings> bindings;
    std::unique_ptr<Runtime> runtime;

    explicit Host(const std::string& updateName = "OnUpdate", bool updateInteger = false)
    {
        TypeRegistry& types = schema.Types();
        entity = schema.AddObject("Entity");
        Check(schema.AddProperty(entity, "Value", types.Int()), "event host property");
        Check(schema.AddEvent(entity, updateName,
            types.FunctionOf(types.Void(), {updateInteger ? types.Int() : types.Float()})) != 0,
            "update event schema");
        Check(schema.AddEvent(entity, "OnDamage", types.FunctionOf(types.Void(), {types.Int()})) != 0,
            "damage event schema");
        Check(schema.AddEvent(entity, "OnQuery", types.FunctionOf(types.Int(), {types.Int()})) != 0,
            "query event schema");
        Check(schema.AddEvent(entity, "OnSleep", types.FunctionOf(types.Void(), {})) != 0,
            "suspended event schema");
        Check(schema.AddGlobal("GameObject", entity) && schema.AddGlobal("Target", entity), "event host globals");
        objects[100] = 0; objects[101] = 0; objects[200] = 0;
        bindings.reset(new HostBindings(schema, [](void* data, TypeId type, ObjectHandle id) -> void* {
            Host& host = *static_cast<Host*>(data);
            const auto found = host.objects.find(id.value);
            return type == host.entity && found != host.objects.end() ? &found->second : nullptr;
        }, this));
        Check(bindings->BindGlobal("GameObject", ObjectHandle(100)) &&
            bindings->BindGlobal("Target", ObjectHandle(200)), "bind default event host IDs");
        for (const HostBinding& binding : schema.Bindings())
        {
            if (binding.kind == HostBindingKind::Getter)
                Check(bindings->Bind(binding.id, [](HostCall& call) {
                    call.ReturnInt(*static_cast<std::int64_t*>(call.Receiver())); return true;
                }), "bind event host getter");
            else if (binding.kind == HostBindingKind::Setter)
                Check(bindings->Bind(binding.id, [](HostCall& call) {
                    *static_cast<std::int64_t*>(call.Receiver()) = call.Int(0); return true;
                }), "bind event host setter");
            else
            {
                const std::string eventName = binding.eventName;
                Check(bindings->Bind(binding.id, [this, eventName](HostCall& call) {
                    Check(call.ArgumentCount() == 2 && call.String(0) == eventName,
                        "compile-time event selection agrees with runtime literal value");
                    Event event;
                    event.receiver = call.ReceiverId().value;
                    event.caller = call.CallerInstance().Id();
                    event.name = eventName;
                    event.callback = call.Argument(1);
                    Check(event.callback.kind() == Scalar::Kind::Function, "registration owns a function value");
                    auto found = std::find_if(events.begin(), events.end(), [&event](const Event& existing) {
                        return existing.receiver == event.receiver && existing.caller == event.caller && existing.name == event.name;
                    });
                    if (found == events.end()) events.push_back(event);
                    else *found = event;
                    return true;
                }), "bind typed event registration");
            }
        }
        Error error;
        runtime = Runtime::Create(*bindings, error);
        Check(runtime && error.ok(), "create event runtime: " + error.message);
    }

    std::unique_ptr<Program> Compile(const std::string& source, bool debug)
    {
        Error error;
        auto program = runtime->Compile(source, error, "host_events.nc", debug);
        Check(program && error.ok(), "compile event program: " + error.message);
        return program;
    }

    Event Find(std::uint64_t caller, const std::string& name) const
    {
        for (const Event& event : events)
            if (event.caller == caller && event.name == name) return event;
        throw std::runtime_error("missing event registration: " + name);
    }

    Result Dispatch(const Event& event, const std::vector<Scalar>& args = {})
    {
        Instance* instance = runtime->FindInstance(event.caller);
        if (!instance || objects.find(event.receiver) == objects.end())
        {
            Result result;
            result.status = ExecutionStatus::Failed;
            result.error.message = "event receiver or calling instance no longer exists";
            return result;
        }
        return instance->Call(event.callback, args);
    }

    void Cancel(std::uint64_t caller)
    {
        events.erase(std::remove_if(events.begin(), events.end(), [caller](const Event& event) {
            return event.caller == caller;
        }), events.end());
    }
};

void FrontendContracts()
{
    Host host;
    const char* accepted[] = {
        "void Update(float dt){} GameObject.AddEvent(\"OnUpdate\",Update);",
        "void Update(float dt){} GameObject.AddEvent((\"OnUpdate\"),Update);",
        "void Update(float dt){} GameObject.AddEvent(\"\\u004fnUpdate\",Update);",
        "GameObject.AddEvent(\"OnUpdate\",fn(float dt)->void{});",
        "void Damage(int n){} int Query(int n){return n;} GameObject.AddEvent(\"OnDamage\",Damage); GameObject.AddEvent(\"OnQuery\",Query);",
        "void Update(float dt){} fn(float)->void callback=Update; GameObject.AddEvent(\"OnUpdate\",callback);"
    };
    const char* rejected[] = {
        "void Update(float dt){} GameObject.AddEvent(\"Missing\",Update);",
        "void Update(float dt){} string eventName=\"OnUpdate\"; GameObject.AddEvent(eventName,Update);",
        "void Update(float dt){} const string eventName=\"OnUpdate\"; GameObject.AddEvent(eventName,Update);",
        "void Update(float dt){} GameObject.AddEvent(\"On\"..\"Update\",Update);",
        "void Update(float dt){} GameObject.AddEvent(1,Update);",
        "void Update(int dt){} GameObject.AddEvent(\"OnUpdate\",Update);",
        "int Update(float dt){return 1;} GameObject.AddEvent(\"OnUpdate\",Update);",
        "void Query(int n){} GameObject.AddEvent(\"OnQuery\",Query);",
        "void Update(float dt,float extra){} GameObject.AddEvent(\"OnUpdate\",Update);",
        "GameObject.AddEvent(\"OnUpdate\",null);",
        "GameObject.AddEvent(\"OnUpdate\",1);",
        "GameObject.AddEvent(\"OnUpdate\");",
        "void Update(float dt){} GameObject.AddEvent(\"OnUpdate\",Update,1);",
        "void Update(float dt){} GameObject.AddEvent(\"On\\u00\",Update);",
        "void Update(float dt){} GameObject.AddEvent(\"On\\u00xz\",Update);",
        "void Update(float dt){} GameObject.AddEvent(\"On\\uD800\",Update);",
        "void Update(float dt){} GameObject.AddEvent(\"On\\uD800\\u0041\",Update);",
        "void Update(float dt){} GameObject.AddEvent(\"On\\uDC00\",Update);",
        "void Update(float dt){} GameObject.AddEvent(\"On\\u0000\",Update);"
    };
    for (const char* source : accepted)
    {
        StaticCompileResult result;
        Check(CompileStaticSource(source, result, &host.schema),
            std::string("accept event contract: ") + source + " : " + result.errorMessage);
    }
    for (const char* source : rejected)
    {
        StaticCompileResult result;
        Check(!CompileStaticSource(source, result, &host.schema) && !result.errorMessage.empty(),
            std::string("reject event contract: ") + source);
    }
}

void LiteralContracts(bool debug)
{
    struct Literal { std::string source; std::string decoded; };
    const std::vector<Literal> cases = {
        { R"("On\n\r\t\"\'\\")", "On\n\r\t\"'\\" },
        { R"('On\'Update')", "On'Update" },
        { R"("On\u00e9")", "On\xc3\xa9" },
        { R"("On\uac00")", "On\xea\xb0\x80" },
        { R"("On\uD83D\uDE80")", "On\xf0\x9f\x9a\x80" },
        { "\"On\xea\xb0\x80\xf0\x9f\x9a\x80\"", "On\xea\xb0\x80\xf0\x9f\x9a\x80" },
        { R"("On\q\0")", R"(On\q\0)" }
    };
    for (const Literal& literal : cases)
    {
        Host host(literal.decoded);
        auto program = host.Compile("void Callback(float dt){} GameObject.AddEvent(" + literal.source + ",Callback);", debug);
        Error error;
        auto instance = program->CreateInstance(error); Check(instance && error.ok(), "literal event instance");
        Completed(instance->Run(), "escaped event literal registers exact decoded name");
        Check(host.events.size() == 1 && host.events[0].name == literal.decoded, "decoded event name identity");
        Completed(host.Dispatch(host.events[0], {Scalar::Float(1.0)}), "escaped event dispatch");
    }
}

const char* Source()
{
    return "int total=0;\n"
        "void Update(float dt){total+=toint(dt);}\n"
        "void Damage(int amount){total+=amount;GameObject.Value+=amount;}\n"
        "int Query(int value){return total+value;}\n"
        "void Delayed(){sleep(0);total+=100;}\n"
        "export void InstallClosure(){int captured=7;Target.AddEvent(\"OnDamage\",fn(int amount)->void{total+=captured+amount;});captured=99;}\n"
        "export int Total(){return total;}\n"
        "GameObject.AddEvent(\"\\u004fnUpdate\",Update);\n"
        "Target.AddEvent(\"OnDamage\",Damage);\n"
        "Target.AddEvent(\"OnQuery\",Query);\n"
        "Target.AddEvent(\"OnSleep\",Delayed);\n";
}

void RuntimeContracts(bool debug)
{
    Host host;
    auto program = host.Compile(Source(), debug);
    Error error;
    auto first = program->CreateInstance(error); Check(first && error.ok(), "create first event instance");
    auto second = program->CreateInstance(error); Check(second && error.ok(), "create second event instance");
    Check(second->SetHostObject("GameObject", ObjectHandle(101), error), "bind second event instance object");
    Completed(first->Run(), "register first instance events");
    Completed(second->Run(), "register second instance events");
    Check(host.events.size() == 8, "two instances keep separate event registrations");
    Integer(first->Call("Total"), 0, "registering does not invoke callback during initialization");
    const std::uint64_t firstId = first->Id(), secondId = second->Id();
    const Event update = host.Find(firstId, "OnUpdate");
    const Event damage = host.Find(firstId, "OnDamage");
    Check(update.receiver == 100 && damage.receiver == 200 && damage.caller == firstId,
        "receiver object and calling instance have separate identities");
    program->FindFunction("Update", error);
    Check(!error.ok(), "private callback is not exposed through exported name lookup");
    Completed(host.Dispatch(update, {Scalar::Float(2.0)}), "invoke private named update callback");
    Completed(host.Dispatch(damage, {Scalar::Int(3)}), "invoke callback registered on another object");
    Integer(first->Call("Total"), 5, "event dispatch targets the original calling instance");
    Integer(second->Call("Total"), 0, "event dispatch leaves sibling globals unchanged");
    Check(host.objects[100] == 3 && host.objects[200] == 0,
        "cross-object callback retains caller host-global bindings");
    Integer(host.Dispatch(host.Find(firstId, "OnQuery"), {Scalar::Int(4)}), 9,
        "event callback returns its declared typed result");
    Completed(first->Call("InstallClosure"), "register capturing callback");
    Event closure = host.Find(firstId, "OnDamage");
    Completed(host.Dispatch(closure, {Scalar::Int(2)}), "invoke owned closure after registering frame returns");
    Integer(first->Call("Total"), 14, "closure stores its captured value");
    Check(!second->Call(closure.callback, {Scalar::Int(1)}).ok(), "closure cannot execute in another instance");
    Result sleeping = host.Dispatch(host.Find(firstId, "OnSleep"));
    Check(sleeping.ok() && sleeping.suspended() && first->IsSuspended(), "event callback can suspend");
    Check(!host.Dispatch(update, {Scalar::Float(1.0)}).ok() && first->IsSuspended(),
        "new events cannot replace an instance's pending callback");
    Completed(first->Resume(), "resume original event callback");
    Integer(first->Call("Total"), 114, "resumed event executes exactly once");
    host.Cancel(firstId);
    Check(host.events.size() == 4 && first->Reset(error) && first->Id() == firstId,
        "host clears registrations before reset despite stable instance ID");
    Check(!first->Call(closure.callback, {Scalar::Int(1)}).ok(), "reset invalidates stored closure generation");
    Completed(first->Run(), "reset instance registers fresh callbacks");
    Check(host.events.size() == 8, "reset refreshes rather than duplicates registrations");
    Integer(first->Call("Total"), 0, "reset events see reset globals");
    const Event dead = host.Find(secondId, "OnUpdate");
    host.Cancel(secondId); second.reset();
    Check(!host.runtime->FindInstance(secondId) && !host.Dispatch(dead, {Scalar::Float(1.0)}).ok(),
        "stale caller ID cannot dispatch after instance destruction");
    program.reset();
    Completed(host.Dispatch(host.Find(firstId, "OnUpdate"), {Scalar::Float(3.0)}),
        "stored callback and instance retain program after wrapper destruction");
    Integer(first->Call("Total"), 3, "retained callback remains functional");
    host.objects.erase(200);
    Check(!host.Dispatch(host.Find(firstId, "OnDamage"), {Scalar::Int(1)}).ok(),
        "host validates receiver lifetime before dispatch");
    host.Cancel(firstId);
    Check(host.events.empty(), "host releases all event registrations");
}

void ImageContracts(bool debug)
{
    Host original;
    auto program = original.Compile(Source(), debug);
    Error error;
    std::vector<std::uint8_t> image;
    Check(program->SaveImage(image, error), "save image with event contract: " + error.message);
    Host matching;
    auto loaded = matching.runtime->LoadImage(image, error);
    Check(loaded && error.ok(), "load image against identical event contract: " + error.message);
    auto instance = loaded->CreateInstance(error); Check(instance && error.ok(), "create loaded event instance");
    Completed(instance->Run(), "loaded event registration");
    Completed(matching.Dispatch(matching.Find(instance->Id(), "OnUpdate"), {Scalar::Float(6.0)}),
        "loaded image resolves original typed callback");
    Integer(instance->Call("Total"), 6, "loaded event callback state");
    Host renamed("OnRenamed");
    Check(!renamed.runtime->LoadImage(image, error) && !error.ok(),
        "renamed event rejects image even with identical binding IDs and signatures");
    Host changedSignature("OnUpdate", true);
    Check(!changedSignature.runtime->LoadImage(image, error) && !error.ok(),
        "changed callback signature rejects image with unchanged event names");
}
}

int main()
{
    try
    {
        SchemaContracts();
        FrontendContracts();
        for (int mode = 0; mode < 2; ++mode)
        {
            LiteralContracts(mode != 0);
            RuntimeContracts(mode != 0);
            ImageContracts(mode != 0);
        }
        std::printf("NeoCore host events: %d checks passed\n", checks);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "NeoCore host events failed after %d checks: %s\n", checks, error.what());
        return 1;
    }
}
