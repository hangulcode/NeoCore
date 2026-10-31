#include "NeoCoreCompiler.h"
#include "NeoCoreHostSchema.h"

#include <cstdio>
#include <string>

using namespace NeoCore;

static int checks = 0;
static int failures = 0;

static void Check(bool condition, const std::string& label)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}

static void CheckGlobalInitialization(const HostSchema& schema)
{
    const char* rejected[] = {
        "string text=Make(); string Make(){Object.Name=text;return \"ok\";}",
        "string text=Make(); string Make(){return Relay();} string Relay(){Object.Name=text;return \"ok\";}",
        "string text=Make(2); string Make(int n){if(n>0)return Make(n-1);return text;}",
        "int count=First(2); int First(int n){if(n==0)return count;return Second(n-1);} int Second(int n){return First(n);}",
        "int count=Fill(); int Fill(){count=7;return 2;}",
        "int count=Fill(); int Fill(){Set();return 2;} void Set(){count=7;}",
        "Touch(); string text=\"ready\"; void Touch(){Object.Name=text;}",
        "Touch(); int count; void Touch(){Object.Count=count;}",
        "Object.Scale(Get()); int count=5; int Get(){return count;}",
        "string text=\"safe\"; int count=Make(); int Make(){return Relay();} int Relay(){Object.Name=text;return count;}"
    };
    const char* accepted[] = {
        "string text=Make(); string Make(){return \"ok\";}",
        "string earlier=\"ok\"; string text=Make(); string Make(){Object.Name=earlier;return earlier;}",
        "string text=Make(); string Make(){string text=\"local\";Object.Name=text;return text;}",
        "Object item=Get(); Object Get(){return Object;}",
        "Object item=Get(); Object Get(){return Optional;}",
        "int count=Recurse(3); int Recurse(int n){if(n==0)return 1;return Recurse(n-1);}",
        "int count=First(3); int First(int n){if(n==0)return 1;return Second(n-1);} int Second(int n){return First(n);}",
        "int count; int Get(){return count;} int copied=Get();",
        "const int count=7; int Get(){return count;} int copied=Get();",
        "string text=\"ready\"; Touch(); void Touch(){Object.Name=text;}",
        "string text=\"ready\"; void Touch(){Object.Name=text;} export void Probe(){Touch();}",
        "int count=Make(); int Make(){return 1;} export int Read(){return count;}"
    };
    {
        for (const char* source : rejected)
        {
            StaticCompileResult result;
            Check(!CompileStaticSource(source, result, &schema) &&
                result.errorMessage.find("initializer completes") != std::string::npos,
                std::string("reject incomplete global through call graph: ") + source + " : " + result.errorMessage);
        }
        for (const char* source : accepted)
        {
            StaticCompileResult result;
            Check(CompileStaticSource(source, result, &schema),
                std::string("accept initialized or independent globals: ") + source + " : " + result.errorMessage);
        }
    }
    StaticCompileResult result;
    Check(!CompileStaticSource("int value=Make(); int Make(){return value;}", result) &&
        result.errorMessage.find("initializer completes") != std::string::npos,
        "initialization dependency check also protects schema-free typed code");
    Check(!CompileStaticSource("string text=\nMake();\nstring Make(){return text;}", result) && result.errorLine == 2,
        "initialization cycle diagnostic points to the original call site");
}

static void CheckPropertySourceLines(const HostSchema& schema)
{
    StaticCompileResult result;
    const std::string source = "Object.Value=1;\nObject.Value+=2;\nObject.Count++;\n// trailing comment";
    Check(CompileStaticSource(source, result, &schema), "property source mapping fixture compiles");
    Check(result.hostWriteSourceLines.size() == 3, "every generated write helper has a source mapping");
    for (std::size_t i = 0; i < result.hostWriteSourceLines.size(); ++i)
    {
        const std::pair<int, int>& mapping = result.hostWriteSourceLines[i];
        std::size_t begin = 0;
        for (int line = 1; line < mapping.first && begin != std::string::npos; ++line)
        {
            begin = result.loweredSource.find('\n', begin);
            if (begin != std::string::npos) ++begin;
        }
        Check(mapping.second == static_cast<int>(i + 1) && begin != std::string::npos &&
            result.loweredSource.compare(begin, 4, "fun ") == 0,
            "mapping resolves generated function line to original write expression");
    }
    Check(CompileStaticSource("Object.Value=1;\n", result, &schema) &&
        result.hostWriteSourceLines.size() == 1 && result.hostWriteSourceLines[0].first == 3 &&
        result.hostWriteSourceLines[0].second == 1, "trailing newline is counted before helper line");
    Check(CompileStaticSource("int value=1;", result) && result.hostWriteSourceLines.empty(),
        "compile result reset clears old helper source mappings");
}

static void CheckInternalNames(const HostSchema& schema)
{
    StaticCompileResult result;
    const char* source = "int __neocore_host_b1=3;\n"
        "// $neocore_host_ 및 $neocore_ir_ 는 주석 내용이다.\n"
        "string text=\"$neocore_host_b1 $neocore_ir_0 $neocore_return_0\";\n"
        "Object.Count=__neocore_host_b1; Object.Name=text;";
    Check(CompileStaticSource(source, result, &schema),
        "user underscore names and internal-looking string text compile: " + result.errorMessage);
    Check(result.hostPrefix == "$neocore_host_" && result.backendIntrinsicPrefix == "$neocore_op_" &&
        result.typedIR.prefix == "$neocore_ir_" && result.typedIR.returnPrefix == "$neocore_return_",
        "internal names use a fixed prefix outside user identifier syntax");
    Check(result.loweredSource.find("\"$neocore_host_b1 $neocore_ir_0 $neocore_return_0\"") != std::string::npos,
        "lowering preserves internal-looking string content");

    const char* rejected[] = {
        "int $neocore_host_b1=3;",
        "int $neocore_return_0(){return 1;}",
        "struct $neocore_clone_1{int x;};",
        "struct S{int $neocore_1;};",
        "int value=$neocore_host_g0.call();",
        "int value=$neocore_ir_0();"
    };
    for (const char* text : rejected)
    {
        Check(!CompileStaticSource(text, result, &schema) &&
            result.errorMessage.find("unexpected character") != std::string::npos,
            std::string("source cannot spell an internal identifier: ") + text);
    }
    HostSchema copy = schema;
    const TypeId object = copy.Globals()[0].type;
    Check(!copy.AddGlobal("$neocore_host_g0", object), "host globals cannot spell internal identifiers");
    Check(copy.AddGlobal("__neocore_host_g0", object), "host globals retain legal double underscore names");
}

int main()
{
    HostSchema schema;
    const TypeId object = schema.AddObject("Object");
    const TypeId other = schema.AddObject("Other");
    const TypeId service = schema.AddObject("Service");
    TypeRegistry& types = schema.Types();
    Check(schema.AddGlobal("Object", object), "type and host global may share a name");
    Check(schema.AddGlobal("OtherObject", other), "second native global");
    Check(schema.AddGlobal("Services", service), "service global");
    Check(schema.AddGlobal("Optional", object), "second global of the same object type");
    Check(schema.AddProperty(object, "Value", types.Float()), "float property");
    Check(schema.AddProperty(object, "Count", types.Int()), "int property");
    Check(schema.AddProperty(object, "Name", types.String()), "string property");
    Check(schema.AddProperty(object, "Child", object), "native property");
    Check(schema.AddProperty(object, "ReadOnly", types.Int(), true, false), "read-only property");
    Check(schema.AddProperty(object, "WriteOnly", types.Int(), false, true), "write-only property");
    Check(schema.AddMethod(object, "Scale", types.Float(), { types.Float() }) != 0, "numeric method");
    Check(schema.AddMethod(object, "Take", types.Void(), { object }) != 0, "native parameter");
    Check(schema.AddMethod(object, "Maybe", object, {}) != 0, "object return that may be null");
    Check(schema.AddMethod(service, "Find", object, { types.String() }) != 0, "native return");
    Check(schema.AddMethod(service, "Accept", types.Void(), { object }) != 0, "object parameter that accepts null");
    CheckGlobalInitialization(schema);
    CheckPropertySourceLines(schema);
    CheckInternalNames(schema);

    const char* accepted[] = {
        "Object o=Object; float x=o.Value; o.Value=2;",
        "Object.Child.Count=3; Object.Child.Count+=4;",
        "Object.Value+=2; Object.Value-=1.5; Object.Value*=3; Object.Value/=2;",
        "Object.Count%=3; Object.Count&=7; Object.Count|=8; Object.Count^=2; Object.Count<<=1; Object.Count>>=1;",
        "int a=Object.Count++; int b=++Object.Count; int c=Object.Count--; int d=--Object.Count;",
        "float a=(Object.Value=2); float b=(Object.Value+=3);",
        "Object.WriteOnly=4; int x=(Object.WriteOnly=5);",
        "Object.Name=\"changed\"; string s=Object.Name;",
        "Object.Child=Services.Find(\"child\"); Object o=(Object.Child=Object);",
        "float x=Object.Scale(2); Object.Take(Object.Child);",
        "Object Id(Object value){return value;} Object o=Id(Object);",
        "Object Maybe(Object value){return value;} Object o=Maybe(null);",
        "Object o=Object.Maybe(); bool b=o==null; bool c=o==Object;",
        "Services.Accept(null); Services.Accept(Object); Services.Accept(Object.Maybe());",
        "Object o; bool a=Object==Object.Child; bool b=Optional!=null; Object.Take(null); Optional.Count=2; Object p=Optional;",
        "struct Holder{Object item;}; Holder h=Holder(Object); h.item.Count=2;",
        "Object o=Object; Object copy=o; copy=Object.Child;",
        "int f(){Object.Child.Count=7;return 2;} Object.Child.Count+=f();",
        "Object Get(){return Object;} Get().Count=4; Get().Count++;"
    };
    {
        for (const char* source : accepted)
        {
            StaticCompileResult result;
            Check(CompileStaticSource(source, result, &schema), std::string("accept ") + source + " : " + result.errorMessage);
            Check(result.success && !result.hostPrefix.empty(), "accepted source exposes private host prefix");
        }
    }

    const char* rejected[] = {
        "Object.Value=\"bad\";", "Object.Unknown=1;", "int x=Object.Unknown;",
        "Object.Unknown();", "Object.Scale();", "Object.Scale(1,2);", "Object.Scale(true);",
        "Object.ReadOnly=2;", "Object.ReadOnly++;", "int x=Object.WriteOnly;", "Object.WriteOnly+=2;",
        "float& x=Object.Value;", "void Set(int& x){} Set(Object.Count);",
        "Object=Object.Child;", "Object& x=Object;",
        "Object o=Object; Object& alias=o; alias=Object.Child;",
        "void Set(Object& value){}", "Object& Get(){return Object;}",
        "Object o=5;", "Object o=OtherObject;",
        "Object o=Object();", "Object? o=Object; o.Count=2;", "Object? o=Object; o.Take(Object);",
        "int x=Object;", "int x=toint(Object);",
        "float x=tofloat(Object);", "string x=tostring(Object);", "print(Object);", "print(Optional);",
        "bool x=Object==OtherObject;", "bool x=Object==5;", "bool x=Object< Object.Child;",
        "Object.Take(OtherObject);",
        "Object.Count+=0.5;", "Object.Count=0.5;", "Object.Scale(Object);",
        "int Object=1;", "int Object(){return 1;}", "void f(int Services){}",
        "struct Services{int n;};", "void f(){int Services=1;}", "Object.Child& x=Object;"
    };
    for (const char* source : rejected)
    {
        StaticCompileResult result;
        Check(!CompileStaticSource(source, result, &schema) && !result.errorMessage.empty(), std::string("reject ") + source);
    }

    StaticCompileResult result;
    Check(!CompileStaticSource("float x=Object.Value;", result), "bindings are not visible without an explicit schema");
    Check(CompileStaticSource("int x=1; x+=2;", result), "schema-free compilation remains available");
    std::printf("host frontend: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
