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
    list<int> group = value.groups["a"];
    return value.meta.id == 9007199254740993 && value.meta.label == "roundtrip"
        && value.scores[1] == 2 && value.missing == null && value.weights[0] == 1.25
        && value.point.z == 3.0 && value.rotation.w == 1.0
        && group[1] == 5 && value.tags.len() == 2 && value.tags.contains(8);
}
export int Nested() {
    map<string, list<array<int>>> data = system.jsonDecode<map<string, list<array<int>>>>("{\"values\":[[1,2],[3]]}");
    list<array<int>> values = data["values"];
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
export bool StringSets() {
    set<string> a = system.jsonDecode<set<string>>("[\"x\",\"x\",\"y\"]");
    set<string> b = system.jsonDecode<set<string>>(system.jsonEncode(a));
    set<string> c = set<string>(["x","y","x"]);
    set<string> d = b.clone();
    d.remove("y");
    set<string> joined = b | d;
    set<string> common = b & d;
    set<string> remaining = b - d;
    return a.len() == 2 && b.len() == 2 && !b.contains(null) && !b.remove(null)
        && c.len() == 2 && joined.len() == 2 && common.len() == 1 && remaining.len() == 1;
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
    Boolean(program->Call("StringSets"), "string set decoding, constructors, operations and encoding");
    Scalar stringSet = program->CreateValue("set<string>", error);
    Check(error.ok(), "create host string set");
    bool rejectedNull = false;
    try { stringSet.Append(Scalar::Null()); } catch (const std::invalid_argument&) { rejectedNull = true; }
    Check(rejectedNull && stringSet.Count()==0, "C++ string set rejects null without mutation");
    Check(!stringSet.Contains(Scalar::Null()) && !stringSet.Remove(Scalar::Null()), "null set queries do not insert");
    stringSet.Append(Scalar::String("x"));
    const std::string setJson = program->ToJson(stringSet, error);
    Check(error.ok() && setJson=="[\"x\"]", "C++ string set encoding");
    Scalar decodedSet = program->ParseJson("set<string>", setJson, error);
    Check(error.ok() && decodedSet.Count()==1 && decodedSet.Contains(Scalar::String("x")), "C++ string set roundtrip");
    program->ParseJson("set<string>", "[\"x\",null]", error);
    Check(!error.ok() && error.message.find("$[1]: set element cannot be null")!=std::string::npos,
        "JSON string set rejects null at its path");
    Scalar intSet = program->CreateValue("set<int>", error);
    Check(error.ok(), "create host int set");
    rejectedNull = false;
    try { intSet.Append(Scalar::Null()); } catch (const std::invalid_argument&) { rejectedNull = true; }
    Check(rejectedNull && intSet.Count()==0, "C++ int set rejects null");
    program->ParseJson("set<int>", "[null]", error);
    Check(!error.ok(), "JSON int set rejects null");
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
    Boolean(loaded->Call("StringSets"), "image restores non-null string set behavior");
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
            "}\n"
            "public string Error(){list<Record> v=system.jsonDecodeOr<list<Record>>(\"1\",[]);return system.jsonError();}\n"
            "public int Fail(){Record v=system.jsonDecode<Record>(\"null\");return v.value;}\n";
        return true;
    };
    Error error;
    auto program = Program::Compile("import data as alias;export string Read(){return alias.Encode();}"
        "export string Error(){return alias.Error();}export int Fail(){return alias.Fail();}",
        error, "json-import.nc", debug, options);
    Check(program && error.ok(), "module-local JSON target: " + error.message);
    const Result result = program->Call("Read");
    Completed(result, "module JSON roundtrip");
    Check(result.value.asString() == "{\"value\":17}", "module rewriting preserves struct JSON field names");
    std::vector<std::uint8_t> image;
    Check(program->SaveImage(image,error),"save module diagnostic names");
    for(int pass=0;pass<2;++pass)
    {
        if(pass) program=Program::LoadImage(image,error);
        Check(bool(program),"load module diagnostic names: "+error.message);
        const auto failure=program->Call("Error");
        Completed(failure,"module JSON diagnostic value");
        Check(failure.value.asString()=="$: expected an array for list<data.Record>","JSON uses original qualified type name");
        const auto strict=program->Call("Fail");
        Check(!strict.ok() && strict.error.message.find("null is not allowed for data.Record")!=std::string::npos &&
            strict.error.message.substr(0,strict.error.message.find('\n')).find("__ncmodule_")==std::string::npos,
            "strict module JSON errors hide internal type names");
        Scalar record=program->CreateValue("data.Record",error);
        Check(error.ok() && record.typeName()=="data.Record","host values use qualified display names without changing type identity");
    }
}

void EncodeVariants(bool debug)
{
    Error error;
    auto program=Program::Compile(R"NC(import system;
struct Reading { list<float> values; };
export string Try(float number){return system.jsonEncodeOrNull(Reading([1.0,number]),true);}
export string Strict(float number){return system.jsonEncode(number);}
export string Error(){return system.jsonError();}
export string Null(){string value=null;return system.jsonEncodeOrNull(value);}
export string Sorted(){map<string,int> value={"z":2,"a":1};return system.jsonEncodeOrNull(value,true);}
)NC",error,"json-encode-variants.nc",debug);
    Check(bool(program),"compile encode variants: "+error.message);
    std::vector<std::uint8_t> image;
    Check(program->SaveImage(image,error),"save encode variant image");
    for(int pass=0;pass<2;++pass)
    {
        if(pass) program=Program::LoadImage(image,error);
        Check(bool(program),"load encode variant image: "+error.message);
        auto other=program->CreateInstance(error);
        for(double number:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()})
        {
            const auto value=program->Call("Try",{Scalar::Float(number)});
            Completed(value,"nonfinite encode failure is a value");
            Check(value.value.kind()==Scalar::Kind::Null,"failed encode returns actual null");
            const auto reason=program->Call("Error");
            Completed(reason,"encode error text");
            Check(reason.value.asString().find("$.values[1]:")==0,"encode failure retains its JSON path");
            const auto isolated=other->Call("Error");
            Check(isolated.ok() && isolated.value.asString().empty(),"JSON error state belongs to the caller instance");
        }
        const auto good=program->Call("Try",{Scalar::Float(2.0)});
        Completed(good,"successful encode after failure");
        Check(good.value.asString().find("values")!=std::string::npos,"encode resumes without a partial result");
        Check(program->Call("Error").value.asString().empty(),"successful encode clears jsonError");
        const auto null=program->Call("Null");
        Check(null.ok() && null.value.kind()==Scalar::Kind::String && null.value.asString()=="null","encoded JSON null differs from failed encode");
        const auto sorted=program->Call("Sorted");
        Check(sorted.ok() && sorted.value.asString()=="{\"a\":1,\"z\":2}","OrNull accepts sortKeys");
        const auto strict=program->Call("Strict",{Scalar::Float(std::numeric_limits<double>::infinity())});
        Check(!strict.ok(),"strict jsonEncode remains a runtime error");
    }
    for(const char* source:{"system.jsonEncodeOrNull(1);","import system;system.jsonEncodeOrNull();",
        "import system;system.jsonEncodeOrNull(1,1);","import system;system.jsonEncodeOrNull(fn()->int{return 1;});"})
        Check(!Program::Compile(source,error),"invalid encode variant remains a compile error");
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

// 실패를 값으로 받는 해독, 너그러운 해독, 경로가 붙은 오류 메시지, 필드 기본값 채움.
void DecodeVariants(bool debug)
{
    Error error;
    auto program = Program::Compile(
        "import system;\n"
        "const int NO_TARGET = -1;\n"
        "struct Inventory { int slots = 20; list<int> items; };\n"
        "struct Slot { int version = 3; int gold; int target = NO_TARGET; float best = 999.0; string name; Inventory inventory; Vector3 spawn = Vector3(0, 1, 0); };\n"
        "export string OrError(string text) { Slot s = system.jsonDecodeOr<Slot>(text, Slot()); return system.jsonError(); }\n"
        "export int OrGold(string text) { Slot s = system.jsonDecodeOr<Slot>(text, Slot()); return s.gold; }\n"
        "export int LenientTarget(string text) { Slot s = system.jsonDecodeLenient<Slot>(text); return s.target; }\n"
        "export int LenientSlots(string text) { return system.jsonDecodeLenient<Slot>(text).inventory.slots; }\n"
        "export float LenientSpawnY(string text) { return system.jsonDecodeLenient<Slot>(text).spawn.y; }\n"
        "export string LenientOrError(string text) { Slot s = system.jsonDecodeLenientOr<Slot>(text, Slot()); return system.jsonError(); }\n"
        "export int Strict(string text) { return system.jsonDecode<Slot>(text).gold; }\n"
        "export string ListError(string text) { list<int> v = system.jsonDecodeOr<list<int>>(text, []); return system.jsonError(); }\n",
        error, "json-variants.nc", debug);
    Check(program && error.ok(), "compile JSON variants: " + error.message);
    if (!program) return;
    const auto text = [&](const char* function, const char* argument, const char* expected, const char* label)
    {
        const Result result = program->Call(function, {Scalar::String(argument)});
        Completed(result, label);
        if (result.ok()) Check(result.value.asString() == expected, std::string(label) + ": " + result.value.asString());
    };
    const char* ok = "{\"version\":3,\"gold\":120,\"target\":7,\"best\":41.5,\"name\":\"a\",\"inventory\":{\"slots\":5,\"items\":[1,2]},\"spawn\":[1,2,3]}";
    const char* missing = "{\"version\":3,\"gold\":120}";
    const char* extra = "{\"version\":3,\"gold\":120,\"target\":7,\"best\":41.5,\"name\":\"a\",\"inventory\":{\"slots\":5,\"items\":[1,2]},\"spawn\":[1,2,3],\"legacyScore\":77}";
    const char* old = "{\"version\":2,\"gold\":50,\"legacyScore\":77,\"inventory\":{\"items\":[9]}}";
    const char* badType = "{\"version\":2,\"gold\":\"many\"}";
    const char* nestedBad = "{\"version\":3,\"gold\":1,\"target\":7,\"best\":1.0,\"name\":\"a\",\"inventory\":{\"slots\":5,\"items\":[1,\"x\"]},\"spawn\":[1,2,3]}";
    text("OrError", missing, "$.target: missing field", "missing field reports its path");
    Integer(program->Call("OrGold", {Scalar::String(missing)}), 0, "failed decode returns the fallback, not a partial value");
    text("OrError", extra, "$.legacyScore: unknown field", "strict decode reports unknown fields");
    text("OrError", badType, "$.gold: expected int", "type mismatch reports its path");
    text("OrError", nestedBad, "$.inventory.items[1]: expected int", "nested paths include fields and indices");
    text("OrError", ok, "", "successful decode clears jsonError");
    Integer(program->Call("LenientTarget", {Scalar::String(old)}), -1, "lenient decode fills a missing field from its declared default");
    Integer(program->Call("LenientSlots", {Scalar::String(old)}), 20, "lenient decode fills nested struct defaults");
    const Result spawn = program->Call("LenientSpawnY", {Scalar::String(old)});
    Completed(spawn, "lenient vector default");
    if (spawn.ok()) Check(spawn.value.asFloat() == 1.0, "lenient decode fills a vector default");
    text("LenientOrError", badType, "$.gold: expected int", "lenient decode still rejects a type mismatch");
    text("LenientOrError", old, "", "lenient decode ignores unknown fields");
    const Result strict = program->Call("Strict", {Scalar::String(missing)});
    Check(!strict.ok() && strict.error.message.find("$.target: missing field") != std::string::npos,
        "strict decode fails with the missing field path: " + strict.error.message);
    text("ListError", "[1,\"x\"]", "$[1]: expected int", "list element paths use indices");
    const Result truncated = program->Call("OrError", {Scalar::String("{\"version\":3,\"go")});
    Completed(truncated, "truncated input");
    if (truncated.ok()) Check(truncated.value.asString().find("unexpected end of data") != std::string::npos,
        "truncated input reports the parser position: " + truncated.value.asString());
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
        "import system; string s=system.jsonDecode<void>(\"null\");",
        "import system; int x=system.jsonDecodeOr<int>(\"1\");",
        "import system; int x=system.jsonDecodeOr<int>(\"1\",\"a\");",
        "import system; int x=system.jsonDecodeLenient<int>(\"1\",2);",
        "import system; int x=system.jsonDecodeLenientOr(\"1\",2);",
        "import system; string e=system.jsonError(1);",
        "string e=system.jsonError();"
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
        {"module types", &ImportedTypes}, {"runtime errors", &Failures}, {"host decode", &HostDecode},
        {"decode variants", &DecodeVariants}, {"encode variants", &EncodeVariants}
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
