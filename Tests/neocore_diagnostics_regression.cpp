#include "NeoCoreHost.h"
#include <cstdio>
#include <stdexcept>

namespace {
int checks = 0;
void Require(bool value, const std::string& label) {
    ++checks; if (!value) throw std::runtime_error(label);
}
void Test(bool image) {
    NeoCore::CompileOptions options;
    options.moduleLoader = [](const std::string&, const std::string&, NeoCore::ModuleSource& source, NeoCore::Error&) {
        source.sourceName = "leaf.nc";
        source.source = "public int Leaf(){\n int z=0;\n return 7/z;\n}\npublic int Middle(){\n return Leaf();\n}\n";
        return true;
    };
    NeoCore::Error error;
    auto program = NeoCore::Program::Compile("import leaf;\nexport int Root(){\n return leaf.Middle();\n}\n"
        "export int Recovery(){return 1;}\n", error, "root.nc", true, options);
    Require(program != nullptr, "compile: " + error.message);
    if (image) {
        std::vector<std::uint8_t> bytes;
        Require(program->SaveImage(bytes,error), "save image");
        program = NeoCore::Program::LoadImage(bytes,error);
        Require(program != nullptr, "load image: " + error.message);
    }
    auto failure = program->Call("Root");
    Require(!failure.ok(), "division fails");
    Require(failure.error.sourceName=="leaf.nc" && failure.error.line==3, "root cause: " + failure.error.message);
    const auto& stack = failure.error.stackTrace;
    Require(stack.size()==3, "three user frames: " + failure.error.message);
    Require(stack[0].functionName.find("Leaf")!=std::string::npos && stack[0].sourceName=="leaf.nc" && stack[0].line==3, "leaf location");
    Require(stack[1].functionName.find("Middle")!=std::string::npos && stack[1].line==6, "middle call site");
    Require(stack[2].functionName=="Root" && stack[2].sourceName=="root.nc" && stack[2].line==3, "root call site");
    Require(program->Call("Recovery").ok(), "recovery");
    Require(failure.error.stackTrace[0].line==3, "snapshot owns its values");
}
void Host() {
    NeoCore::HostSchema schema;
    const auto object = schema.AddObject("Counter");
    const auto method = schema.AddMethod(object,"Reenter",schema.Types().Int(),{});
    const auto target = schema.AddMethod(object,"Target",object,{});
    Require(schema.AddProperty(object,"Value",schema.Types().Int()),"property");
    Require(schema.AddGlobal("counter",object),"global");
    int alive=1;
    NeoCore::HostBindings bindings(schema,[](void* p,NeoCore::TypeId,NeoCore::ObjectHandle)->void* {return p;},&alive);
    Require(bindings.BindGlobal("counter",NeoCore::ObjectHandle(1)),"bind global");
    Require(bindings.Bind(method,[](NeoCore::HostCall& call){call.CallScript("Inner");call.ReturnInt(0);return true;}),"reenter");
    Require(bindings.Bind(target,[](NeoCore::HostCall& call){call.Fail("receiver failed");return false;}),"target");
    const auto property = schema.FindProperty(object,"Value");
    bindings.Bind(property->getter,[](NeoCore::HostCall& call){call.ReturnInt(1);return true;});
    bindings.Bind(property->setter,[](NeoCore::HostCall&){return true;});
    NeoCore::Error error;
    auto program=NeoCore::Program::Compile(
        "export int Inner(){\n int z=0;return 1/z;\n}\n"
        "export int Outer(){\n return counter.Reenter();\n}\n"
        "export void Receiver(){\n counter.Target().Value =\n 2;\n}\n"
        "export void Compound(){\n counter.Target().Value +=\n 2;\n}\n",bindings,error,"host.nc",true);
    Require(program!=nullptr,"host compile: "+error.message);
    auto failure=program->Call("Outer");
    Require(!failure.ok() && failure.error.line==2,"inner cause retained");
    Require(failure.error.stackTrace.size()==2 && failure.error.stackTrace[0].functionName=="Inner"
        && failure.error.stackTrace[1].functionName=="Outer" && failure.error.stackTrace[1].line==5,
        "host reentry retains outer frame: "+failure.error.message);
    failure=program->Call("Receiver");
    Require(!failure.ok() && failure.error.line==8,"multiline receiver location: "+failure.error.message);
    failure=program->Call("Compound");
    Require(!failure.ok() && failure.error.line==12,"multiline compound receiver location: "+failure.error.message);
}
void Retention() {
    NeoCore::Error error;
    auto program=NeoCore::Program::Compile(
        "export int Deep(int n){\n if(n>0)return Deep(n-1);\n int z=0;return 1/z;\n}\n"
        "export int Root(){return Deep(100);}\n"
        "export int Wait(){sleep(0);return Deep(0);}\n"
        "export int Good(){return 42;}\n",error,"retention.nc",true);
    Require(program!=nullptr,"retention compile");
    auto result=program->Call("Root");
    Require(!result.ok() && result.error.stackTruncated && result.error.stackTrace.size()==64,"bounded recursion snapshot");
    Require(result.error.stackTrace.front().functionName=="Deep" && result.error.stackTrace.back().functionName=="Root","recursion preserves both ends");
    Require(program->Call("Good").error.stackTrace.empty(),"success has no stale stack");
    Require(program->Call("Wait").suspended(),"wait suspends");
    result=program->Resume();
    Require(!result.ok() && result.error.line==3 && !result.error.stackTruncated && result.error.stackTrace.size()==2,"resume failure owns fresh stack");
}
}
int main() { try { Test(false);Test(true);Host();Retention();std::printf("diagnostics regression: %d checks passed\n",checks);return 0; }
catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;} }
