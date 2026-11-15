#include "NeoCoreHost.h"
#include <cstdio>
#include <stdexcept>
#include <limits>

namespace {
int checks = 0;
void Require(bool value, const std::string& label) {
    ++checks;
    if (!value) throw std::runtime_error(label);
}
void Int(const NeoCore::Result& result, std::int64_t value) {
    Require(result.ok() && !result.suspended() && result.value.kind() == NeoCore::Scalar::Kind::Int
        && result.value.asInt() == value, "integer result: " + result.error.message);
}
void Test(bool debug, bool image) {
    NeoCore::Error error;
    NeoCore::HostSchema schema;
    const auto object = schema.AddObject("Gate");
    const auto reenter = schema.AddMethod(object, "Reenter", schema.Types().Int(), {});
    Require(schema.AddGlobal("gate", object), "host global");
    int live = 1;
    NeoCore::HostBindings host(schema, [](void* data, NeoCore::TypeId, NeoCore::ObjectHandle id)->void* {
        return id.value == 1 && *static_cast<int*>(data) ? data : nullptr;
    }, &live);
    Require(host.BindGlobal("gate", NeoCore::ObjectHandle(1)), "bind global");
    Require(host.Bind(reenter, [](NeoCore::HostCall& call) {
        const auto nested = call.CallScript("Nested");
        call.ReturnInt(nested.ok() ? nested.value.asInt() : -1);
        return true;
    }), "bind reentry");
    const char* source =
        "struct Inner { int n; string text; Vector3 position; };\n"
        "struct Outer { Inner inner; list<int> shared; };\n"
        "Outer state=Outer(Inner(7,\"owned\",Vector3(1,2,3)),[1]);\n"
        "export int Nested(){Outer copy=state;copy.inner.n=99;copy.inner.text=\"changed\";"
        "copy.inner.position.x=9;copy.shared[0]=2;return copy.inner.n;}\n"
        "export int Probe(){Outer saved=state;int n=gate.Reenter();"
        "if(saved.inner.text!=\"owned\"||saved.inner.position.x!=1||n!=99)return -1;"
        "return state.inner.n*100+saved.inner.n*10+saved.shared[0];}\n"
        "export Outer Wait(){Outer snapshot=state;sleep(0);return snapshot;}\n"
        "export void Change(){state.inner.n=12;state.inner.text=\"later\";}\n"
        "export int Zero(){int z=0;return 1/z;}\n"
        "export int Bounds(){list<int> a=[3];return a[1];}\n"
        "export int Null(){list<int> a;return a[0];}\n"
        "export int Overflow(){int n=9223372036854775807;return n+1;}\n"
        "export int Recovery(){return 42;}\n";
    auto program = NeoCore::Program::Compile(source, host, error, "boundary.nc", debug);
    Require(program != nullptr, "compile: " + error.message);
    if (image) {
        std::vector<std::uint8_t> bytes;
        Require(program->SaveImage(bytes, error), "save image: " + error.message);
        program = NeoCore::Program::LoadImage(bytes, host, error);
        Require(program != nullptr, "load image: " + error.message);
    }
    auto first = program->CreateInstance(error);
    auto second = program->CreateInstance(error);
    Require(first && second, "create independent instances");
    Int(first->Call("Probe"), 772);
    Int(first->Call("Overflow"), (std::numeric_limits<std::int64_t>::min)());
    for (const char* name : {"Zero", "Bounds", "Null"}) {
        auto result = first->Call(name);
        Require(!result.ok() && result.status == NeoCore::ExecutionStatus::Failed, std::string(name)+" fails");
        Int(first->Call("Recovery"), 42);
        Int(second->Call("Recovery"), 42);
    }
    Require(first->Call("Wait").suspended(), "nested value retained over suspension");
    Require(second->Call("Change").ok(), "peer state can change");
    program.reset();
    auto retained = first->Resume();
    Require(retained.ok() && retained.value.kind() == NeoCore::Scalar::Kind::Struct, "struct return after program destruction");
    Require(retained.value.At(0).At(0).asInt() == 7 && retained.value.At(0).At(1).asString() == "owned", "suspended nested struct is isolated");
    Require(first->Call("Wait").suspended() && first->Cancel(error), "cancel suspended ownership");
    Int(first->Call("Recovery"), 42);
    live = 0;
    Require(!first->Call("Probe").ok(), "deleted host object fails");
    live = 1;
    Int(first->Call("Probe"), 772);
    Require(first->Reset(error), "reset clears instance state");
    Int(first->Call("Probe"), 772);
    first.reset(); second.reset();
    Require(retained.value.At(0).At(1).asString() == "owned", "returned nested value outlives instances");
}
}
int main() {
    try {
        for (bool debug : {false,true}) for (bool image : {false,true}) Test(debug,image);
        std::printf("boundary regression: %d checks passed\n",checks);
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
