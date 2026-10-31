#include "NeoCoreHost.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace
{
using namespace NeoCore;
int checks = 0;
int failures = 0;

void Check(bool value, const std::string& label)
{
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}

struct Object
{
    ObjectHandle parent;
    Vector2Value uv = Vector2Value(1, 2);
    Vector3Value position = Vector3Value(10, 20, 30);
    Vector4Value color = Vector4Value(1, 2, 3, 4);
    QuaternionValue rotation = QuaternionValue(1, 2, 3, 4);
};

struct Fixture
{
    HostSchema schema;
    TypeId objectType = InvalidType;
    std::unique_ptr<HostBindings> bindings;
    std::map<std::uint64_t, Object> objects;
    std::string trace;
    std::vector<std::uint64_t> setters;
    int reads = 0;
    int resolves = 0;
    int badResult = 0;
    bool destroyDuringGet = false;
    bool reenterDuringGet = false;
    bool failDuringGet = false;
    bool failDuringSet = false;
    bool reentrySucceeded = false;
    bool argumentsPreserved = false;

    static void* Resolve(void* data, TypeId type, ObjectHandle id)
    {
        Fixture& fixture = *static_cast<Fixture*>(data);
        ++fixture.resolves;
        const auto found = fixture.objects.find(id.value);
        return type == fixture.objectType && found != fixture.objects.end() ? &found->second : nullptr;
    }

    void Property(const char* name, TypeId type, bool read = true, bool write = true)
    {
        Check(schema.AddProperty(objectType, name, type, read, write), std::string("register ") + name);
    }

    void Method(const char* name, TypeId result, const std::vector<TypeId>& parameters = {})
    {
        Check(schema.AddMethod(objectType, name, result, parameters) != InvalidHostBinding, std::string("register ") + name);
    }

    Fixture()
    {
        objectType = schema.AddObject("GameObject");
        TypeRegistry& types = schema.Types();
        Property("Parent", objectType, true, false);
        Property("Position", types.Vector3());
        Property("UV", types.Vector2());
        Property("Color", types.Vector4());
        Property("Rotation", types.Quaternion());
        Property("ReadOnly", types.Vector3(), true, false);
        Property("WriteOnly", types.Vector3(), false, true);
        Method("Target", objectType);
        Method("Rhs", types.Float());
        Method("Reparent", types.Void());
        Method("DestroyParent", types.Void());
        Method("Destroy", types.Void());
        Method("Echo2", types.Vector2(), {types.Vector2()});
        Method("Echo3", types.Vector3(), {types.Vector3()});
        Method("Echo4", types.Vector4(), {types.Vector4()});
        Method("EchoQ", types.Quaternion(), {types.Quaternion()});
        Check(types.NullableOf(types.Vector3()) == InvalidType, "vectors never hold null");
        Method("Reenter3", types.Vector3(), {types.Vector3()});
        Method("Reenter4", types.Vector4(), {types.Vector4()});
        Check(schema.AddGlobal("GameObject", objectType), "register primary global");
        Check(schema.AddGlobal("Other", objectType), "register parent global");
        objects[1].parent = ObjectHandle(2);
        objects[2].position = Vector3Value(40, 50, 60);
        objects[3].position = Vector3Value(70, 80, 90);
        bindings.reset(new HostBindings(schema, &Resolve, this));
        for (const HostBinding& binding : schema.Bindings())
        {
            const HostBinding copy = binding;
            Check(bindings->Bind(binding.id, [this, copy](HostCall& call) { return Dispatch(copy, call); }), "connect member");
        }
        Check(bindings->BindGlobal("GameObject", ObjectHandle(1)), "connect primary global");
        Check(bindings->BindGlobal("Other", ObjectHandle(2)), "connect parent global");
    }

    bool Dispatch(const HostBinding& binding, HostCall& call)
    {
        Object* object = static_cast<Object*>(call.Receiver());
        if (!object) return false;
        const std::string& name = binding.name;
        if (binding.kind == HostBindingKind::Getter)
        {
            if (name == "Parent") { trace += "P"; call.ReturnObject(object->parent); return true; }
            trace += "G";
            ++reads;
            if (failDuringGet) { call.Fail("vector getter failed"); return false; }
            if (name == "UV") call.ReturnVector2(object->uv);
            else if (name == "Color") call.ReturnVector4(object->color);
            else if (name == "Rotation")
            {
                if (badResult) call.ReturnVector4(Vector4Value(1, 2, 3, 4));
                else call.ReturnQuaternion(object->rotation);
            }
            else if (name == "Optional") call.ReturnNull();
            else if (badResult == 1) call.ReturnVector2(Vector2Value(1, 2));
            else if (badResult == 2) call.ReturnVector4(Vector4Value(1, 2, 3, 4));
            else if (badResult == 3) call.ReturnNull();
            else call.ReturnVector3(object->position);
            // 반환 성분은 이미 복사했으므로 중첩 실행이나 객체 삭제 뒤에도 포인터를 읽지 않는다.
            if (destroyDuringGet) objects.erase(call.ReceiverId().value);
            if (reenterDuringGet) reentrySucceeded = call.CallScript("Nested").ok();
            return true;
        }
        if (binding.kind == HostBindingKind::Setter)
        {
            trace += "S";
            setters.push_back(call.ReceiverId().value);
            if (failDuringSet) { call.Fail("vector setter failed"); return false; }
            if (name == "UV") object->uv = call.Vector2(0);
            else if (name == "Color") object->color = call.Vector4(0);
            else if (name == "Rotation") object->rotation = call.Quaternion(0);
            else object->position = call.Vector3(0);
            return true;
        }
        if (name == "Target") { trace += "T"; call.ReturnObject(call.ReceiverId()); }
        else if (name == "Rhs") { trace += "R"; call.ReturnFloat(3); }
        else if (name == "Reparent") { trace += "C"; object->parent = ObjectHandle(3); }
        else if (name == "DestroyParent") { trace += "D"; objects.erase(object->parent.value); }
        else if (name == "Destroy") { trace += "D"; objects.erase(call.ReceiverId().value); }
        else if (name == "Echo2") call.ReturnVector2(call.Vector2(0));
        else if (name == "Echo3") call.ReturnVector3(call.Vector3(0));
        else if (name == "Echo4") call.ReturnVector4(call.Vector4(0));
        else if (name == "EchoQ") call.ReturnQuaternion(call.Quaternion(0));
        else if (name == "Maybe") call.ReturnNull();
        else if (name == "AcceptMaybe") call.ReturnBool(call.IsNull(0));
        else if (name == "Reenter3")
        {
            const Vector3Value before = call.Vector3(0);
            reentrySucceeded = call.CallScript("Nested").ok();
            const Vector3Value after = call.Vector3(0);
            argumentsPreserved = before.x == after.x && before.y == after.y && before.z == after.z;
            call.ReturnVector3(after);
        }
        else if (name == "Reenter4")
        {
            const Vector4Value before = call.Vector4(0);
            reentrySucceeded = call.CallScript("Nested").ok();
            const Vector4Value after = call.Vector4(0);
            argumentsPreserved = before.x == after.x && before.y == after.y && before.z == after.z && before.w == after.w;
            call.ReturnVector4(after);
        }
        else { call.Fail("unimplemented fixture member"); return false; }
        return true;
    }
};

std::unique_ptr<Program> Compile(Fixture& fixture, const char* source, bool debug)
{
    Error error;
    std::unique_ptr<Program> program = Program::Compile(source, *fixture.bindings, error, "vector_host.nc", debug);
    Check(program != nullptr, std::string("compile: ") + error.message);
    return program;
}

Result Run(Fixture& fixture, const char* source, bool debug)
{
    std::unique_ptr<Program> program = Compile(fixture, source, debug);
    if (program) return program->Call("Probe");
    Result result;
    result.error.message = "compile failed";
    return result;
}

void ExpectFloat(const Result& result, double expected, const char* label)
{
    Check(result.ok() && result.value.kind() == Scalar::Kind::Float && result.value.asFloat() == expected,
        std::string(label) + ": " + result.error.message);
}

void ExpectError(const Result& result, const char* text, const char* label)
{
    Check(!result.ok() && result.error.message.find(text) != std::string::npos,
        std::string(label) + ": " + result.error.message);
}

void RoundTrips(bool debug)
{
    Fixture fixture;
    ExpectFloat(Run(fixture,
        "export float Probe(){GameObject.UV=GameObject.Echo2(Vector2(5,6));"
        "GameObject.Position=GameObject.Echo3(Vector3(7,8,9));"
        "GameObject.Color=GameObject.Echo4(Vector4(10,11,12,13));"
        "GameObject.Rotation=GameObject.EchoQ(Quaternion(14,15,16,17));"
        "return GameObject.UV.x+GameObject.Position.y+GameObject.Color.w+GameObject.Rotation.w;}", debug),
        40, "all vector dimensions and quaternion roundtrip");
    Check(fixture.objects[1].rotation.x == 15 && fixture.objects[1].rotation.z == 17, "quaternion wxyz ordering");
}

void StoredParent(bool debug)
{
    Fixture fixture;
    ExpectFloat(Run(fixture,
        "export float Probe(){GameObject pr=GameObject.Parent;"
        "if(pr!=null){GameObject.Reparent();pr.Position.x=3.0;return pr.Position.x;}return -1.0;}", debug),
        3, "stored parent survives relationship change");
    Check(fixture.objects[1].parent.value == 3 && fixture.objects[2].position.x == 3 && fixture.objects[3].position.x == 70,
        "write uses saved parent ID instead of current parent");
    Check(fixture.setters.size() == 1 && fixture.setters[0] == 2, "saved parent setter ID");
    Fixture empty;
    empty.objects[1].parent = ObjectHandle();
    ExpectFloat(Run(empty,
        "export float Probe(){GameObject pr=GameObject.Parent;if(pr!=null){pr.Position.x=3.0;}return 9.0;}", debug),
        9, "absent parent guard");
    Check(empty.setters.empty() && empty.reads == 0, "null parent never reaches vector callbacks");
    Fixture dead;
    Result result = Run(dead,
        "export float Probe(){\nGameObject pr=GameObject.Parent;\n"
        "if(pr!=null){GameObject.DestroyParent();\npr.Position.x=3.0;}return 0.0;}", debug);
    ExpectError(result, "no longer valid", "stored parent invalidated before component access");
    Check(dead.setters.empty() && dead.reads == 0, "destroyed parent rejected before getter");
    if (debug) Check(result.error.line == 4, "dead parent component error has original line");
}

void WritesAndOrder(bool debug)
{
    Fixture fixture;
    ExpectFloat(Run(fixture,
        "export float Probe(){GameObject.Target().Position.x=GameObject.Rhs();return 1.0;}", debug),
        1, "component assignment");
    Check(fixture.trace == "RTGS" && fixture.objects[1].position.x == 3 && fixture.objects[1].position.y == 20 &&
        fixture.objects[1].position.z == 30, "RHS precedes one receiver and getter/setter pair preserves other components");
    fixture.trace.clear(); fixture.setters.clear();
    ExpectFloat(Run(fixture,
        "export float Probe(){GameObject.Target().Position.x+=GameObject.Rhs();"
        "float old=GameObject.Position.x++;float now=++GameObject.Position.x;return old*100.0+now;}", debug),
        608, "compound and pre/post component values");
    Check(fixture.trace == "RTGSGSGS" && fixture.setters.size() == 3 && fixture.objects[1].position.x == 8,
        "each component update invokes one getter and one setter");
    Fixture copies;
    ExpectFloat(Run(copies,
        "export float Probe(){Vector3 p=GameObject.Position;p.x=99.0;return GameObject.Position.x+p.x;}", debug),
        109, "detached vector copy is independent");
    Check(copies.setters.empty() && copies.objects[1].position.x == 10, "local copy does not implicitly write host");
    Fixture rotation;
    ExpectFloat(Run(rotation,
        "export float Probe(){GameObject.Rotation.w=9.0;GameObject.Rotation.x+=2.0;"
        "return GameObject.Rotation.w+GameObject.Rotation.x;}", debug), 13, "quaternion named component storage order");
    Check(rotation.objects[1].rotation.y == 3 && rotation.objects[1].rotation.z == 4, "quaternion untouched components");
}

void LifetimeAndErrors(bool debug)
{
    Fixture fixture;
    fixture.destroyDuringGet = true;
    Result result = Run(fixture, "export float Probe(){\nGameObject.Position.x=3.0;\nreturn 0.0;}", debug);
    ExpectError(result, "no longer valid", "getter destroys receiver before setter");
    Check(fixture.reads == 1 && fixture.setters.empty(), "setter resolves ID again after getter");
    if (debug) Check(result.error.line == 2, "failed setter resolution has source component line");
    Fixture reentry;
    reentry.reenterDuringGet = true;
    result = Run(reentry,
        "export void Nested(){GameObject.Destroy();}\n"
        "export float Probe(){\nGameObject.Position.x=3.0;\nreturn 0.0;}", debug);
    ExpectError(result, "no longer valid", "getter reentry destroys receiver");
    Check(reentry.reentrySucceeded && reentry.setters.empty(), "reentry succeeds but stale ID setter fails");
    if (debug) Check(result.error.line == 3, "reentry writeback error points to component source");
    for (int stage = 0; stage != 2; ++stage)
    {
        Fixture failing;
        failing.failDuringGet = stage == 0;
        failing.failDuringSet = stage == 1;
        result = Run(failing, "export float Probe(){\nGameObject.Position.x+=3.0;\nreturn 0.0;}", debug);
        ExpectError(result, stage == 0 ? "vector getter failed" : "vector setter failed", "host vector failure propagates");
        if (debug) Check(result.error.line == 2, "host vector failure keeps component source location");
    }
    for (int bad = 1; bad <= 3; ++bad)
    {
        Fixture mismatch;
        mismatch.badResult = bad;
        ExpectError(Run(mismatch, "export float Probe(){return GameObject.Position.x;}", debug),
            "declared signature", "wrong vector dimension or null return rejected");
    }
    Fixture quaternion;
    quaternion.badResult = 1;
    ExpectError(Run(quaternion, "export float Probe(){return GameObject.Rotation.w;}", debug),
        "declared signature", "Vector4 cannot masquerade as Quaternion host result");
}

void WholeVectorArithmetic(bool debug)
{
    Fixture fixture;
    ExpectFloat(Run(fixture,
        "export float Probe(){GameObject.Target().Position+=Vector3(GameObject.Rhs(),4,5);return 1.0;}", debug),
        1, "host whole-vector addition");
    Check(fixture.trace == "RTGS" && fixture.objects[1].position.x == 13 &&
        fixture.objects[1].position.y == 24 && fixture.objects[1].position.z == 35,
        "whole-vector addition evaluates RHS then receiver and one getter setter pair");
    fixture.trace.clear(); fixture.setters.clear();
    ExpectFloat(Run(fixture,
        "export float Probe(){GameObject.Position-=Vector3(1,2,3);"
        "GameObject.Position*=2;GameObject.Position/=4.0;return 1.0;}", debug),
        1, "host whole-vector subtraction multiplication division");
    Check(fixture.trace == "GSGSGS" && fixture.setters.size() == 3 &&
        fixture.objects[1].position.x == 6 && fixture.objects[1].position.y == 11 &&
        fixture.objects[1].position.z == 16, "whole-vector compounds preserve vector result type");
    Fixture dimensions;
    ExpectFloat(Run(dimensions,
        "export float Probe(){GameObject.UV+=Vector2(3,4);GameObject.Color*=2.0;"
        "Vector4 value=GameObject.Color-Vector4(1,1,1,1);return GameObject.UV.x+value.w;}", debug),
        11, "host vector2 and vector4 arithmetic");
    Check(dimensions.objects[1].color.w == 8, "local host vector arithmetic does not mutate getter source");
    Fixture parent;
    ExpectFloat(Run(parent,
        "export float Probe(){GameObject pr=GameObject.Parent;if(pr!=null){GameObject.Reparent();"
        "pr.Position+=Vector3(1,2,3);return pr.Position.z;}return -1.0;}", debug),
        63, "whole-vector compound on saved parent ID");
    Check(parent.setters.size() == 1 && parent.setters[0] == 2 && parent.objects[3].position.z == 90,
        "whole-vector compound retains original object identity");
    Fixture dead;
    dead.destroyDuringGet = true;
    const Result result = Run(dead,
        "export float Probe(){\nGameObject.Position+=Vector3(1,2,3);\nreturn 0.0;}", debug);
    ExpectError(result, "no longer valid", "whole-vector getter can destroy receiver before setter");
    Check(dead.reads == 1 && dead.setters.empty(), "whole-vector setter revalidates ID after getter");
    if (debug) Check(result.error.line == 2, "whole-vector failure retains source line");
    Fixture scalarOrder;
    ExpectFloat(Run(scalarOrder,
        "export float Probe(){GameObject.Target().Position*=GameObject.Rhs();return 1.0;}", debug),
        1, "host scalar compound order");
    Check(scalarOrder.trace == "RTGS" && scalarOrder.objects[1].position.x == 30,
        "scalar compound executes RHS and target once");
}

void ReentrySnapshots(bool debug)
{
    Fixture v3;
    ExpectFloat(Run(v3,
        "Vector3 value=Vector3(1,2,3);"
        "export void Nested(){value=Vector3(90,80,70);}"
        "export float Probe(){Vector3 result=GameObject.Reenter3(value);return result.x+value.x;}", debug),
        91, "vector3 snapshot survives nested script");
    Check(v3.reentrySucceeded && v3.argumentsPreserved, "vector3 arguments owned across reentry");
    Fixture v4;
    ExpectFloat(Run(v4,
        "Vector4 value=Vector4(1,2,3,4);"
        "export void Nested(){value.x=90.0;value.w=70.0;}"
        "export float Probe(){Vector4 result=GameObject.Reenter4(value);return result.w+value.w;}", debug),
        74, "heap vector4 snapshot survives nested COW writes");
    Check(v4.reentrySucceeded && v4.argumentsPreserved, "vector4 arguments owned across reentry");
}

void Narrowing(bool debug)
{
    Fixture fixture;
    ExpectFloat(Run(fixture,
        "export float Probe(){GameObject.Position=Vector3(3.4028235e38,0.0,0.0);return GameObject.Position.x;}", debug),
        static_cast<double>((std::numeric_limits<float>::max)()), "FLT_MAX shortest decimal roundtrip");
    ExpectFloat(Run(fixture, "export float Probe(){GameObject.Position.x=1e300;return GameObject.Position.x;}", debug),
        std::numeric_limits<double>::infinity(), "component overflow narrows to infinity");
    ExpectFloat(Run(fixture, "export float Probe(){GameObject.Position.x=16777217.0;return GameObject.Position.x;}", debug),
        16777216.0, "component double narrows once to binary32");
    ExpectFloat(Run(fixture, "export float Probe(){return (GameObject.Position.x=16777217.0);}", debug),
        16777216.0, "assignment value matches stored binary32 component");
    ExpectFloat(Run(fixture, "export float Probe(){GameObject.Position.x=-0.0;return GameObject.Position.x;}", debug),
        -0.0, "signed zero roundtrip");
    Check(std::signbit(fixture.objects[1].position.x), "float32 setter preserves negative zero");
}

void CompileRejections()
{
    const char* sources[] = {
        "export void Probe(){GameObject.ReadOnly.x=3.0;}",
        "export void Probe(){GameObject.WriteOnly.x=3.0;}",
        "export void Probe(){float& r=GameObject.Position.x;}",
        "export void Probe(){Vector3& r=GameObject.Position;}",
        "export void Probe(){GameObject.Position=Vector2(1,2);}",
        "export void Probe(){GameObject.Rotation=Vector4(1,2,3,4);}",
        "export void Probe(){GameObject.Echo4(Quaternion(1,2,3,4));}",
        "export void Probe(){GameObject.Position.w=3.0;}",
        "export void Probe(){GameObject.ReadOnly+=Vector3(1,2,3);}",
        "export void Probe(){GameObject.WriteOnly*=2;}",
        "export void Probe(){GameObject.Position+=Vector2(1,2);}",
        "export void Probe(){GameObject.Position*=Vector3(1,2,3);}",
        "export void Probe(){GameObject.Rotation+=Quaternion();}",
        "export void Probe(){GameObject.Rotation*=2;}",
        "void Clear(GameObject& value){value=null;}"
        "export void Probe(){GameObject pr=GameObject.Parent;if(pr!=null){Clear(pr);pr.Position.x=3.0;}}",
        "export void Probe(){GameObject? pr=GameObject.Parent;}",
        "export void Probe(){Vector3? v=GameObject.Position;}"
    };
    for (const char* source : sources)
    {
        Fixture fixture;
        Error error;
        Check(!Program::Compile(source, *fixture.bindings, error), std::string("reject incompatible vector access: ") + source);
        Check(!error.message.empty(), "rejected vector source explains reason");
        Check(fixture.trace.empty(), "compile rejection invokes no host callbacks");
    }
}
}

int main()
{
    for (int mode = 0; mode != 2; ++mode)
    {
        const bool debug = mode != 0;
        RoundTrips(debug);
        StoredParent(debug);
        WritesAndOrder(debug);
        LifetimeAndErrors(debug);
        WholeVectorArithmetic(debug);
        ReentrySnapshots(debug);
        Narrowing(debug);
    }
    CompileRejections();
    std::printf("NeoCore vector host regression: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
