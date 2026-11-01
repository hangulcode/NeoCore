#include "NeoCoreDebugger.h"
#include "NeoCoreCompiler.h"
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace
{
int checks = 0, failures = 0;
void Check(bool condition, const std::string& message)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message.c_str()); }
}
struct Listener : NeoCore::IDebugListener
{
    NeoCore::DebugLocation location;
    void OnStopped(NeoCore::Instance&, const NeoCore::DebugLocation& at, NeoCore::DebugStopReason) override
    { location = at; }
};

void RuntimeErrors(bool debug)
{
    using namespace NeoCore;
    Error error;
    auto runtime = Runtime::Create(error);
    auto program = runtime->Compile(
        "map<string,int> numbers={\"ok\":3};\n"
        "map<string,string> texts={\"ok\":\"v\"};\n"
        "string key=null;\n"
        "export void Insert(){\n"
        "numbers[key]=7;\n"
        "}\n"
        "export void Remove(){\n"
        "texts[key]=null;\n"
        "}\n"
        "export void Literal(){\n"
        "map<string,int> invalid={key:7};\n"
        "}\n"
        "export int Missing(){\n"
        "return numbers[\"absent\"];\n"
        "}\n"
        "export int Other(){\n"
        "return numbers[\"other\"];\n"
        "}\n"
        "export int Lambda(){\n"
        "fn()->int f=fn()->int{\n"
        "return numbers[\"lambda\"];\n"
        "};\n"
        "return f();\n"
        "}\n"
        "export int LambdaDivide(){\n"
        "fn(int)->int f=fn(int n)->int{\n"
        "return 1/n;\n"
        "};\n"
        "return f(0);\n"
        "}\n"
        "export void Resize(){\n"
        "list<int> items=null;\n"
        "items.resize(3,7);\n"
        "}\n"
        "export int Count(){return numbers.len()*10+texts.len();}\n"
        "import system;\n"
        "struct Data{map<string,float> values;};\n"
        "export string Encode(float value){return system.jsonEncode(Data({\"bad\":value}));}\n",
        error, "error-locations.nc", debug);
    Check(bool(program), "compile error cases: " + error.message);
    if (!program) return;
    for (int image = 0; image != 2; ++image)
    {
        auto instance = program->CreateInstance(error);
        Check(bool(instance), "create error test instance: " + error.message);
        if (!instance) return;
        Debugger* debugger = runtime->GetDebugger();
        Listener listener;
        if (debug) { debugger->SetListener(&listener); Check(debugger->Attach(*instance, error), "attach error debugger"); }
        const struct Case { const char* name; const char* message; int line; } cases[] = {
            {"Insert", "map key cannot be null", 5},
            {"Remove", "map key cannot be null", 8},
            {"Literal", "map key cannot be null", 11},
            {"Missing", "map key not found", 14},
            {"Other", "map key not found", 17},
            {"Lambda", "map key not found", 21},
            {"LambdaDivide", "division by zero", 27},
            {"Resize", "", 33}
        };
        for (const auto& test : cases)
        {
            const Result result = instance->Call(test.name);
            Check(!result.ok() && result.error.message.find(test.message) != std::string::npos,
                std::string(test.name) + " must fail: " + result.error.message);
            if (debug)
            {
                Check(result.error.sourceName == "error-locations.nc" && result.error.line == test.line,
                    std::string(test.name) + " user error location: " + result.error.message);
                Check(result.error.message.find("Line(" + std::to_string(test.line) + ")") != std::string::npos,
                    std::string(test.name) + " error header line: " + result.error.message);
                Check(listener.location.line == test.line && listener.location.sourceName == "error-locations.nc",
                    std::string(test.name) + " exception stop location");
                std::vector<DebugStackFrame> frames;
                Check(debugger->GetStackTrace(*instance, frames) && !frames.empty() && frames.front().location.line == test.line,
                    std::string(test.name) + " top user frame");
                Check(result.error.message.find("$neocore_mapread") == std::string::npos,
                    "common helper does not leak into the user stack");
            }
        }
        const Result count = instance->Call("Count");
        Check(count.ok() && count.value.asInt() == 11, "invalid key writes leave existing maps intact");
        const Result json = instance->Call("Encode", {Scalar::Float((std::numeric_limits<double>::infinity)())});
        Check(!json.ok() && json.error.message.find("$.values.bad:") != std::string::npos,
            "nested JSON encode errors include their path: " + json.error.message);
        if (debug) { debugger->Detach(*instance); debugger->SetListener(nullptr); }
        instance.reset();
        if (image == 0)
        {
            std::vector<std::uint8_t> bytes;
            Check(program->SaveImage(bytes, error), "save helper error metadata: " + error.message);
            program = runtime->LoadImage(bytes, error);
            Check(bool(program), "restore helper error metadata: " + error.message);
            if (!program) return;
        }
    }
}

void SetNulls(bool debug)
{
    using namespace NeoCore;
    Error error;
    auto program = Program::Compile(
        "import system;\n"
        "set<string> names=set<string>();\n"
        "string key=null;\n"
        "export void Add(){\n"
        "names.add(key);\n"
        "}\n"
        "export void Constructor(){\n"
        "set<string> invalid=set<string>([\"x\",key]);\n"
        "}\n"
        "export void SystemConstructor(){\n"
        "list<string> initial=[key];set<string> invalid=system.set(initial);\n"
        "}\n"
        "export void Literal(){\n"
        "names.add(null);\n"
        "}\n"
        "export int Count(){return names.len();}\n"
        "export set<string> Decode(string text){return system.jsonDecode<set<string>>(text);}\n",
        error, "set-null.nc", debug);
    Check(bool(program), "compile nullable set inputs: " + error.message);
    if (!program) return;
    for (int image = 0; image != 2; ++image)
    {
        const struct Case { const char* name; int line; } cases[] = {
            {"Add", 5}, {"Constructor", 8}, {"SystemConstructor", 11}, {"Literal", 14}
        };
        for (const auto& test : cases)
        {
            const Result result = program->Call(test.name);
            Check(!result.ok() && result.error.message.find("set element cannot be null") != std::string::npos,
                std::string(test.name) + " rejects null set elements: " + result.error.message);
            if (debug) Check(result.error.line == test.line, std::string(test.name) + " retains insertion line");
        }
        const Result count = program->Call("Count");
        Check(count.ok() && count.value.asInt() == 0, "failed set insertion leaves the set unchanged");
        const Result decoded = program->Call("Decode", {Scalar::String("[\"x\",null]")});
        Check(!decoded.ok() && decoded.error.message.find("$[1]: set element cannot be null") != std::string::npos,
            "script JSON set decoding reports null element path");
        if (image == 0)
        {
            std::vector<std::uint8_t> bytes;
            Check(program->SaveImage(bytes, error), "save set null checks");
            program = Program::LoadImage(bytes, error);
            Check(bool(program), "load set null checks: " + error.message);
            if (!program) return;
        }
    }
}

void ModuleErrors()
{
    using namespace NeoCore;
    CompileOptions options;
    options.moduleLoader = [](const std::string& name, const std::string&, ModuleSource& out, Error&) {
        if (name != "data") return false;
        out.sourceName = "data.nc";
        out.source = "public int Read(){\nmap<string,int> values={};\nreturn values[\"missing\"];\n}\n";
        return true;
    };
    Error error;
    auto program = Program::Compile("import data;\nexport int Probe(){return data.Read();}\n",
        error, "main.nc", true, options);
    Check(bool(program), "compile imported helper caller: " + error.message);
    if (!program) return;
    for (int image = 0; image != 2; ++image)
    {
        const Result result = program->Call("Probe");
        Check(!result.ok() && result.error.sourceName == "data.nc" && result.error.line == 3,
            "imported user function retains its own callsite: " + result.error.message);
        const std::size_t stack = result.error.message.find("\nScript Call Stack:");
        Check(result.error.message.substr(0, stack).find("Line(3)") != std::string::npos,
            "imported helper error text agrees with the structured location: " + result.error.message);
        if (image == 0)
        {
            std::vector<std::uint8_t> bytes;
            Check(program->SaveImage(bytes, error), "save module helper metadata");
            program = Program::LoadImage(bytes, error);
            Check(bool(program), "load module helper metadata: " + error.message);
            if (!program) return;
        }
    }
}

void ConstantDiagnostics()
{
    using namespace NeoCore;
    const struct Case { const char* source; const char* reason; int line; } cases[] = {
        {"int initial=7;\nstruct Data{\nint value=initial;\n};", "non-constant variable 'initial'", 3},
        {"int initial=7;\nstruct Data{\nint value=1+initial;\n};", "non-constant variable 'initial'", 3},
        {"float initial=7;\nstruct Data{\nVector3 value=Vector3(initial,0,0);\n};", "non-constant variable 'initial'", 3},
        {"int Initial(){return 7;}\nstruct Data{\nint value=Initial();\n};", "cannot call this function", 3}
    };
    for (const auto& test : cases)
    {
        StaticCompileResult result;
        Check(!CompileStaticSource(test.source, result) && result.errorLine == test.line &&
            result.errorMessage.find(test.reason) != std::string::npos,
            "field default diagnostic explains its cause: " + result.errorMessage);
    }
}
}

int main()
{
    RuntimeErrors(false); RuntimeErrors(true); SetNulls(false); SetNulls(true); ModuleErrors(); ConstantDiagnostics();
    std::printf("NeoCore error locations: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
