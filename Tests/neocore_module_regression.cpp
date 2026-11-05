#include "NeoCore.h"
#include "NeoCoreModules.h"
#include "NeoCoreHost.h"
#include "NeoCoreConsoleAccess.h"

#include <cstdio>
#include <map>
#include <string>

namespace
{

int checks = 0;
int failures = 0;
typedef std::map<std::string, std::string> Sources;

void Check(bool condition, const std::string& label)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}

NeoCore::ModuleLoader Loader(const Sources& sources)
{
    return [&sources](const std::string& name, const std::string&, NeoCore::ModuleSource& source, NeoCore::Error& error)
    {
        const Sources::const_iterator found = sources.find(name);
        if (found == sources.end()) { error.message = "missing module: " + name; return false; }
        source.source = found->second;
        source.sourceName = name + ".nc";
        return true;
    };
}

void ExpectInt(const Sources& sources, const std::string& root, std::int64_t expected, const std::string& label, bool debug)
{
    NeoCore::ModuleBundle bundle;
    NeoCore::Error error;
    const bool bundled = NeoCore::BundleModules(root, "root.nc", Loader(sources), bundle, error);
    Check(bundled, label + " bundle: " + error.message);
    if (!bundled) return;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(bundle.source, error, "bundle.nc", debug);
    if (!program) bundle.Translate(error);
    Check(program != nullptr, label + " checked compile: " + error.message);
    if (!program) return;
    const NeoCore::Result result = program->Call("Probe");
    Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Int && result.value.asInt() == expected,
        label + " value: " + result.error.message);
}

void Reject(const Sources& sources, const std::string& root, const std::string& message, const std::string& label)
{
    NeoCore::ModuleBundle bundle;
    NeoCore::Error error;
    Check(!NeoCore::BundleModules(root, "root.nc", Loader(sources), bundle, error), label + " rejected");
    Check(error.message.find(message) != std::string::npos, label + " diagnostic: " + error.message);
    Check(bundle.source.empty() && bundle.lines.empty(), label + " no partial bundle");
}

void CheckExecution(bool debug)
{
    Sources modules;
    modules["calc"] = "int secret=2;\npublic const int OFFSET=3;\npublic int value=5;\n"
        "public int Add(int n){return n+secret+OFFSET;}\npublic int Twice(int n){return Later(n);}\n"
        "int Later(int n){return n*2;}\nexport int HostOnly(){return 99;}\n";
    ExpectInt(modules, "import calc;\nexport int Probe(){calc.value+=1;return calc.Add(4)+calc.Twice(calc.value);}",
        21, "public functions variables constants and forward calls", debug);
    ExpectInt(modules, "import calc as c;\nexport int Probe(){return c.Add(c.OFFSET);}",
        8, "explicit import alias", debug);
    ExpectInt(modules, "int __ncmodule_1_secret=50;import calc;export int Probe(){return calc.Add(4)+__ncmodule_1_secret;}",
        59, "generated identifier collision avoided", debug);

    modules["state"] = "public int value=1;value+=1;";
    modules["worker"] = "import state;state.value+=7;public int Read(){return state.value;}";
    ExpectInt(modules, "import state;state.value=3;import worker;export int Probe(){return worker.Read();}",
        10, "module initialization occurs at first import position", debug);
    modules["left"] = "import state as s;public int Read(){return s.value;}";
    modules["right"] = "import state as t;public int Read(){return t.value;}";
    ExpectInt(modules, "import left;import right;export int Probe(){return left.Read()*100+right.Read();}",
        202, "diamond imports initialize dependency once", debug);
    ExpectInt(modules, "import state as first;first.value=8;import state as second;export int Probe(){return second.value;}",
        8, "two aliases share module globals", debug);

    modules["fields"] = "struct S{int counter;};int counter=3;public int Read(int counter){S s=S(counter);return s.counter;}";
    ExpectInt(modules, "import fields;export int Probe(){return fields.Read(17);}",
        17, "local shadowing and struct field names", debug);
    modules["text"] = "// import missing; text.fake\npublic string Read(){return \"text.member // import missing;\";}\n";
    ExpectInt(modules, "import text;export int Probe(){return text.Read().len();}",
        30, "strings and comments are unchanged", debug);
    modules["numeric"] = "public float Read(){return 1.25e2+0x10+0.5;}";
    ExpectInt(modules, "import numeric;export int Probe(){return toint(numeric.Read());}",
        141, "exponents hexadecimal literals and decimals", debug);
    modules["trig"] = "import math;public int Read(){return toint(math.cos(0));}";
    ExpectInt(modules, "import math;import trig;export int Probe(){return trig.Read()+toint(math.cos(0));}",
        2, "builtins imported independently by each file", debug);

    modules["shape"] = "public struct Target{int id;float x;};struct Hidden{int v;};"
        "public Target Make(int id,float x){return Target(id,x);}public list<Target> targets=[];public Hidden MakeHidden(){return Hidden(1);}";
    ExpectInt(modules, "import shape;shape.Target t=shape.Make(1,2.5);list<shape.Target> items=[t];items.append(shape.Target(4,0.5));"
        "shape.targets.append(items[1]);map<string,shape.Target> named={\"a\":t};int Sum(shape.Target v){return v.id;}"
        "export int Probe(){return Sum(items[0])*1000+items[1].id*100+shape.targets[0].id*10+named[\"a\"].id+toint(t.x);}",
        1443, "public struct types across modules", debug);
    modules["holder"] = "import shape;public shape.Target current=shape.Make(7,1.0);public list<shape.Target> all=[current];"
        "public int Id(shape.Target v){return v.id;}public fn(shape.Target)->int picker=fn(shape.Target v)->int{return v.id*2;};";
    ExpectInt(modules, "import shape;import holder;export int Probe(){shape.Target c=holder.current;return holder.Id(c)*100+holder.all[0].id*10+holder.picker(c)/2;}",
        777, "qualified struct types declared inside another module", debug);
    ExpectInt(modules, "import shape;export int Probe(){return shape.MakeHidden().v;}",
        1, "private struct values remain usable without naming the type", debug);
    modules["defaults"] = "public const int LIMIT=7;public struct Cfg{int limit=LIMIT;string tag=\"m\";};public Cfg Make(){Cfg c;return c;}";
    ExpectInt(modules, "import defaults;export int Probe(){defaults.Cfg c;defaults.Cfg m=defaults.Make();return c.limit*100+m.limit*10+m.tag.len();}",
        771, "struct field defaults referencing a module constant", debug);

    NeoCore::ModuleBundle bundle;
    NeoCore::Error error;
    Check(NeoCore::BundleModules("import calc;export int Probe(){return 1;}", "root.nc", Loader(modules), bundle, error),
        "module export bundle");
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(bundle.source, error, "bundle.nc", debug);
    Check(program != nullptr, "module export typed compile");
    if (program) Check(!program->Call("__ncmodule_1_HostOnly").ok(), "imported export does not expose a host function");
}

void CheckRejections()
{
    Sources modules;
    modules["a"] = "int hidden=1;export int HostOnly(){return 2;}public int Read(){return hidden;}";
    Reject(modules, "import a;int value=a.hidden;", "not public", "private variable inaccessible");
    Reject(modules, "import a;int value=a.HostOnly();", "not public", "export is not public");
    Reject(modules, "import a;int value=a.Absent();", "no member", "missing member");
    Reject(modules, "import absent;", "missing module", "loader failure");
    Reject(modules, "int value=a.Read();import a;", "before use", "module alias declaration order");
    Reject(modules, "import a;int F(int a){return a;}", "cannot be hidden", "module aliases cannot be shadowed");
    Reject(modules, "import math as m;", "built-in module aliases", "builtin alias unsupported explicitly");
    Reject(modules, "import a as return;", "reserved", "reserved words cannot become aliases");
    Reject(modules, "import a as system;", "conflicts", "module aliases cannot replace builtin modules");
    Reject(modules, "import a as Vector3;", "conflicts", "module aliases cannot replace builtin types");
    modules["a"] = "import a;public int Read(){return 1;}";
    Reject(modules, "import a;", "cyclic", "self import");
    modules["a"] = "import b;public int Read(){return 1;}";
    modules["b"] = "import a;public int Read(){return 2;}";
    Reject(modules, "import a;", "cyclic", "indirect cycle");
    modules["a"] = "public int Read(){return toint(math.cos(0));}";
    Reject(modules, "import math;import a;", "must be imported", "builtin imports do not leak between modules");
    modules["a"] = "export const int value=1;";
    Reject(modules, "import a;", "export const", "export stripping cannot accept invalid const declaration");
    Reject(modules, "if(true) import a;", "file scope", "unbraced conditional import is rejected");
    modules["a"] = "public int Read(){return 1;";
    Reject(modules, "import a;}", "delimiter", "module delimiters cannot close in importer");
    modules["a"] = "public int value=1";
    Reject(modules, "import a;+2;", "incomplete statement", "module expressions cannot continue in importer");
    modules["a"] = "if(true) export int hidden=1;public int Read(){return 1;}";
    Reject(modules, "import a;", "file scope", "export removal cannot accept a nested declaration");

    NeoCore::ModuleBundle bundle;
    NeoCore::Error error;
    Check(!NeoCore::BundleModules("import a;", "root.nc", NeoCore::ModuleLoader(), bundle, error), "loader must be explicit");
    Check(error.message.find("ModuleLoader") != std::string::npos, "missing loader diagnostic");
    Check(NeoCore::BundleModules("import coroutine;coroutine<fn(int)->void> task;", "root.nc", NeoCore::ModuleLoader(), bundle, error),
        "builtin coroutine type is not an alias value");

    modules["isolated"] = "public int Read(){return rootValue;}";
    Check(NeoCore::BundleModules("int rootValue=99;import isolated;export int Probe(){return isolated.Read();}",
        "root.nc", Loader(modules), bundle, error), "undeclared module name bundle");
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(bundle.source, error, "bundle.nc");
    Check(!program, "undeclared module name cannot capture root globals");
    bundle.Translate(error);
    Check(error.sourceName == "isolated.nc" && error.line == 1, "isolated module error source location");
    Sources shapes;
    shapes["shape"] = "public struct Target{int id;};struct Hidden{int v;};public Hidden MakeHidden(){return Hidden(1);}";
    Reject(shapes, "import shape;shape.Hidden h=shape.MakeHidden();", "module member is not public: Hidden", "private struct type name");
    Reject(shapes, "import shape;list<shape.Hidden> items=[];", "module member is not public: Hidden", "private struct type in a generic argument");
}

void CheckLocations()
{
    Sources modules;
    modules["bad"] = "// heading\npublic int Bad(){\n    return \"wrong\";\n}\n";
    NeoCore::ModuleBundle bundle;
    NeoCore::Error error;
    Check(NeoCore::BundleModules("int before=1;\nimport bad;\nexport int Probe(){return bad.Bad();}", "root.nc", Loader(modules), bundle, error),
        "invalid typed module reaches checker");
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(bundle.source, error, "bundle.nc", true);
    Check(!program, "module return type is checked");
    bundle.Translate(error);
    Check(error.sourceName == "bad.nc" && error.line == 3 && error.column == 12,
        "type error original file line and column: " + error.sourceName + ":" + std::to_string(error.line) + ":" + std::to_string(error.column));

    modules["bad"] = "public int Crash(){\n    int zero=0;\n    return 1/zero;\n}\n";
    Check(NeoCore::BundleModules("import bad;\nexport int Probe(){return bad.Crash();}", "root.nc", Loader(modules), bundle, error), "runtime error bundle");
    program = NeoCore::Program::Compile(bundle.source, error, "bundle.nc", true);
    Check(program != nullptr, "runtime error typed compile");
    if (program)
    {
        NeoCore::Result result = program->Call("Probe");
        Check(!result.ok(), "module runtime division error");
        bundle.Translate(result.error);
        Check(result.error.sourceName == "bad.nc" && result.error.line == 3, "runtime error original module line");
    }

    modules["external"] = "public int Read(){return hostValue;}";
    std::vector<std::string> externalNames(1, "hostValue");
    Check(NeoCore::BundleModules("import external;", "root.nc", Loader(modules), bundle, error, externalNames), "explicit external names accepted");
    Check(bundle.source.find("return hostValue;") != std::string::npos, "host identifier remains unchanged");
    Check(!NeoCore::BundleModules("import external as hostValue;", "root.nc", Loader(modules), bundle, error, externalNames),
        "module alias cannot hide a host global");
}

}

static void CheckNativeModuleNames()
{
    NeoCore::HostSchema schema;
    const NeoCore::TypeId globalType = schema.AddObject("GlobalOnly");
    const NeoCore::TypeId argumentType = schema.AddObject("ArgumentOnly");
    const NeoCore::TypeId returnType = schema.AddObject("ReturnOnly");
    schema.AddObject("UnusedNative");
    const NeoCore::TypeId ownerType = schema.AddObject("ServiceOnly");
    const NeoCore::HostBindingId method = schema.AddMethod(ownerType, "Make", returnType, {argumentType});
    Check(method != NeoCore::InvalidHostBinding && schema.AddGlobal("hostGlobal", globalType), "native module schema");
    int nativeObject = 1;
    NeoCore::HostBindings bindings(schema, [](void* context, NeoCore::TypeId, NeoCore::ObjectHandle object) -> void* {
        return object.value == 1 ? context : nullptr;
    }, &nativeObject);
    Check(bindings.Bind(method, [](NeoCore::HostCall& call) {
        call.ReturnObject(NeoCore::ObjectHandle(1));
        return true;
    }) && bindings.BindGlobal("hostGlobal", NeoCore::ObjectHandle(1)), "native module bindings");
    Sources sources;
    sources["native"] = "public int Read(){GlobalOnly copy=hostGlobal;ArgumentOnly argument=null;"
        "ReturnOnly result=null;UnusedNative unused=null;"
        "if(copy==hostGlobal&&argument==null&&result==null&&unused==null)return 1;return 0;}";
    NeoCore::CompileOptions options;
    options.moduleLoader = Loader(sources);
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(
        "import native;export int Probe(){return native.Read();}", bindings, error, "root.nc", true, options);
    Check(program != nullptr, "Program::Compile preserves native names without members: " + error.message);
    if (program)
    {
        const NeoCore::Result result = program->Call("Probe");
        Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Int && result.value.asInt() == 1,
            "module native global argument return and unused types");
    }
}

void CheckConsoleAccess(bool debug)
{
    Sources sources;
    sources["private"] = "int hidden=3;const int LIMIT=4;struct Hidden{int value;};"
        "int Read(){return hidden;}public int PublicRead(){return hidden;}";
    NeoCore::CompileOptions options;
    options.moduleLoader = Loader(sources);
    NeoCore::Error error;
    const char* entry = "import private as p;export int Probe(){p.Hidden h=p.Hidden(p.LIMIT);p.hidden+=1;return p.Read()+h.value;}";
    auto allowed = NeoCore::ConsoleCompileAccess::Compile(entry, nullptr, error, "entry.nc", debug, options, "entry.nc");
    Check(bool(allowed), "console internal entry accepts private symbols: " + error.message);
    if (allowed)
    {
        const auto result = allowed->Call("Probe");
        Check(result.ok() && result.value.asInt() == 8, "console entry private state and type identity");
    }
    for (const std::string& name : {std::string("entry.nc"), std::string()})
    {
        auto regular = NeoCore::Program::Compile(entry, error, name, debug, options);
        Check(!regular && error.message.find("not public") != std::string::npos,
            "Program::Compile retains normal visibility after a console compile");
        auto runtime = NeoCore::Runtime::Create(error);
        Check(bool(runtime), "normal embedding runtime");
        if (!runtime) continue;
        regular = runtime->Compile(entry, error, name, debug, options);
        Check(!regular && error.message.find("not public") != std::string::npos,
            "Runtime::Compile cannot enable console private access");
        regular = runtime->Compile("import private;export int Probe(){return private.PublicRead();}", error, name, debug, options);
        Check(bool(regular), "public access remains valid after private access rejection");
    }
    sources["intruder"] = "import private;public int Leak(){return private.hidden;}";
    allowed = NeoCore::ConsoleCompileAccess::Compile("import intruder;print(intruder.Leak());", nullptr,
        error, "entry.nc", debug, options, "entry.nc");
    Check(!allowed && error.sourceName == "intruder.nc" && error.message.find("not public") != std::string::npos,
        "console entry does not grant imported modules private access");
    allowed = NeoCore::ConsoleCompileAccess::Compile("print(1);", nullptr, error, "entry.nc", debug, options, "");
    Check(!allowed && error.message.find("requires an entry source name") != std::string::npos,
        "console exception requires an explicit entry identity");
}

int main()
{
    CheckExecution(false);
    CheckExecution(true);
    CheckRejections();
    CheckLocations();
    CheckNativeModuleNames();
    CheckConsoleAccess(false);
    CheckConsoleAccess(true);
    std::printf("NeoCore module regression: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
