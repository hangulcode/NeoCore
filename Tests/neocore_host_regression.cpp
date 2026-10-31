#include "NeoCoreHost.h"

#include <cstdio>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

namespace
{
int checks = 0;
int failures = 0;

void Check(bool condition, const std::string& label)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
    }
}

struct Object
{
    NeoCore::TypeId type = NeoCore::InvalidType;
    std::int64_t value = 10;
    double amount = 1.5;
    bool enabled = false;
    std::string text = "initial";
};

struct Fixture
{
    NeoCore::HostSchema schema;
    NeoCore::TypeId objectType = NeoCore::InvalidType;
    NeoCore::TypeId alienType = NeoCore::InvalidType;
    std::unique_ptr<NeoCore::HostBindings> bindings;
    std::map<std::uint64_t, Object> objects;
    std::string trace;
    int calls = 0;
    int resolutions = 0;
    bool nestedSucceeded = false;
    bool nullReceiverAfterReentry = false;
    bool nullObjectAfterReentry = false;
    bool retainedString = false;
    std::uint64_t observedId = 0;

    static std::uint64_t HighId() { return (std::numeric_limits<std::uint64_t>::max)() - 7; }

    // ID 조회는 빌린 포인터만 반환한다. 중첩 실행 뒤에는 다시 조회해야 한다.
    static void* Resolve(void* data, NeoCore::TypeId type, NeoCore::ObjectHandle id)
    {
        Fixture& fixture = *static_cast<Fixture*>(data);
        ++fixture.resolutions;
        const auto found = fixture.objects.find(id.value);
        return found != fixture.objects.end() && found->second.type == type ? &found->second : nullptr;
    }

    void AddObject(std::uint64_t id, NeoCore::TypeId type, std::int64_t value)
    {
        Object object;
        object.type = type;
        object.value = value;
        Check(objects.insert(std::make_pair(id, object)).second, "mock object IDs are never reused");
    }

    void Method(const char* name, NeoCore::TypeId result, const std::vector<NeoCore::TypeId>& parameters = {})
    {
        Check(schema.AddMethod(objectType, name, result, parameters) != NeoCore::InvalidHostBinding,
            std::string("register mock method ") + name + ": " + schema.LastError());
    }

    Fixture()
    {
        objectType = schema.AddObject("Object");
        alienType = schema.AddObject("AlienObject");
        NeoCore::TypeRegistry& types = schema.Types();
        const NeoCore::TypeId nullableObject = types.NullableOf(objectType);
        const NeoCore::TypeId nullableInt = types.NullableOf(types.Int());
        const NeoCore::TypeId nullableText = types.NullableOf(types.String());
        Check(schema.AddProperty(objectType, "Value", types.Int()), "register integer property");
        Check(schema.AddProperty(objectType, "Amount", types.Float()), "register float property");
        Check(schema.AddProperty(objectType, "Enabled", types.Bool()), "register bool property");
        Check(schema.AddProperty(objectType, "Text", types.String()), "register string property");
        Check(schema.AddProperty(objectType, "ReadOnly", types.Int(), true, false), "register readonly property");
        Check(schema.AddProperty(objectType, "WriteOnly", types.Int(), false, true), "register writeonly property");
        Check(schema.AddProperty(objectType, "Optional", nullableInt), "register nullable property");
        Check(schema.AddProperty(objectType, "Child", objectType, true, false), "register native property");
        Check(schema.AddProperty(objectType, "MaybeChild", nullableObject, true, false), "register nullable native property");
        Method("EchoInt", types.Int(), {types.Int()});
        Method("EchoFloat", types.Float(), {types.Float()});
        Method("EchoBool", types.Bool(), {types.Bool()});
        Method("EchoText", types.String(), {types.String()});
        Method("ZeroText", types.String());
        Method("Join", types.Int(), {objectType, objectType});
        Method("Identity", objectType, {objectType});
        Method("AcceptNullable", types.Bool(), {nullableObject});
        Method("NullableArgs", types.Bool(), {nullableInt, nullableText});
        Method("NullableInt", nullableInt);
        Method("NullableReturn", nullableObject);
        Method("Target", objectType);
        Method("Rhs", types.Int());
        Method("Mark", types.Int(), {types.Int()});
        Method("Reenter", types.Int());
        Method("ReenterObject", types.Int(), {objectType});
        Method("ReenterText", types.String(), {types.String()});
        Method("Destroy", types.Void());
        Method("DeleteOther", types.Void());
        Method("Touch", types.Void());
        Method("Fail", types.Int());
        Method("Bad", types.Int());
        Method("Void", types.Void());
        Check(schema.AddGlobal("Object", objectType), "native type and global can share a name");
        Check(schema.AddGlobal("Other", objectType), "register second host global");
        Check(schema.AddGlobal("Maybe", nullableObject), "register nullable host global");
        Check(schema.AddGlobal("Alien", alienType), "register different nominal host global");
        AddObject(1, objectType, 10);
        AddObject(2, objectType, 20);
        AddObject(3, alienType, 30);
        AddObject(HighId(), objectType, 70);
        bindings.reset(new NeoCore::HostBindings(schema, &Resolve, this));
        for (const NeoCore::HostBinding& binding : schema.Bindings())
        {
            const NeoCore::HostBinding copy = binding;
            Check(bindings->Bind(binding.id, [this, copy](NeoCore::HostCall& call) {
                ++calls;
                return Dispatch(copy, call);
            }), "link mock member " + binding.name);
        }
        Check(bindings->BindGlobal("Object", NeoCore::ObjectHandle(1)), "bind primary object");
        Check(bindings->BindGlobal("Other", NeoCore::ObjectHandle(2)), "bind second object");
        Check(bindings->BindGlobal("Maybe", NeoCore::ObjectHandle()), "bind nullable object to null");
        Check(bindings->BindGlobal("Alien", NeoCore::ObjectHandle(3)), "bind alien object");
        std::string error;
        Check(bindings->Validate(error), "complete mock binding: " + error);
    }

    void Override(const char* name, NeoCore::HostFunction function)
    {
        const NeoCore::HostBinding* method = schema.FindMethod(objectType, name);
        Check(method != nullptr, std::string("find override ") + name);
        if (method) Check(bindings->Bind(method->id, [this, function](NeoCore::HostCall& call) {
            ++calls;
            return function(call);
        }), std::string("override ") + name);
    }

    bool Dispatch(const NeoCore::HostBinding& binding, NeoCore::HostCall& call)
    {
        Check(call.UserData() == this, "callback userdata is the owning mock world");
        Check(call.ArgumentCount() == binding.parameters.size(), "callback argument count excludes receiver");
        Object* object = static_cast<Object*>(call.Receiver());
        if (!object) return false;
        const std::string& name = binding.name;
        if (binding.kind == NeoCore::HostBindingKind::Getter)
        {
            trace += "G";
            if (name == "Value" || name == "ReadOnly") call.ReturnInt(object->value);
            else if (name == "Amount") call.ReturnFloat(object->amount);
            else if (name == "Enabled") call.ReturnBool(object->enabled);
            else if (name == "Text") call.ReturnString(object->text);
            else if (name == "Child") call.ReturnObject(NeoCore::ObjectHandle(2));
            else call.ReturnNull();
            return true;
        }
        if (binding.kind == NeoCore::HostBindingKind::Setter)
        {
            trace += "S";
            if (name == "Value" || name == "WriteOnly") object->value = call.Int(0);
            else if (name == "Amount") object->amount = call.Float(0);
            else if (name == "Enabled") object->enabled = call.Bool(0);
            else if (name == "Text") object->text = call.String(0);
            else if (name == "Optional") object->value = call.IsNull(0) ? -99 : call.Int(0);
            return true;
        }
        if (name == "EchoInt") call.ReturnInt(call.Int(0));
        else if (name == "EchoFloat") call.ReturnFloat(call.Float(0));
        else if (name == "EchoBool") call.ReturnBool(call.Bool(0));
        else if (name == "EchoText") call.ReturnString(call.String(0));
        else if (name == "ZeroText") call.ReturnString(std::string("a\0b", 3));
        else if (name == "Join") call.ReturnInt(static_cast<Object*>(call.Object(0))->value + static_cast<Object*>(call.Object(1))->value);
        else if (name == "Identity") { observedId = call.ObjectId(0).value; call.ReturnObject(call.ObjectId(0)); }
        else if (name == "AcceptNullable") call.ReturnBool(call.IsNull(0) && !call.ObjectId(0) && call.Object(0) == nullptr);
        else if (name == "NullableArgs") call.ReturnBool(call.IsNull(0) && call.IsNull(1));
        else if (name == "NullableInt" || name == "NullableReturn") call.ReturnNull();
        else if (name == "Target") { trace += "T"; call.ReturnObject(call.ReceiverId()); }
        else if (name == "Rhs") { trace += "R"; call.ReturnInt(4); }
        else if (name == "Mark") { trace += std::to_string(call.Int(0)); call.ReturnInt(call.Int(0)); }
        else if (name == "Destroy") { trace += "D"; objects.erase(call.ReceiverId().value); }
        else if (name == "DeleteOther") { trace += "D"; objects.erase(2); }
        else if (name == "Touch") trace += "X";
        else if (name == "Void") {}
        else if (name == "Fail") { call.Fail("mock host failure"); return false; }
        else if (name == "Bad") call.ReturnInt(1);
        else { call.Fail("unconfigured reentry callback"); return false; }
        return true;
    }
};

std::unique_ptr<NeoCore::Program> Compile(Fixture& fixture, const char* source, const char* label, bool debug)
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, *fixture.bindings, error, label, debug);
    Check(program != nullptr, std::string(label) + (debug ? " debug" : " release") + " compile: " + error.message);
    return program;
}

NeoCore::Result Invoke(Fixture& fixture, const char* source, const char* label, bool debug)
{
    std::unique_ptr<NeoCore::Program> program = Compile(fixture, source, label, debug);
    if (program) return program->Call("Probe");
    NeoCore::Result result;
    result.error.message = "compilation failed";
    return result;
}

void ExpectInt(const NeoCore::Result& result, std::int64_t expected, const char* label)
{
    Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Int && result.value.asInt() == expected,
        std::string(label) + ": " + result.error.message);
}

void ExpectError(const NeoCore::Result& result, const char* label, const char* fragment = "")
{
    Check(!result.ok() && !result.error.message.empty() && result.error.message.find(fragment) != std::string::npos,
        std::string(label) + ": " + result.error.message);
}

void CheckScalarBindings(bool debug)
{
    Fixture fixture;
    ExpectInt(Invoke(fixture,
        "export int Probe(){Object.Value=Object.EchoInt(31);Object.Amount=Object.EchoFloat(2);"
        "Object.Enabled=Object.EchoBool(true);Object.Text=Object.EchoText(\"hello\");"
        "if(Object.Amount==2.0&&Object.Enabled&&Object.Text==\"hello\")return Object.Value;return -1;}",
        "scalar arguments and properties", debug), 31, "scalar values retain declared types");
    Check(fixture.objects[1].value == 31 && fixture.objects[1].amount == 2.0 && fixture.objects[1].enabled && fixture.objects[1].text == "hello",
        "property setters update only the resolved receiver");
    ExpectInt(Invoke(fixture,
        "export int Probe(){Object local=Other;return Object.Join(Object,local)+Object.Child.Value;}",
        "native identity and nested property", debug), 71, "native variables and chained getters");
    ExpectInt(Invoke(fixture,
        "export int Probe(){if(Maybe==null&&Object.MaybeChild==null&&Object.NullableReturn()==null"
        "&&Object.NullableInt()==null&&Object.AcceptNullable(null)&&Object.NullableArgs(null,null))return 1;return 0;}",
        "nullable host contracts", debug), 1, "nullable inputs and outputs preserve null");
    ExpectInt(Invoke(fixture,
        "export int Probe(){Object.Optional=null;Object.WriteOnly=9;return Object.ReadOnly;}",
        "writeonly and nullable setters", debug), 9, "writeonly setter and readonly getter");
    const NeoCore::Result text = Invoke(fixture,
        "export string Probe(){return Object.EchoText(Object.ZeroText());}", "embedded zero string", debug);
    Check(text.ok() && text.value.kind() == NeoCore::Scalar::Kind::String && text.value.asString() == std::string("a\0b", 3),
        "host string snapshots preserve embedded NUL bytes");
}

void CheckInternalNames(bool debug)
{
    NeoCore::HostSchema schema;
    const NeoCore::TypeId object = schema.AddObject("PrefixObject");
    Check(schema.AddGlobal("__neocore_host_g0", object), "register legal underscore host global");
    Check(schema.AddProperty(object, "Value", schema.Types().Int()), "register internal name test property");
    const NeoCore::HostBindingId add = schema.AddMethod(object, "Add", schema.Types().Int(), {schema.Types().Int()});
    Check(add != NeoCore::InvalidHostBinding, "register internal name test method");
    std::int64_t value = 7;
    NeoCore::HostBindings bindings(schema,
        [](void* data, NeoCore::TypeId, NeoCore::ObjectHandle id) -> void* { return id.value == 1 ? data : nullptr; }, &value);
    Check(bindings.BindGlobal("__neocore_host_g0", NeoCore::ObjectHandle(1)), "bind legal underscore host global");
    const NeoCore::HostProperty* property = schema.FindProperty(object, "Value");
    Check(property != nullptr, "find internal name test property");
    if (!property) return;
    Check(bindings.Bind(property->getter, [](NeoCore::HostCall& call) {
        call.ReturnInt(*static_cast<std::int64_t*>(call.Receiver())); return true;
    }), "bind internal name test getter");
    Check(bindings.Bind(property->setter, [](NeoCore::HostCall& call) {
        *static_cast<std::int64_t*>(call.Receiver()) = call.Int(0); return true;
    }), "bind internal name test setter");
    Check(bindings.Bind(add, [](NeoCore::HostCall& call) {
        call.ReturnInt(*static_cast<std::int64_t*>(call.Receiver()) + call.Int(0)); return true;
    }), "bind internal name test method");
    const char* source = "int __neocore_host_b1=3;\n"
        "int __neocore_hostwrite_0(int __neocore_1){return __neocore_1+2;}\n"
        "// $neocore_host_b1 는 참조가 아니라 주석 내용이다.\n"
        "export int Probe(){\n"
        "__neocore_host_g0.Value=__neocore_hostwrite_0(__neocore_host_b1);\n"
        "__neocore_host_g0.Value+=__neocore_host_g0.Add(4);\n"
        "return __neocore_host_g0.Value;}";
    NeoCore::Error error;
    auto program = NeoCore::Program::Compile(source, bindings, error, "internal_names.nc", debug);
    Check(program != nullptr, "underscore names coexist with generated host/property helpers: " + error.message);
    if (program)
    {
        ExpectInt(program->Call("Probe"), 14, "generated host methods and write helpers retain distinct identities");
        Check(value == 14, "internal names preserve host property writeback");
    }
    Fixture fixture;
    const NeoCore::Result text = Invoke(fixture,
        "export string Probe(){return Object.EchoText(\"$neocore_host_b1 $neocore_ir_0 $neocore_return_0\");}",
        "literal internal names", debug);
    Check(text.ok() && text.value.kind() == NeoCore::Scalar::Kind::String &&
        text.value.asString() == "$neocore_host_b1 $neocore_ir_0 $neocore_return_0",
        "internal-looking text passes through host calls unchanged");
}

void CheckPropertyOrder(bool debug)
{
    Fixture fixture;
    ExpectInt(Invoke(fixture,
        "export int Probe(){Object.Target().Value=Object.Rhs();return 1;}", "property assignment order", debug),
        1, "plain property assignment succeeds");
    Check(fixture.trace == "RTS" && fixture.objects[1].value == 4, "property assignment evaluates RHS before target once");
    fixture.trace.clear();
    ExpectInt(Invoke(fixture,
        "export int Probe(){Object.Target().Value+=Object.Rhs();return 1;}", "compound property order", debug),
        1, "compound property assignment succeeds");
    Check(fixture.trace == "RTGS" && fixture.objects[1].value == 8, "compound property evaluates RHS then target getter setter once");
    fixture.trace.clear();
    ExpectInt(Invoke(fixture,
        "export int Probe(){int before=Object.Target().Value++;int after=++Object.Target().Value;return before*100+after;}",
        "property increment results", debug), 810, "postfix returns old value and prefix returns stored value");
    Check(fixture.trace == "TGSTGS", "property increments resolve one target and read/write once");
    fixture.trace.clear();
    ExpectInt(Invoke(fixture,
        "export int Probe(){Object.Target().Value-=2;Object.Target().Value*=3;Object.Target().Value/=4;return Object.Value;}",
        "property arithmetic", debug), 6, "all scalar compound property operators");
    fixture.trace.clear();
    ExpectInt(Invoke(fixture,
        "export int Probe(){return Object.EchoInt(Object.Mark(1)+Object.Mark(2));}", "method argument order", debug),
        3, "method arguments preserve left to right evaluation");
    Check(fixture.trace == "12", "method argument side effects run in source order");
    const NeoCore::Result floating = Invoke(fixture,
        "export float Probe(){Object.Amount=2;Object.Amount+=3;Object.Amount*=2;return Object.Amount;}",
        "floating property compound widening", debug);
    Check(floating.ok() && floating.value.kind() == NeoCore::Scalar::Kind::Float && floating.value.asFloat() == 10.0,
        "floating compound properties widen integer RHS before typed arithmetic");
}

void CheckIntegerPropertyOperators(bool debug)
{
    struct Case { const char* operation; std::int64_t initial; std::int64_t expected; };
    const Case cases[] = {
        {"%=", 11, 3}, {"&=", 13, 4}, {"|=", 9, 13},
        {"^=", 13, 9}, {"<<=", 3, 48}, {">>=", -32, -2}
    };
    for (const Case& item : cases)
    {
        Fixture fixture;
        fixture.objects[1].value = item.initial;
        const std::string source = "export int Probe(){return Object.Target().Value" + std::string(item.operation) + "Object.Rhs();}";
        ExpectInt(Invoke(fixture, source.c_str(), "integer property compound", debug), item.expected,
            "integer property compound uses typed arithmetic and returns stored value");
        Check(fixture.trace == "RTGS" && fixture.objects[1].value == item.expected,
            "integer property compound preserves RHS target getter setter order");
    }
    for (const char* operation : {"%=", "<<=", ">>="})
    {
        Fixture fixture;
        const char* divisor = std::string(operation) == "%=" ? "0" : "64";
        const std::string source = "export int Probe(){\nObject.Value" + std::string(operation) + divisor + ";\nreturn 1;}";
        const NeoCore::Result result = Invoke(fixture, source.c_str(), "integer property error", debug);
        ExpectError(result, "integer property errors stop before setter", std::string(operation) == "%=" ? "zero" : "shift");
        Check(fixture.trace == "G" && fixture.objects[1].value == 10,
            "failed integer property arithmetic does not call setter or alter the value");
        if (debug) Check(result.error.line == 2, "integer property error retains source line");
    }
}

void CheckRebinding(bool debug)
{
    Fixture fixture;
    const char* source = "export int Probe(){return Object.Value;}";
    std::unique_ptr<NeoCore::Program> first = Compile(fixture, source, "first instance", debug);
    std::unique_ptr<NeoCore::Program> second = Compile(fixture, source, "second instance", debug);
    if (!first || !second) return;
    NeoCore::Error error;
    ExpectInt(first->Call("Probe"), 10, "first initial object");
    Check(first->SetHostObject("Object", NeoCore::ObjectHandle(2), error), "instance global rebind: " + error.message);
    ExpectInt(first->Call("Probe"), 20, "global is resolved from current instance binding on each call");
    ExpectInt(second->Call("Probe"), 10, "rebinding does not affect another compiled instance");
    Check(!first->SetHostObject("Object", NeoCore::ObjectHandle(), error), "nonnull host global rejects null");
    ExpectInt(first->Call("Probe"), 20, "failed rebind preserves old global");
    Check(!first->SetHostObject("Unknown", NeoCore::ObjectHandle(1), error), "rebind rejects unknown global");
    Check(first->SetHostObject("Object", NeoCore::ObjectHandle(Fixture::HighId()), error), "rebind full-width unsigned object ID");
    ExpectInt(first->Call("Probe"), 70, "high ID receiver survives signed backend payload");
    Check(fixture.bindings->BindGlobal("Other", NeoCore::ObjectHandle(Fixture::HighId())), "bind high ID argument");
    ExpectInt(Invoke(fixture,
        "export int Probe(){Object local=Object.Identity(Other);return local.Value;}", "high ID return roundtrip", debug),
        70, "native return and argument retain all 64 ID bits");
    Check(fixture.observedId == Fixture::HighId(), "host observes original unsigned object argument ID");
}

void CheckInvalidObjects(bool debug)
{
    {
        Fixture fixture;
        std::unique_ptr<NeoCore::Program> program = Compile(fixture,
            "export int Probe(){\n Object.Destroy();\n return Object.Value;\n}", "destroyed receiver.nc", debug);
        if (!program) return;
        const NeoCore::Result result = program->Call("Probe");
        ExpectError(result, "destroyed receiver fails before getter", "no longer valid");
        Check(fixture.calls == 1 && fixture.trace == "D", "dead receiver never reaches host callback");
        if (debug) Check(result.error.line == 3, "dead receiver error refers to original source line, got " + std::to_string(result.error.line));
    }
    for (int argument = 0; argument != 2; ++argument)
    {
        Fixture fixture;
        const char* source = argument == 0
            ? "export int Probe(){Object dead=Other;Object.DeleteOther();return Object.Join(dead,Object);}"
            : "export int Probe(){Object dead=Other;Object.DeleteOther();return Object.Join(Object,dead);}";
        ExpectError(Invoke(fixture, source, "all object arguments validated", debug), "dead object argument is rejected", "argument");
        Check(fixture.calls == 1, "unused or later object argument is validated before callback entry");
    }
    {
        Fixture fixture;
        Check(fixture.bindings->BindGlobal("Other", NeoCore::ObjectHandle(3)), "bind object ID with wrong nominal runtime type");
        ExpectError(Invoke(fixture, "export int Probe(){return Object.Join(Object,Other);}", "resolver type mismatch", debug),
            "resolver rejects object of different native type", "argument");
        Check(fixture.calls == 0, "wrong runtime native type cannot enter typed thunk");
    }
    {
        Fixture fixture;
        Check(fixture.bindings->BindGlobal("Object", NeoCore::ObjectHandle(999)), "stale nonzero ID may be bound without dereferencing");
        ExpectError(Invoke(fixture, "export int Probe(){return Object.Value;}", "unresolved global", debug), "stale global fails at member access");
        Check(fixture.calls == 0, "unresolved global never reaches callback");
    }
}

void CheckReentry(bool debug)
{
    {
        Fixture fixture;
        fixture.Override("ReenterText", [&fixture](NeoCore::HostCall& call) {
            fixture.nestedSucceeded = call.CallScript("Nested").ok();
            call.ReturnString("ignored after nested failure");
            return true;
        });
        std::unique_ptr<NeoCore::Program> program = Compile(fixture,
            "string text=Object.ReenterText(\"initial\");export void Nested(){Object.EchoText(text);}"
            "export int Probe(){Object.Touch();return 1;}", "global initialization rejects reentry", debug);
        if (program)
        {
            ExpectError(program->Call("Probe"), "nested call cannot read not-yet-initialized typed globals", "initialization");
            Check(!fixture.nestedSucceeded && fixture.calls == 1 && fixture.trace.empty(),
                "incomplete initialization never enters nested callback or exported body");
            ExpectError(program->Run(), "failed initialization remains failed", "initialization");
            Check(fixture.calls == 1, "failed initialization is not replayed");
        }
    }
    {
        Fixture fixture;
        fixture.Override("Reenter", [&fixture](NeoCore::HostCall& call) {
            Check(call.Receiver() != nullptr, "receiver valid before nested call");
            const NeoCore::Result nested = call.CallScript("Nested");
            fixture.nestedSucceeded = nested.ok();
            fixture.nullReceiverAfterReentry = call.Receiver() == nullptr;
            call.ReturnInt(99);
            return true;
        });
        ExpectError(Invoke(fixture,
            "export void Nested(){Object.Destroy();}export int Probe(){int x=Object.Reenter();Object.Touch();return x;}",
            "reentry destroys receiver", debug), "reentry invalidates cached receiver", "no longer valid");
        Check(fixture.nestedSucceeded && fixture.nullReceiverAfterReentry && fixture.trace == "D",
            "nested destruction succeeds but outer callback cannot resume with stale pointer");
    }
    {
        Fixture fixture;
        fixture.Override("ReenterObject", [&fixture](NeoCore::HostCall& call) {
            Check(call.Object(0) != nullptr, "argument valid before nested call");
            fixture.nestedSucceeded = call.CallScript("Nested").ok();
            fixture.nullObjectAfterReentry = call.Object(0) == nullptr;
            call.ReturnInt(99);
            return true;
        });
        ExpectError(Invoke(fixture,
            "export void Nested(){Object.DeleteOther();}export int Probe(){return Object.ReenterObject(Other);}",
            "reentry destroys argument", debug), "reentry invalidates every cached argument", "argument");
        Check(fixture.nestedSucceeded && fixture.nullObjectAfterReentry, "argument cache is re-resolved after script callback");
    }
    {
        Fixture fixture;
        fixture.Override("Reenter", [&fixture](NeoCore::HostCall& call) {
            fixture.nestedSucceeded = call.CallScript("Nested").ok();
            call.ReturnInt(99);
            return true;
        });
        ExpectError(Invoke(fixture,
            "export int Nested(){return Object.Fail();}export int Probe(){int x=Object.Reenter();Object.Touch();return x;}",
            "nested host failure remains sticky", debug), "swallowed nested failure propagates", "mock host failure");
        Check(!fixture.nestedSucceeded && fixture.trace.empty(), "failed nested callback prevents outer script continuation");
    }
    {
        Fixture fixture;
        fixture.Override("Reenter", [&fixture](NeoCore::HostCall& call) {
            const NeoCore::Result nested = call.CallScript("Nested", {NeoCore::Scalar::Int(7)});
            fixture.nestedSucceeded = nested.ok();
            call.ReturnInt(nested.ok() ? nested.value.asInt() + static_cast<Object*>(call.Receiver())->value : 0);
            return true;
        });
        ExpectInt(Invoke(fixture,
            "export int Nested(int n){Object.Value=n;return n+1;}export int Probe(){return Object.Reenter();}",
            "successful typed reentry", debug), 15, "callback passes scalar arguments and observes re-resolved receiver");
        Check(fixture.nestedSucceeded, "successful nested exported function result");
    }
    {
        Fixture fixture;
        fixture.Override("ReenterText", [&fixture](NeoCore::HostCall& call) {
            const std::string& saved = call.String(0);
            const std::string before = saved;
            fixture.nestedSucceeded = call.CallScript("Nested").ok();
            fixture.retainedString = saved == before && call.String(0) == "before/value";
            call.ReturnString(saved);
            return true;
        });
        const NeoCore::Result result = Invoke(fixture,
            "string source=\"before\";export void Nested(){source=\"after\";list<string> slots=[];slots.resize(64,\"overwritten\");Object.Text=source;}"
            "export string Probe(){return Object.ReenterText(source..\"/value\");}", "argument string survives reentry", debug);
        Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::String && result.value.asString() == "before/value"
            && fixture.nestedSucceeded && fixture.retainedString, "owned string snapshot survives VM stack and source mutation: " + result.error.message);
    }
    {
        Fixture fixture;
        fixture.Override("Reenter", [&fixture](NeoCore::HostCall& call) {
            fixture.objects.erase(call.ReceiverId().value);
            fixture.nullReceiverAfterReentry = call.ResolveReceiver() == nullptr;
            call.ReturnInt(99);
            return true;
        });
        ExpectError(Invoke(fixture, "export int Probe(){return Object.Reenter();}", "external receiver invalidation", debug),
            "explicit resolver reports deletion outside CallScript");
        Check(fixture.nullReceiverAfterReentry, "ResolveReceiver bypasses old borrowed pointer");
    }
}

void CheckReturnContracts(bool debug)
{
    for (int variant = 0; variant != 6; ++variant)
    {
        Fixture fixture;
        fixture.Override("Bad", [variant](NeoCore::HostCall& call) {
            if (variant == 0) call.ReturnFloat(1.0);
            else if (variant == 1) call.ReturnNull();
            else if (variant == 2) call.ReturnObject(NeoCore::ObjectHandle(1));
            else if (variant == 3) {}
            else if (variant == 4) { call.ReturnInt(1); return false; }
            else { call.Fail("sticky callback failure"); call.ReturnInt(1); }
            return true;
        });
        ExpectError(Invoke(fixture,
            "export int Probe(){int value=Object.Bad();Object.Touch();return value;}", "invalid host return contract", debug),
            "incorrect host result or callback failure is not silently converted");
        Check(fixture.trace.empty(), "bad host result prevents subsequent script side effects");
    }
    {
        Fixture fixture;
        fixture.Override("Void", [](NeoCore::HostCall& call) { call.ReturnInt(1); return true; });
        ExpectError(Invoke(fixture, "export int Probe(){Object.Void();return 1;}", "void host return contract", debug),
            "void callbacks must not return a scalar", "signature");
    }
    {
        Fixture fixture;
        fixture.Override("Identity", [](NeoCore::HostCall& call) { call.ReturnNull(); return true; });
        ExpectError(Invoke(fixture, "export int Probe(){return Object.Identity(Object).Value;}", "nonnull object return contract", debug),
            "nonnull native result rejects null", "signature");
    }
    {
        Fixture fixture;
        fixture.Override("Bad", [](NeoCore::HostCall&) -> bool { throw std::runtime_error("mock exception"); });
        ExpectError(Invoke(fixture, "export int Probe(){return Object.Bad();}", "host exception boundary", debug),
            "C++ callback exceptions become script errors", "mock exception");
    }
}

void CheckErrorLocations(bool debug)
{
    {
        Fixture fixture;
        const NeoCore::HostProperty* property = fixture.schema.FindProperty(fixture.objectType, "Value");
        Check(property && fixture.bindings->Bind(property->setter, [](NeoCore::HostCall& call) {
            call.Fail("setter failure from helper");
            return false;
        }), "override property setter for source mapping");
        const NeoCore::Result result = Invoke(fixture,
            "export int Probe(){\n Object.Value+=2;\n return 1;\n}", "helper_setter.nc", debug);
        ExpectError(result, "generated property helper propagates setter failure", "setter failure from helper");
        if (debug)
        {
            Check(result.error.line == 2, "helper setter error maps to source line 2, got " + std::to_string(result.error.line));
            Check(result.error.sourceName == "helper_setter.nc", "helper setter error keeps source name");
        }
    }
    {
        Fixture fixture;
        const NeoCore::Result result = Invoke(fixture,
            "export int Probe(){\n Object.Value/=0;\n return 1;\n}", "helper_arithmetic.nc", debug);
        ExpectError(result, "generated property helper preserves arithmetic failure", "zero");
        if (debug) Check(result.error.line == 2, "helper arithmetic error maps to source line 2, got " + std::to_string(result.error.line));
    }
    {
        Fixture fixture;
        fixture.Override("Reenter", [](NeoCore::HostCall& call) {
            call.CallScript("Nested");
            call.ReturnInt(1);
            return true;
        });
        const NeoCore::Result result = Invoke(fixture,
            "export int Nested(){\n return Object.Fail();\n}\nexport int Probe(){\n return Object.Reenter();\n}",
            "nested_error.nc", debug);
        ExpectError(result, "deepest nested failure retains structured location", "mock host failure");
        if (debug) Check(result.error.line == 2, "outer ctx.fail must preserve inner source line 2, got " + std::to_string(result.error.line));
    }
    {
        Fixture fixture;
        std::unique_ptr<NeoCore::Program> program = Compile(fixture,
            "export int First(){\n return Object.Fail();\n}\nexport int Second(){\n return Object.Fail();\n}\nexport int Recovery(){return 17;}",
            "fresh_error.nc", debug);
        if (program)
        {
            const NeoCore::Result first = program->Call("First");
            ExpectError(first, "first independently failed invocation");
            ExpectInt(program->Call("Recovery"), 17, "successful fresh invocation after failure");
            const NeoCore::Result second = program->Call("Second");
            ExpectError(second, "second independently failed invocation");
            if (debug) Check(first.error.line == 2 && second.error.line == 5,
                "new execution clears stale error location, got " + std::to_string(first.error.line) + "/" + std::to_string(second.error.line));
        }
    }
    {
        Fixture fixture;
        std::unique_ptr<NeoCore::Program> program = Compile(fixture,
            "int first=1;\nint second=2;\nint bad=Object.Fail();\nexport int Probe(){return 1;}",
            "global_error.nc", debug);
        if (program)
        {
            const NeoCore::Result result = program->Run();
            ExpectError(result, "global initializer reports structured host failure", "mock host failure");
            if (debug) Check(result.error.line == 3 && result.error.sourceName == "global_error.nc",
                "global init preserves original source line 3, got " + std::to_string(result.error.line));
        }
    }
}

void CheckSchemaStructures(bool debug)
{
    NeoCore::HostSchema schema;
    NeoCore::TypeRegistry& types = schema.Types();
    const NeoCore::TypeId owner = schema.AddObject("HostShape");
    const NeoCore::TypeId point = types.DeclareStruct("HostPoint");
    const NeoCore::TypeId node = types.DeclareStruct("HostNode");
    Check(types.DefineStructFields(point, {{"x", types.Int()}, {"y", types.Int()}}), "define host point fields");
    Check(types.DefineStructFields(node, {{"value", types.Int()}, {"children", types.ListOf(node)}}),
        "define host recursive collection fields");
    const NeoCore::HostBindingId echo = schema.AddMethod(owner, "Echo", node, {node});
    Check(echo != NeoCore::InvalidHostBinding, "accept recursive host type graph without recursion");
    Check(schema.AddProperty(owner, "Point", point), "declare host struct property");
    Check(schema.AddGlobal("Host", owner), "declare host shape global");
    int object = 0;
    NeoCore::Scalar saved;
    NeoCore::HostBindings bindings(schema,
        [](void* data, NeoCore::TypeId, NeoCore::ObjectHandle id) -> void* { return id.value == 1 ? data : nullptr; }, &object);
    Check(bindings.Bind(echo, [](NeoCore::HostCall& call) { call.ReturnValue(call.Argument(0)); return true; }),
        "bind host struct method");
    const NeoCore::HostProperty* property = schema.FindProperty(owner, "Point");
    Check(property && bindings.Bind(property->getter, [&saved](NeoCore::HostCall& call) { call.ReturnValue(saved); return true; }) &&
        bindings.Bind(property->setter, [&saved](NeoCore::HostCall& call) { saved = call.Argument(0); return true; }),
        "bind host struct property");
    Check(bindings.BindGlobal("Host", NeoCore::ObjectHandle(1)), "bind host shape global");
    NeoCore::Error error;
    auto program = NeoCore::Program::Compile(
        "export int Probe(){HostPoint a=HostPoint(4,5);Host.Point=a;HostPoint b=Host.Point;b.x=9;"
        "HostPoint c=Host.Point;list<HostNode> children=[];HostNode n=HostNode(3,children);"
        "HostNode copy=Host.Echo(n);copy.value=10;return c.x*100+n.value*10+copy.value;}",
        bindings, error, "host_structures.nc", debug);
    Check(program != nullptr, "compile host-declared struct helpers: " + error.message);
    if (program) ExpectInt(program->Call("Probe"), 440, "host structs preserve method/property value copies");
}

void CheckRejectedSource(bool debug)
{
    struct Rejected { const char* source; const char* name; };
    const Rejected cases[] = {
        {"export int Probe(){return Object.EchoInt(1.5);}", "float argument cannot narrow to int"},
        {"export int Probe(){return Object.EchoInt(true);}", "bool argument is not int"},
        {"export int Probe(){return Object.EchoInt();}", "host argument count"},
        {"export int Probe(){return Object.Missing(1);}", "unknown method"},
        {"export int Probe(){return Object.Missing;}", "unknown property"},
        {"export int Probe(){Object.ReadOnly=3;return 1;}", "readonly property assignment"},
        {"export int Probe(){Object.ReadOnly++;return 1;}", "readonly property increment"},
        {"export int Probe(){return Object.WriteOnly;}", "writeonly property read"},
        {"export int Probe(){Object.WriteOnly+=1;return 1;}", "writeonly property compound read"},
        {"export int Probe(){Object.Value=\"wrong\";return 1;}", "property assignment type"},
        {"export int Probe(){return Object.Join(Object,Alien);}", "native nominal argument type"},
        {"export int Probe(){return Object.Join(Object,null);}", "nonnull object argument"},
        {"export int Probe(){return Maybe.Value;}", "nullable object dereference"},
        {"export int Probe(){if(Maybe!=null)return Maybe.Value;return 0;}", "nullable narrowing remains unsupported"},
        {"export int Probe(){return Object.MaybeChild.Value;}", "nullable native property dereference"},
        {"export int Probe(){int value=Object.NullableInt();return value;}", "nullable scalar return cannot unwrap"},
        {"export int Probe(){Object value=Object();return 1;}", "native types cannot be constructed"},
        {"export int Probe(){int value=Object;return value;}", "native opaque IDs do not become integers"},
        {"export int Probe(){return toint(Object);}", "native opaque ID numeric conversion"},
        {"export int Probe(){Object=Other;return 1;}", "script cannot rebind host global"},
        {"export int Probe(){int& value=Object.Value;return 1;}", "host property cannot escape as reference"},
        {"struct S{int value;};export int Probe(){return Object.EchoInt(S(1));}", "struct cannot enter scalar thunk"},
        {"export int Probe(){Vector3 value;return Object.EchoInt(value);}", "unimplemented vector boundary rejected"}
    };
    Fixture fixture;
    for (const Rejected& rejected : cases)
    {
        NeoCore::Error error;
        std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(rejected.source, *fixture.bindings, error, rejected.name, debug);
        Check(!program && !error.message.empty() && error.line == 1, std::string(rejected.name) + " rejected with source diagnostic: " + error.message);
    }
    Check(fixture.calls == 0, "rejected host programs execute no callbacks");
}

}

int main()
{
    for (int mode = 0; mode != 2; ++mode)
    {
        const bool debug = mode != 0;
        CheckScalarBindings(debug);
        CheckInternalNames(debug);
        CheckPropertyOrder(debug);
        CheckIntegerPropertyOperators(debug);
        CheckRebinding(debug);
        CheckInvalidObjects(debug);
        CheckReentry(debug);
        CheckReturnContracts(debug);
        CheckErrorLocations(debug);
        CheckSchemaStructures(debug);
        CheckRejectedSource(debug);
    }
    std::printf("NeoCore host regression: %d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
