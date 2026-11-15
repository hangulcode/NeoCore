#include "NeoCore.h"
#include "NeoCoreHost.h"
#include "NeoCoreHostSchemaIO.h"
#include "NeoCoreCompiler.h"
#include <cstdio>
#include <stdexcept>

using namespace NeoCore;
static int checks = 0;
static void Check(bool value, const std::string& label)
{
    ++checks;
    if (!value) throw std::runtime_error(label);
}
static void ResultInt(const Result& result, long long value)
{
    Check(result.ok() && result.value.asInt() == value, "result: " + result.error.message);
}
static void Run(const std::string& source, long long expected, bool debug)
{
    Error error;
    auto program = Program::Compile(source, error, "defaults.nc", debug);
    Check(bool(program), "compile: " + error.message);
    ResultInt(program->Call("Probe"), expected);
    std::vector<std::uint8_t> bytes;
    Check(program->SaveImage(bytes, error), "save: " + error.message);
    auto loaded = Program::LoadImage(bytes, error);
    Check(bool(loaded), "load: " + error.message);
    ResultInt(loaded->Call("Probe"), expected);
}
static void Reject(const std::string& source)
{
    Error error;
    Check(!Program::Compile(source, error) && error.line > 0, "must reject: " + source);
}
static void Host(bool debug)
{
    HostSchemaDocument document;
    HostSchema& schema = document.schema;
    TypeRegistry& types = schema.Types();
    const TypeId service = schema.AddObject("Service");
    const HostBindingId method = schema.AddMethod(service, "Test", types.Int(),
        {types.Int(), types.Float(), types.Bool(), types.String(), types.Vector3(), types.ListOf(types.Int())});
    Check(method != 0 && schema.AddGlobal("service", service), "host declaration");
    Check(schema.SetMethodDefaults(method, {"1.5", "true", "\"hi\\n\"", "Vector3(1, 2, 3)", "null"}), schema.LastError());
    Check(!schema.SetMethodDefaults(method, {"4"}), "reject list default integer");
    Check(!schema.SetMethodDefaults(method, {"null", "true", "\"x\"", "Vector3()", "null"}), "reject nonnullable null");
    Check(!schema.SetMethodDefaults(method, {"[]"}), "reject allocating defaults");
    Check(!schema.SetMethodDefaults(method, {"null; print(1)"}), "reject trailing statements");
    const HostBindingId out = schema.AddMethod(service, "Output", types.Void(), {types.Int(), types.Int()}, {"value", "input"},
        {ApiParameterMode::Out, ApiParameterMode::In});
    Check(!schema.SetMethodDefaults(out, {"0", "8"}), "reject out default");
    Check(schema.SetMethodDefaults(out, {"8"}), "allow trailing input default after required out");
    std::string json, message, reencoded;
    Check(document.ToJson(json, message), message);
    HostSchemaDocument restored;
    Check(HostSchemaDocument::FromJson(json, restored, message), message);
    Check(restored.ToJson(reencoded, message) && reencoded == json, "default schema roundtrip");
    const char* source = "export int Probe(){int value=0;service.Output(out value);return service.Test(value-4);}";
    StaticCompileResult a, b;
    Check(CompileStaticSource(source, a, &schema) && CompileStaticSource(source, b, &restored.schema), "host check");
    Check(a.loweredSource == b.loweredSource, "restored schema emits identical calls");
    int receiver = 0;
    HostBindings bindings(restored.schema, [](void* data, TypeId, ObjectHandle) { return data; }, &receiver);
    Check(bindings.BindGlobal("service", ObjectHandle(1)), "host global");
    Check(bindings.Bind(method, [](HostCall& call) {
        Check(call.ArgumentCount() == 6, "host always receives full arity");
        Check(call.Float(1) == 1.5 && call.Bool(2) && call.String(3) == "hi\n", "host scalar defaults");
        Check(call.Vector3(4).z == 3 && call.IsNull(5), "host vector and null defaults");
        call.ReturnInt(call.Int(0) + 6); return true;
    }), "bind callback");
    Check(bindings.Bind(out, [](HostCall& call) {
        Check(call.ArgumentCount()==2, "out call has full arity");
        call.OutInt(0, call.Int(1)); return true;
    }), "bind output");
    Error error;
    auto program = Program::Compile(source, bindings, error, "host-defaults.nc", debug);
    Check(bool(program), "host compile: " + error.message);
    ResultInt(program->Call("Probe"), 10);
    std::vector<std::uint8_t> bytes;
    Check(program->SaveImage(bytes, error), "host image save");
    auto loaded = Program::LoadImage(bytes, bindings, error);
    Check(bool(loaded), "host image load: " + error.message);
    ResultInt(loaded->Call("Probe"), 10);
    Check(!Program::Compile("service.Test();", bindings, error), "host missing required argument");
    Check(restored.schema.SetMethodDefaults(method, {"2.5", "true", "\"hi\\n\"", "Vector3(1,2,3)", "null"}), "change default");
    HostBindings changed(restored.schema, [](void* data, TypeId, ObjectHandle) { return data; }, &receiver);
    changed.BindGlobal("service", ObjectHandle(1));
    changed.Bind(method, [](HostCall&) { return true; }); changed.Bind(out, [](HostCall&) { return true; });
    Check(!Program::LoadImage(bytes, changed, error), "changed defaults invalidate old host image contract");
}
int main()
{
    try
    {
        for (bool debug : {false, true})
        {
            Run("export int Probe(){return Add(2)+Add(2,5);} int Add(int x,int y=3){return x+y;}", 12, debug);
            Run("const int B=2; int Add(int x=B*3){return x;} export int Probe(){int B=99;return Add();}", 6, debug);
            Run("int F(int n,int step=1){if(n==0)return 0;return step+F(n-1);} export int Probe(){return F(4);}", 4, debug);
            Run("int F(float a=2,bool b=true,string s=\"a\\n\",list<int> l=null,fn(int)->int f=null){if(a==2.0&&b&&s==\"a\\n\"&&l==null&&f==null)return 1;return 0;} export int Probe(){return F();}", 1, debug);
            Run("int F(Vector3 p=Vector3(1,2,3),Quaternion q=Quaternion()){p.x=9.0;return toint(p.y+q.w);} export int Probe(){return F()+F();}", 6, debug);
            Run("int F(string s=\"text\"){return s==null?1:2;} export int Probe(){return F()+F(null);}", 3, debug);
            Run("int F(int n=-9223372036854775808){return n;} export int Probe(){return F();}", (-9223372036854775807LL-1), debug);
            Run("float F(float n=0.12345678901234567){return n;} export int Probe(){return F()==0.12345678901234567?1:0;}", 1, debug);
            Run("int n=0; int Next(){n+=1;return n;} int F(int a,int b=7){return a*10+b;} export int Probe(){return F(Next())+F(Next());}", 44, debug);
            Run("import system; export int Probe(){if(\"A\".regex_match(\"a\"))return 0; if(!\"A\".regex_match(\"a\",true))return 0; if(system.jsonEncode(3)!=\"3\")return 0;return 1;}", 1, debug);
            Host(debug);
            if (debug)
            {
                Error error;
                auto program = Program::Compile("export int Probe(){\n\"x\".regex_match(\n\"x\"\n);\nint z=0;\nreturn 1/z;\n}", error, "lines.nc", true);
                Check(bool(program), "multiline defaults compile");
                Result failure = program->Call("Probe");
                Check(!failure.ok() && failure.error.line == 6, "defaults preserve following runtime error line");
            }
            CompileOptions options;
            options.moduleLoader = [](const std::string&, const std::string&, ModuleSource& source, Error&) {
                source.sourceName="lib.nc"; source.source="const int N=4; public int F(int x=N){return x;}"; return true;
            };
            Error error;
            auto module = Program::Compile("import lib; export int Probe(){return lib.F();}", error, "entry.nc", debug, options);
            Check(bool(module), "module defaults: " + error.message);
            ResultInt(module->Call("Probe"), 4);
        }
        for (const char* source : {
            "void F(int x=1,int y){}", "void F(int x=null){}", "void F(int x=1.5){}",
            "int x=2;void F(int y=x){}", "void F(int x,int y=x){}", "void F(int x=L){} const int L=1;",
            "int x=2;void F(int y=false?x:1){}", "int G(){return 1;} void F(int y=false?G():1){}",
            "int G(){return 1;} void F(int x=G()){}", "void F(list<int> x=[]){}",
            "void F(int x=1/0){}", "void F(int x){} F();", "void F(int x=1){} F(1,2);",
            "int F(int x=1){return x;} fn(int)->int f=F; int x=f();",
            "fn(int)->int f=fn(int x=1)->int{return x;};", "void F(out int x=1){}"}) Reject(source);
        std::vector<StaticDeclaration> declarations;
        StaticCompileResult implicit, explicitCall;
        Check(CompileStaticSource("int F(int x=3){return x;} F();", implicit), "implicit default IR");
        Check(CompileStaticSource("int F(int x=3){return x;} F(3);", explicitCall), "explicit default IR");
        Check(implicit.loweredSource == explicitCall.loweredSource && implicit.typedIR.nodes.size() == explicitCall.typedIR.nodes.size(), "same call skeleton and IR size");
        for (std::size_t i=0;i<implicit.typedIR.nodes.size();++i)
            Check(implicit.typedIR.nodes[i].op==explicitCall.typedIR.nodes[i].op && implicit.typedIR.nodes[i].integer==explicitCall.typedIR.nodes[i].integer,
                "same operations and integer payloads for implicit and explicit defaults");
        CollectStaticDeclarations("int F(int x=2,Vector3 p=Vector3(1,2,3)){return x;}", declarations);
        Check(!declarations.empty() && declarations[0].parameters.size()==2 && declarations[0].parameters[1].find("= Vector3(1,2,3)")!=std::string::npos, "tooling default signatures");
        std::printf("default arguments: %d checks passed\n", checks);
        return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
}
