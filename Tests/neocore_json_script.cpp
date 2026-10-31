#include "NeoCore.h"
#include "NeoCoreDebugger.h"
#include "NeoCoreHost.h"

#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace NeoCore;
int checks = 0;

void Check(bool condition, const std::string& label)
{
    ++checks;
    if (!condition) throw std::runtime_error(label);
}

void Completed(const Result& result, const char* label)
{
    Check(result.ok() && result.status == ExecutionStatus::Completed,
        std::string(label) + ": " + result.error.message);
}

void Integer(const Result& result, std::int64_t expected, const char* label)
{
    Completed(result, label);
    Check(result.value.kind() == Scalar::Kind::Int && result.value.asInt() == expected, label);
}

void Boolean(const Result& result, const char* label)
{
    Completed(result, label);
    Check(result.value.kind() == Scalar::Kind::Bool && result.value.asBool(), label);
}

const char* RoundTripSource = R"NC(import system;
struct Header { int id; string label; };
struct Payload {
    Header meta;
    list<int> scores;
    string missing;
    array32<float> weights;
    Vector3 point;
    Quaternion rotation;
    map<string, list<int>> groups;
    set<int> tags;
};
string fixture = "{\"meta\":{\"id\":9007199254740993,\"label\":\"roundtrip\"},\"scores\":[1,2,3],\"missing\":null,\"weights\":[1.25,2.5],\"point\":[1,2,3],\"rotation\":[1,0,0,0],\"groups\":{\"a\":[4,5]},\"tags\":[7,7,8]}";
list<int> state = system.jsonDecode<list<int>>("[1]");
export int Step() { state[0] += 1; return state[0]; }
export string Encode() {
    Payload value = system.jsonDecode<Payload>(fixture);
    return system.jsonEncode(value, true);
}
export bool RoundTrip() {
    Payload value = system.jsonDecode<Payload>(Encode());
    list<int> group = value.groups.get("a", []);
    return value.meta.id == 9007199254740993 && value.meta.label == "roundtrip"
        && value.scores[1] == 2 && value.missing == null && value.weights[0] == 1.25
        && value.point.z == 3.0 && value.rotation.w == 1.0
        && group[1] == 5 && value.tags.len() == 2 && value.tags.contains(8);
}
export int Nested() {
    map<string, list<array<int>>> data = system.jsonDecode<map<string, list<array<int>>>>("{\"values\":[[1,2],[3]]}");
    list<array<int>> values = data.get("values", []);
    return values[0][1] + values[1][0];
}
export bool Storage() {
    array<bool> flags = system.jsonDecode<array<bool>>("[true,false]");
    array<float> numbers = system.jsonDecode<array<float>>("[1,2.5]");
    array32<int> compact = system.jsonDecode<array32<int>>("[-2147483648,2147483647]");
    Vector2 v2 = system.jsonDecode<Vector2>("[2,3]");
    Vector4 v4 = system.jsonDecode<Vector4>("[1,2,3,4]");
    string missing = system.jsonDecode<string>("null");
    string present = system.jsonDecode<string>("\"7\"");
    list<int> absent = system.jsonDecode<list<int>>("null");
    return flags[0] && !flags[1] && numbers[1] == 2.5
        && compact[0] == -2147483648 && compact[1] == 2147483647
        && v2.y == 3.0 && v4.w == 4.0 && missing == null && present == "7" && absent == null
        && system.jsonEncode(missing) == "null";
}
export string Sorted() {
    map<string, int> values = {"z":2, "a":1};
    return system.jsonEncode(values, true);
}
)NC";

void RoundTrips(bool debug)
{
    Error error;
    auto program = Program::Compile(RoundTripSource, error, "json-roundtrip.nc", debug);
    Check(program && error.ok(), "compile JSON roundtrip: " + error.message);
    Boolean(program->Call("RoundTrip"), "struct JSON roundtrip");
    Integer(program->Call("Nested"), 5, "nested generic decode target");
    Boolean(program->Call("Storage"), "null reference, compact, vector and typed array decoding");
    const Result encoded = program->Call("Encode");
    Completed(encoded, "encoded struct");
    Check(encoded.value.asString().find("\"meta\":{\"id\":9007199254740993,\"label\":\"roundtrip\"}") != std::string::npos,
        "structs encode named fields instead of positional backend lists");
    Check(encoded.value.asString().find("\"missing\":null") != std::string::npos,
        "null reference field remains present during encoding");
    const Result sorted = program->Call("Sorted");
    Completed(sorted, "sorted object");
    Check(sorted.value.asString() == "{\"a\":1,\"z\":2}", "sortKeys orders object keys");
    auto one = program->CreateInstance(error);
    auto two = program->CreateInstance(error);
    Check(one && two && error.ok(), "JSON instances");
    Integer(one->Call("Step"), 2, "first instance decoded global");
    Integer(two->Call("Step"), 2, "second instance has independent decoded storage");
    Integer(one->Call("Step"), 3, "first instance retains own decoded state");
    std::vector<std::uint8_t> image;
    Check(program->SaveImage(image, error), "save JSON image: " + error.message);
    program.reset();
    auto loaded = Program::LoadImage(image, error);
    Check(loaded && error.ok(), "load JSON image: " + error.message);
    Boolean(loaded->Call("RoundTrip"), "image restores JSON type contracts");
    Integer(loaded->Call("Nested"), 5, "image restores nested generic decode contract");
}

void ProgramIsolation(bool debug)
{
    Error error;
    auto runtime = Runtime::Create(error);
    Check(runtime && error.ok(), "JSON shared runtime");
    auto first = runtime->Compile(
        "import system; struct Data{int value;}; export int Read(){Data d=system.jsonDecode<Data>(\"{\\\"value\\\":7}\");return d.value;}",
        error, "json-first.nc", debug);
    Check(first && error.ok(), "first JSON registry: " + error.message);
    auto second = runtime->Compile(
        "import system; struct Padding{bool ok;}; struct Data{string label;}; export string Read(){Data d=system.jsonDecode<Data>(\"{\\\"label\\\":\\\"other\\\"}\");return d.label;}",
        error, "json-second.nc", debug);
    Check(second && error.ok(), "second JSON registry: " + error.message);
    Integer(first->Call("Read"), 7, "first program selects its own JSON target");
    Result text = second->Call("Read");
    Completed(text, "second program JSON target");
    Check(text.value.asString() == "other", "same struct name uses destination program layout");
    Integer(first->Call("Read"), 7, "interleaved program does not replace JSON registry");
}

void ImportedTypes(bool debug)
{
    CompileOptions options;
    options.moduleLoader = [](const std::string& name, const std::string&, ModuleSource& source, Error&) {
        if (name != "data") return false;
        source.sourceName = "json-data.nc";
        source.source =
            "import system;\n"
            "struct Record{int value;};\n"
            "public string Encode(){\n"
            "    list<Record> values=system.jsonDecode<list<Record>>(\"[{\\\"value\\\":17}]\");\n"
            "    return system.jsonEncode(values[0],true);\n"
            "}\n";
        return true;
    };
    Error error;
    auto program = Program::Compile("import data;export string Read(){return data.Encode();}",
        error, "json-import.nc", debug, options);
    Check(program && error.ok(), "module-local JSON target: " + error.message);
    const Result result = program->Call("Read");
    Completed(result, "module JSON roundtrip");
    Check(result.value.asString() == "{\"value\":17}", "module rewriting preserves struct JSON field names");
}

void Failures(bool debug)
{
    Error error;
    auto runtime = Runtime::Create(error);
    Check(runtime && error.ok(), "JSON failure runtime");
    const char* source =
        "import system;\n"
        "export int completed=0;\n"
        "int Inner(string text)\n"
        "{\n"
        "    list<int> values=system.jsonDecode<list<int>>(text);\n"
        "    completed+=1;\n"
        "    return values[0];\n"
        "}\n"
        "export int Outer(string text){return Inner(text);}\n"
        "export string Encode(float value){return system.jsonEncode(value);}\n"
        "struct Exact{int n;};\n"
        "export int Shape(string text){Exact value=system.jsonDecode<Exact>(text);return value.n;}\n"
        "export int Compact(string text){array32<int> value=system.jsonDecode<array32<int>>(text);return value[0];}\n"
        "export float Vector(string text){Vector3 value=system.jsonDecode<Vector3>(text);return value.x;}\n"
        "struct Optional{string n;};\n"
        "struct NestedOptional{Optional child;int version;};\n"
        "export string OptionalData(string text){NestedOptional value=system.jsonDecode<NestedOptional>(text);return system.jsonEncode(value,true);}\n";
    auto program = runtime->Compile(source, error, "json-failures.nc", debug);
    Check(program && error.ok(), "compile JSON failures: " + error.message);
    auto instance = program->CreateInstance(error);
    Check(instance && error.ok(), "JSON failure instance");
    Debugger* debugger = runtime->GetDebugger();
    if (debug) Check(debugger->Attach(*instance, error), "attach JSON error debugger");
    for (const char* invalid : {"{", "[1,\"bad\"]", "[9223372036854775808]", "[1.5]"})
    {
        const Result failed = instance->Call("Outer", {Scalar::String(invalid)});
        Check(!failed.ok() && !failed.error.message.empty(), std::string("reject invalid JSON: ") + invalid);
        if (debug)
        {
            Check(failed.error.sourceName == "json-failures.nc" && failed.error.line == 5,
                "JSON error retains source call line: " + failed.error.sourceName + ":" + std::to_string(failed.error.line));
            std::vector<DebugStackFrame> frames;
            Check(debugger->GetStackTrace(*instance, frames), "JSON error retains stack snapshot");
            bool inner = false, outer = false;
            for (const DebugStackFrame& frame : frames)
            {
                inner = inner || frame.functionName == "Inner";
                outer = outer || frame.functionName == "Outer";
            }
            Check(inner && outer, "JSON error stack retains both script callers");
        }
        Integer(instance->ReadGlobal("completed"), 0, "failed decode cannot execute following statement");
    }
    Check(!instance->Call("Outer", {Scalar::String("null")}).ok(), "a decoded null list fails on its first use");
    Integer(instance->Call("Outer", {Scalar::String("[9]")}), 9, "JSON failure leaves fresh calls usable");
    for (const char* invalid : {"{}", "{\"n\":1,\"extra\":2}", "{\"n\":false}", "[7]"})
        Check(!instance->Call("Shape", {Scalar::String(invalid)}).ok(), "struct shape is checked");
    Check(!instance->Call("Compact", {Scalar::String("[2147483648]")}).ok(), "compact integer range is checked");
    Check(!instance->Call("Vector", {Scalar::String("[1e100,0,0]")}).ok(), "vector float32 range is checked");
    Check(!instance->Call("Encode", {Scalar::Float(std::numeric_limits<double>::infinity())}).ok(), "nonfinite JSON output is rejected");
    for (const char* text : {"{\"child\":{\"n\":null},\"version\":1}", "{\"child\":{\"n\":\"x\",\"n\":null},\"version\":1}"})
    {
        const Result optional = instance->Call("OptionalData", {Scalar::String(text)});
        Completed(optional, "nested null reference field roundtrip");
        Check(optional.value.asString() == "{\"child\":{\"n\":null},\"version\":1}",
            "explicit null is preserved, including a final duplicate key");
    }
    for (const char* text : {"{\"child\":{},\"version\":1}", "{\"child\":{\"n\":null}}", "{\"child\":null,\"version\":1}"})
        Check(!instance->Call("OptionalData", {Scalar::String(text)}).ok(), "nested fields cannot be omitted or defaulted");
}

void RejectedTypes()
{
    const char* invalid[] = {
        "int x=system.jsonDecode<int>(\"1\");",
        "import system; int x=system.jsonDecode(\"1\");",
        "import system; int x=system.jsonDecode<int>(1);",
        "import system; int x=system.jsonDecode<int><float>(\"1\");",
        "import system; string x=system.jsonEncode(1,1);",
        "import system; string x=system.jsonEncode();",
        "import system; map<int,int> x=system.jsonDecode<map<int,int>>(\"{}\");",
        "import system; map<int,int> x={1:2}; string s=system.jsonEncode(x);",
        "import system; list<fn()->int> x=[]; string s=system.jsonEncode(x);",
        "import system; fn()->int x=system.jsonDecode<fn()->int>(\"null\");",
        "import system; struct Bad{list<map<int,int>> values;}; string s=system.jsonEncode(Bad([]));",
        "import system; Stopwatch x=system.jsonDecode<Stopwatch>(\"{}\");",
        "import system; string s=system.jsonEncode(system.stopwatch());",
        "import system; import coroutine; void F(){} coroutine<fn()->void> c=coroutine.create(F); string s=system.jsonEncode(c);",
        "import system; string s=system.jsonDecode<void>(\"null\");"
    };
    for (const char* source : invalid)
    {
        Error error;
        auto program = Program::Compile(source, error, "json-rejected.nc");
        Check(!program && !error.ok() && error.line > 0, std::string("reject unsupported JSON contract: ") + source);
    }
    HostSchema schema;
    const TypeId object = schema.AddObject("Entity");
    Check(schema.AddGlobal("Object", object), "JSON rejected native schema");
    HostBindings bindings(schema, [](void*, TypeId, ObjectHandle) -> void* { return nullptr; }, nullptr);
    Check(bindings.BindGlobal("Object", ObjectHandle(1)), "JSON rejected native binding");
    Error error;
    auto program = Program::Compile("import system; string s=system.jsonEncode(Object);", bindings, error, "json-native.nc");
    Check(!program && !error.ok(), "native IDs are not JSON data");
}

void HostDecode(bool debug)
{
    HostSchema schema;
    const TypeId host = schema.AddObject("Resource");
    const TypeId record = schema.AddStruct("Document");
    Check(schema.DefineStruct(record, {StructField("id", schema.Types().Int()),
        StructField("values", schema.Types().ListOf(schema.Types().Float()))}), "JSON host record schema");
    const HostBindingId load = schema.AddMethod(host, "Load", record, {schema.Types().String()});
    Check(load != InvalidHostBinding && schema.AddGlobal("Resource", host), "JSON host method schema");
    TypeId hostType = host;
    HostBindings bindings(schema, [](void* data, TypeId type, ObjectHandle id) -> void* {
        return type == *static_cast<TypeId*>(data) && id.value == 1 ? data : nullptr;
    }, &hostType);
    Check(bindings.BindGlobal("Resource", ObjectHandle(1)), "JSON host global");
    Check(bindings.Bind(load, [](HostCall& call) { return call.ReturnJson(call.String(0)); }), "JSON host load callback");
    Error error;
    auto runtime = Runtime::Create(bindings, error);
    Check(runtime && error.ok(), "JSON host runtime");
    auto program = runtime->Compile(
        "Document initial=Resource.Load(\"{\\\"id\\\":3,\\\"values\\\":[1,2.5]}\");"
        "export int Initial(){return initial.id;}"
        "export float Load(string text){Document d=Resource.Load(text);return float(d.id)+d.values[1];}",
        error, "json-host.nc", debug);
    Check(program && error.ok(), "compile JSON host: " + error.message);
    Integer(program->Call("Initial"), 3, "host JSON during default-instance initialization");
    auto instance = program->CreateInstance(error);
    Check(instance && error.ok(), "host JSON separate instance");
    Integer(instance->Call("Initial"), 3, "host JSON during separate-instance initialization");
    const std::vector<Scalar> args = {Scalar::String("{\"id\":7,\"values\":[1,2.5]}")};
    Result value = program->Call("Load", args);
    Completed(value, "default-instance host JSON");
    Check(value.value.asFloat() == 9.5, "default-instance host JSON uses declared shape");
    value = instance->Call("Load", args);
    Completed(value, "separate-instance host JSON");
    Check(value.value.asFloat() == 9.5, "host JSON return belongs to the actual caller");
    Check(!instance->Call("Load", {Scalar::String("{\"id\":7}")}).ok(), "host JSON rejects missing fields");
    value = instance->Call("Load", args);
    Completed(value, "host JSON recovery");
}
}

int main()
{
    int failures = 0;
    try
    {
        RejectedTypes();
    }
    catch (const std::exception& error)
    {
        ++failures;
        std::fprintf(stderr, "NeoCore script JSON rejection: %s\n", error.what());
    }
    const struct Test { const char* name; void (*run)(bool); } tests[] = {
        {"roundtrip", &RoundTrips}, {"program isolation", &ProgramIsolation},
        {"module types", &ImportedTypes}, {"runtime errors", &Failures}, {"host decode", &HostDecode}
    };
    for (bool debug : {false, true})
        for (const Test& test : tests)
            try { test.run(debug); }
            catch (const std::exception& error)
            {
                ++failures;
                std::fprintf(stderr, "NeoCore script JSON %s (debug=%d): %s\n", test.name, debug ? 1 : 0, error.what());
            }
    std::printf("NeoCore script JSON: %d checks, %d failing groups\n", checks, failures);
    return failures ? 1 : 0;
}
