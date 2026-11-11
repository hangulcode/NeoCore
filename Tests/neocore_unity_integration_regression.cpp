#include "NeoCoreC.h"

#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
typedef std::unique_ptr<NcProgram, void (*)(NcProgram*)> Program;
typedef std::unique_ptr<NcInstance, void (*)(NcInstance*)> Instance;
typedef std::unique_ptr<NcArguments, void (*)(NcArguments*)> Arguments;
typedef std::unique_ptr<NcResult, void (*)(NcResult*)> Result;
typedef std::unique_ptr<NcValue, void (*)(NcValue*)> Value;
typedef std::unique_ptr<NcOptions, void (*)(NcOptions*)> Options;
typedef std::unique_ptr<NcBytes, void (*)(NcBytes*)> Bytes;
int checks = 0;
uint32_t Length(const char* text) { return static_cast<uint32_t>(std::strlen(text)); }
void Check(bool condition, const std::string& message)
{ ++checks; if (!condition) throw std::runtime_error(message); }
std::string Error(NcResult* result)
{ uint32_t size = 0; const char* text = NcResultError(result, &size); return std::string(text, size); }
void Status(NcResult* result, int status, const char* label)
{ Check(result && NcResultStatus(result) == status, std::string(label) + ": " + Error(result) + " at " + std::to_string(NcResultLine(result)) + ":" + std::to_string(NcResultColumn(result))); }
void Done(NcResult* raw, const char* label)
{ Result result(raw, &NcResultDestroy); Status(result.get(), NC_COMPLETED, label); }
void Rejected(NcResult* raw, const char* label)
{ Result result(raw, &NcResultDestroy); Status(result.get(), NC_FAILED, label); Check(!Error(result.get()).empty(), label); }
void Int(NcResult* raw, int64_t expected, const char* label)
{ Result result(raw, &NcResultDestroy); Status(result.get(), NC_COMPLETED, label); Check(NcResultKind(result.get()) == NC_INT && NcResultInt(result.get()) == expected, label); }
Value Capture(NcResult* raw, int kind, const char* label)
{
    Result result(raw, &NcResultDestroy); Status(result.get(), NC_COMPLETED, label);
    Value value(NcResultValue(result.get()), &NcValueDestroy);
    Check(value && NcValueKind(value.get()) == kind, label); return value;
}
Value Integer(int64_t number)
{
    Arguments args(NcArgumentsCreate(), &NcArgumentsDestroy);
    Check(NcArgumentsInt(args.get(), number) != 0, "integer argument");
    return Value(NcArgumentsValueAt(args.get(), 0), &NcValueDestroy);
}
Value String(const std::string& text)
{
    Arguments args(NcArgumentsCreate(), &NcArgumentsDestroy);
    Check(NcArgumentsString(args.get(), text.data(), static_cast<uint32_t>(text.size())) != 0, "string argument");
    return Value(NcArgumentsValueAt(args.get(), 0), &NcValueDestroy);
}
Program Compile(const char* source, bool debug, NcOptions* options = 0)
{
    NcResult* raw = 0;
    Program program(NcCompileWithOptions(source, Length(source), "integration.nc", 14, debug, options, &raw), &NcProgramDestroy);
    Result result(raw, &NcResultDestroy); Status(result.get(), NC_COMPLETED, "compile integration");
    Check(bool(program), "compiled integration program"); return program;
}
Instance Create(NcProgram* program)
{
    NcResult* raw = 0; Instance instance(NcInstanceCreate(program, &raw), &NcInstanceDestroy);
    Result result(raw, &NcResultDestroy); Status(result.get(), NC_COMPLETED, "create instance");
    Check(bool(instance), "created instance"); return instance;
}
NcResult* Call(NcInstance* instance, const char* name, NcArguments* args = 0)
{ return NcCall(instance, name, Length(name), args); }

void Collections(bool debug)
{
    Program program = Compile(
        "import system;\n"
        "struct Point { int x; int y; };\n"
        "export list<int> values = [3,5];\n"
        "export Point point = Point(2,4);\n"
        "int hidden = 10;\n"
        "export int Read(list<int> x){return x[0];}\n"
        "export map<string,int> MakeMap(){return {}; }\n"
        "export set<int> MakeSet(){return set<int>([1]);}\n"
        "export set<string> MakeStringSet(){return set<string>();}\n"
        "export int ArrayCount(array<int> input){return input.len();}\n"
        "export array<int> MakeArray(){return array<int>(2);}\n"
        "export array<Vector3> MakeVectors(){return array<Vector3>(2,Vector3(1,2,3));}\n"
        "export fn()->int Make(){int n=4;return fn()->int {n+=1;return n;};}\n", debug);
    Instance instance = Create(program.get());
    Value values = Capture(NcReadGlobal(instance.get(), "values", 6), NC_LIST, "read exported list");
    Value alias = Capture(NcReadGlobal(instance.get(), "values", 6), NC_LIST, "read list alias");
    Check(NcValueSameStorage(values.get(), alias.get()) != 0, "list identity across results");
    Value nine = Integer(9); Value wrong = String("bad");
    Done(NcValueSet(values.get(), 0, nine.get()), "list mutation");
    Int(NcValueAt(alias.get(), 0), 9, "list alias sees mutation");
    Rejected(NcValueAppend(values.get(), wrong.get()), "typed append rejects wrong type");
    Check(NcValueCount(values.get()) == 2, "rejected mutation leaves list unchanged");
    Rejected(NcValueAt(values.get(), UINT64_MAX), "wide collection index rejected");
    Rejected(NcReadGlobal(instance.get(), "hidden", 6), "private globals unavailable");
    Rejected(NcWriteGlobal(instance.get(), "values", 6, wrong.get()), "global static type enforced");
    Arguments args(NcArgumentsCreate(), &NcArgumentsDestroy);
    Check(NcArgumentsValue(args.get(), values.get()) != 0, "owned list argument");
    Int(Call(instance.get(), "Read", args.get()), 9, "list argument identity");
    Value fresh = Capture(NcValueCreate(program.get(), "list<int>", 9), NC_LIST, "create list");
    Done(NcValueAppend(fresh.get(), nine.get()), "append to created list");
    Done(NcWriteGlobal(instance.get(), "values", 6, fresh.get()), "write exported list");
    Value replacement = Capture(NcReadGlobal(instance.get(), "values", 6), NC_LIST, "read replacement");
    Check(NcValueSameStorage(replacement.get(), fresh.get()) != 0, "global write preserves collection identity");

    Value map = Capture(Call(instance.get(), "MakeMap"), NC_MAP, "map result");
    Value key = String(std::string("a\0b", 3));
    Done(NcValuePut(map.get(), key.get(), nine.get()), "map insert");
    Int(NcValueGet(map.get(), key.get()), 9, "embedded NUL map key");
    Value storedKey = Capture(NcValueKeyAt(map.get(), 0), NC_STRING, "map key enumeration");
    uint32_t size = 0; const char* bytes = NcValueString(storedKey.get(), &size);
    Check(size == 3 && std::memcmp(bytes, "a\0b", 3) == 0, "map key copied exactly");
    Rejected(NcValuePut(map.get(), nine.get(), nine.get()), "wrong map key type");
    Value set = Capture(Call(instance.get(), "MakeSet"), NC_SET, "set result");
    Done(NcValueAppend(set.get(), nine.get()), "set add");
    Done(NcValueAppend(set.get(), nine.get()), "set duplicate add");
    Check(NcValueCount(set.get()) == 2, "set deduplicates");
    Result contains(NcValueContains(set.get(), nine.get()), &NcResultDestroy);
    Status(contains.get(), NC_COMPLETED, "set membership"); Check(NcResultBool(contains.get()) != 0, "set contains added value");
    Arguments nullArguments(NcArgumentsCreate(), &NcArgumentsDestroy);
    Check(NcArgumentsNull(nullArguments.get()) != 0, "null argument");
    Value nullValue(NcArgumentsValueAt(nullArguments.get(), 0), &NcValueDestroy);
    Value strings = Capture(Call(instance.get(), "MakeStringSet"), NC_SET, "string set result");
    Rejected(NcValueAppend(strings.get(), nullValue.get()), "C ABI string set rejects null");
    Check(NcValueCount(strings.get()) == 0, "C ABI invalid set append does not mutate");
    Result setJson(NcToJson(program.get(), strings.get(), 0), &NcResultDestroy);
    Status(setJson.get(), NC_COMPLETED, "C ABI empty set encoding");
    uint32_t setLength = 0;
    const char* setText = NcResultString(setJson.get(), &setLength);
    Check(std::string(setText, setLength)=="[]", "C ABI failed set insertion leaves empty JSON");
    Rejected(NcParseJson(program.get(), "set<string>", 11, "[null]", 6, 1), "C ABI string set rejects null JSON");
    Rejected(NcValueAppend(set.get(), nullValue.get()), "C ABI int set rejects null");
    Rejected(Call(instance.get(), "ArrayCount", nullArguments.get()), "C ABI null array argument fails safely");
    Value array = Capture(Call(instance.get(), "MakeArray"), NC_ARRAY, "array result");
    Done(NcValueSet(array.get(), 1, nine.get()), "array write");
    Int(NcValueAt(array.get(), 1), 9, "array read");
    Done(NcValueResize(array.get(), 4), "array growth"); Int(NcValueAt(array.get(), 3), 0, "array initializes new slots");
    Rejected(NcValueResize(array.get(), UINT64_MAX), "wide resize rejected before allocation");
    Rejected(NcValueReserve(array.get(), UINT64_MAX), "wide reserve rejected before allocation");
    Done(NcValueReserve(array.get(), 16), "array reserve");
    Rejected(NcValueRemoveAt(array.get(), 0), "array indexed removal is unsupported");
    Done(NcValueRemoveAt(values.get(), 1), "list indexed removal");
    Check(NcValueCount(values.get()) == 1, "list removal changes length");
    Value vectors = Capture(Call(instance.get(), "MakeVectors"), NC_ARRAY, "vector array result");
    Value vector = Capture(NcValueAt(vectors.get(), 0), NC_VECTOR3, "vector array element");
    float components[4] = { 0, 0, 0, 77 };
    Check(NcValueVector(vector.get(), components, 4) != 0 && components[0] == 1 && components[2] == 3 && components[3] == 77,
        "vector array read copies exact dimension");
    Rejected(NcValueSet(vectors.get(), 1, nine.get()), "vector array rejects scalar");
    Done(NcValueSet(vectors.get(), 1, vector.get()), "vector array element assignment");
    Value point = Capture(NcReadGlobal(instance.get(), "point", 5), NC_STRUCT, "struct result");
    Done(NcValueSet(point.get(), 0, nine.get()), "edit returned struct");
    Value originalPoint = Capture(NcReadGlobal(instance.get(), "point", 5), NC_STRUCT, "struct read again");
    Int(NcValueAt(originalPoint.get(), 0), 2, "struct read has value semantics");
    Done(NcWriteGlobal(instance.get(), "point", 5, point.get()), "struct write");
    Done(NcValueSet(point.get(), 0, Integer(12).get()), "edit host struct after assignment");
    Value copiedPoint = Capture(NcReadGlobal(instance.get(), "point", 5), NC_STRUCT, "read copied struct");
    Int(NcValueAt(copiedPoint.get(), 0), 9, "struct write has value semantics");
    Value function = Capture(Call(instance.get(), "Make"), NC_FUNCTION, "owned closure");
    Int(NcCallFunction(instance.get(), function.get(), 0, -1, 0), 5, "closure first call");
    Int(NcCallFunction(instance.get(), function.get(), 0, -1, 0), 6, "closure retains state");
    Program other = Compile("export int Read(list<int> x){return x[0];}", debug);
    Instance otherInstance = Create(other.get());
    Rejected(Call(otherInstance.get(), "Read", args.get()), "cross-runtime collection rejected");
    Done(NcReset(instance.get()), "reset with retained collections");
    Int(NcValueAt(values.get(), 0), 9, "old values survive reset");
    Rejected(NcCallFunction(instance.get(), function.get(), 0, -1, 0), "reset invalidates old closure");
    instance.reset(); program.reset();
    Int(NcValueAt(array.get(), 1), 9, "owned array survives program disposal");
    Int(NcValueGet(map.get(), key.get()), 9, "owned map survives program disposal");
    Done(NcValueClear(array.get()), "clear retained array"); Check(NcValueCount(array.get()) == 0, "array cleared");
}

struct Host
{
    int releases = 0;
    int initializations = 0;
    int64_t value = 40;
    bool valid = true;
    NcInstance* instance = 0;
    Value saved;
    Host() : saved(0, &NcValueDestroy) {}
};
void ReleaseHost(void* user) { ++static_cast<Host*>(user)->releases; }
void* Resolve(void* user, uint32_t, uint64_t id)
{ Host* host = static_cast<Host*>(user); return host->valid && id == 7 ? host : 0; }
int32_t Getter(void*, NcHostCall* call)
{ Host* host = static_cast<Host*>(NcHostResolveReceiver(call)); if (!host) return 0; NcHostReturnInt(call, host->value); return 1; }
int32_t Setter(void*, NcHostCall* call)
{ Host* host = static_cast<Host*>(NcHostResolveReceiver(call)); if (!host) return 0; host->value = NcHostInt(call, 0); return 1; }
int32_t Invoke(void* user, NcHostCall* call)
{
    Host* host = static_cast<Host*>(user);
    host->saved.reset(NcHostArgument(call, 0));
    if (!host->saved || !NcIsRunning(host->instance) || NcInstanceTryDestroy(host->instance)) return 0;
    Result reset(NcReset(host->instance), &NcResultDestroy);
    if (NcResultStatus(reset.get()) != NC_FAILED) return 0;
    Result nested(NcHostCallFunction(call, host->saved.get(), 0), &NcResultDestroy);
    if (NcResultStatus(nested.get()) != NC_COMPLETED) return 0;
    NcHostReturnInt(call, NcResultInt(nested.get())); return 1;
}
int32_t Echo(void*, NcHostCall* call)
{ Value input(NcHostArgument(call, 0), &NcValueDestroy); if (!input) return 0; NcHostReturnValue(call, input.get()); return 1; }

Options Schema(Host& host)
{
    Options options(NcOptionsCreate(), &NcOptionsDestroy);
    Check(NcOptionsSetResolver(options.get(), Resolve, &host, ReleaseHost) != 0, "set resolver");
    const uint32_t object = NcSchemaObject(options.get(), "HostType", 8);
    const uint32_t integer = NcTypeBuiltin(NC_INT);
    Check(object != 0 && integer != 0, "schema types");
    const uint32_t function = NcTypeFunction(options.get(), integer, 0, 0);
    const uint32_t list = NcTypeCollection(options.get(), NC_LIST, integer, 0, 0);
    Check(NcSchemaProperty(options.get(), object, "Value", 5, integer, Getter, Setter, &host, ReleaseHost) != 0, "schema property");
    Check(NcSchemaMethod(options.get(), object, "Invoke", 6, integer, &function, 1, Invoke, &host, ReleaseHost) != 0, "schema callback method");
    Check(NcSchemaMethod(options.get(), object, "Echo", 4, list, &list, 1, Echo, &host, ReleaseHost) != 0, "schema collection method");
    Check(NcSchemaGlobal(options.get(), "Host", 4, object, 7) != 0, "schema global"); return options;
}

void BindingsAndImages(bool debug)
{
    Host host; Options options = Schema(host);
    NcResult* rejectedRaw = 0;
    const char* invalid = "Host.Value=\"wrong\";";
    Program rejected(NcCompileWithOptions(invalid, Length(invalid), "invalid.nc", 10, debug, options.get(), &rejectedRaw), &NcProgramDestroy);
    Rejected(rejectedRaw, "host property type rejected during compile"); Check(!rejected, "invalid host access has no executable");
    Program program = Compile(
        "export int Run(){Host.Value += 2;return Host.Invoke(fn()->int {return Host.Value;});}\n"
        "export list<int> Echo(){list<int> values=[8,9];return Host.Echo(values);}\n", debug, options.get());
    Instance instance = Create(program.get()); host.instance = instance.get();
    Int(Call(instance.get(), "Run"), 42, "nested callback and active guards");
    Int(NcCallFunction(instance.get(), host.saved.get(), 0, -1, 0), 42, "callback survives native return");
    Value list = Capture(Call(instance.get(), "Echo"), NC_LIST, "host collection return");
    Int(NcValueAt(list.get(), 1), 9, "host collection contents");
    NcResult* raw = 0; Bytes image(NcSaveImage(program.get(), &raw), &NcBytesDestroy);
    Result saved(raw, &NcResultDestroy); Status(saved.get(), NC_COMPLETED, "save C image");
    uint64_t size = 0; const uint8_t* data = NcBytesData(image.get(), &size);
    Check(data && size > 24, "owned image bytes");
    Host second; second.value = 100; Options secondOptions = Schema(second);
    Program loaded(NcLoadImage(data, size, secondOptions.get(), &raw), &NcProgramDestroy);
    Result loadResult(raw, &NcResultDestroy); Status(loadResult.get(), NC_COMPLETED, "load C image with new bindings");
    Instance loadedInstance = Create(loaded.get()); second.instance = loadedInstance.get();
    image.reset(); secondOptions.reset();
    Int(Call(loadedInstance.get(), "Run"), 102, "loaded image resolves new host");
    host.valid = false; Rejected(Call(instance.get(), "Run"), "destroyed object ID rejected at runtime");
    options.reset(); program.reset(); Check(host.releases == 0, "instance and values retain callbacks");
    host.saved.reset(); list.reset(); instance.reset();
    Check(host.releases == 4, "each registration released once");
    second.saved.reset(); loadedInstance.reset(); loaded.reset();
    Check(second.releases == 4, "loaded callbacks released once");
}

// Out 매개변수: C ABI 로 등록한 메서드가 스크립트 변수에 값을 되돌려 쓴다.
int32_t TryGetInt(void*, NcHostCall* call)
{
    if (NcHostIsOut(call, 0) || !NcHostIsOut(call, 1)) return 0;
    const bool found = NcHostInt(call, 0) == 1;
    if (found) NcHostOutInt(call, 1, 41);
    NcHostReturnBool(call, found ? 1 : 0); return 1;
}
int32_t TryGetText(void*, NcHostCall* call) { NcHostOutString(call, 0, "hello", 5); NcHostReturnBool(call, 1); return 1; }
int32_t KeepList(void* user, NcHostCall* call)
{ static_cast<Host*>(user)->saved.reset(NcHostArgument(call, 0)); return static_cast<Host*>(user)->saved ? 1 : 0; }
int32_t TryGetList(void* user, NcHostCall* call)
{ NcHostOutValue(call, 0, static_cast<Host*>(user)->saved.get()); NcHostReturnBool(call, 1); return 1; }
int32_t TryGetVector(void*, NcHostCall* call)
{ const float v[3] = { 1.0f, 2.0f, 3.0f }; NcHostOutVector(call, 0, NC_VECTOR3, v); NcHostReturnBool(call, 1); return 1; }
int32_t WriteInput(void*, NcHostCall* call) { NcHostOutInt(call, 0, 5); NcHostReturnBool(call, 1); return 1; }

void OutParameters(bool debug)
{
    Host host; Options options(NcOptionsCreate(), &NcOptionsDestroy);
    Check(NcOptionsSetResolver(options.get(), Resolve, &host, ReleaseHost) != 0, "set out resolver");
    const uint32_t object = NcSchemaObject(options.get(), "HostType", 8);
    const uint32_t integer = NcTypeBuiltin(NC_INT), boolean = NcTypeBuiltin(NC_BOOL), text = NcTypeBuiltin(NC_STRING);
    const uint32_t vector = NcTypeBuiltin(NC_VECTOR3), none = NcTypeBuiltin(NC_VOID);
    const uint32_t list = NcTypeCollection(options.get(), NC_LIST, integer, 0, 0);
    const uint32_t pair[] = { integer, integer };
    const uint8_t pairModes[] = { NC_PARAMETER_IN, NC_PARAMETER_OUT }, out[] = { NC_PARAMETER_OUT }, invalidMode[] = { 2 };
    Check(NcSchemaMethodOut(options.get(), object, "Bad", 3, boolean, &integer, invalidMode, 1, WriteInput, &host, ReleaseHost) == 0, "invalid parameter mode rejected");
    uint32_t errorLength = 0; NcOptionsError(options.get(), &errorLength); Check(errorLength != 0, "invalid parameter mode reports an error");
    Check(NcSchemaMethodOut(options.get(), object, "TryGet", 6, boolean, pair, pairModes, 2, TryGetInt, &host, ReleaseHost) != 0, "schema out int method");
    Check(NcSchemaMethodOut(options.get(), object, "TryText", 7, boolean, &text, out, 1, TryGetText, &host, ReleaseHost) != 0, "schema out string method");
    Check(NcSchemaMethod(options.get(), object, "Keep", 4, none, &list, 1, KeepList, &host, ReleaseHost) != 0, "schema keep method");
    Check(NcSchemaMethodOut(options.get(), object, "TryList", 7, boolean, &list, out, 1, TryGetList, &host, ReleaseHost) != 0, "schema out list method");
    Check(NcSchemaMethodOut(options.get(), object, "TryVector", 9, boolean, &vector, out, 1, TryGetVector, &host, ReleaseHost) != 0, "schema out vector method");
    // 방향 배열이 없으면 모두 입력이다. 입력 매개변수에 쓰면 호출이 실패한다.
    Check(NcSchemaMethodOut(options.get(), object, "WriteInput", 10, boolean, &integer, 0, 1, WriteInput, &host, ReleaseHost) != 0, "schema input-only method");
    Check(NcSchemaGlobal(options.get(), "Host", 4, object, 7) != 0, "schema out global");
    NcResult* rejectedRaw = 0;
    const char* invalid = "export int Run(){int v=0;Host.TryGet(1,v);return v;}";
    Program rejected(NcCompileWithOptions(invalid, Length(invalid), "out_invalid.nc", 14, debug, options.get(), &rejectedRaw), &NcProgramDestroy);
    Rejected(rejectedRaw, "missing out rejected during compile"); Check(!rejected, "missing out has no executable");
    Program program = Compile(
        "export int Run(){int v=1;if(!Host.TryGet(1,out v))return -1;int a=v;if(Host.TryGet(2,out v))return -2;\n"
        "string s=\"x\";Host.TryText(out s);list<int> xs=[5];Host.Keep(xs);list<int> got=[];Host.TryList(out got);\n"
        "Vector3 p=Vector3(0,0,0);Host.TryVector(out p);\n"
        "return a*100+v+s.len()*1000+got[0]*10000+toint(p.x+p.y+p.z)*100000;}\n"
        "export bool Bad(){return Host.WriteInput(1);}\n", debug, options.get());
    Instance instance = Create(program.get()); host.instance = instance.get();
    Int(Call(instance.get(), "Run"), 659141, "C ABI out parameters write back");
    Rejected(Call(instance.get(), "Bad"), "writing an input parameter fails the call");
    Int(Call(instance.get(), "Run"), 659141, "out parameters still work after a failed call");
    host.saved.reset();
}

struct EventRegistration
{
    uint64_t caller;
    uint64_t receiver;
    std::string name;
    Value function;
    EventRegistration(NcHostCall* call, const std::string& event)
        : caller(NcHostCallerInstanceId(call)), receiver(NcHostReceiverId(call)), name(event),
          function(NcHostArgument(call, 1), &NcValueDestroy) {}
};
struct EventHost
{
    int releases = 0;
    std::vector<EventRegistration> registrations;
};
void ReleaseEventHost(void* user) { ++static_cast<EventHost*>(user)->releases; }
void* ResolveEventHost(void* user, uint32_t, uint64_t id) { return id == 7 ? user : nullptr; }
int32_t RegisterEvent(void* user, NcHostCall* call)
{
    EventHost& host = *static_cast<EventHost*>(user);
    uint32_t length = 0;
    const char* text = NcHostString(call, 0, &length);
    host.registrations.emplace_back(call, std::string(text, length));
    return host.registrations.back().caller != 0 && host.registrations.back().function != nullptr;
}
void Events(bool debug)
{
    EventHost host;
    Options options(NcOptionsCreate(), &NcOptionsDestroy);
    const uint32_t owner = NcSchemaObject(options.get(), "Entity", 6);
    const uint32_t number = NcTypeBuiltin(NC_FLOAT), text = NcTypeBuiltin(NC_STRING);
    const uint32_t update = NcTypeFunction(options.get(), NcTypeBuiltin(NC_VOID), &number, 1);
    const uint32_t query = NcTypeFunction(options.get(), NcTypeBuiltin(NC_INT), &text, 1);
    Check(NcOptionsSetResolver(options.get(), ResolveEventHost, &host, ReleaseEventHost) != 0, "event resolver");
    Check(NcSchemaGlobal(options.get(), "GameObject", 10, owner, 7) != 0, "event global");
    Check(NcSchemaEvent(options.get(), owner, "OnUpdate", 8, update, "AddEvent", 8,
        RegisterEvent, &host, ReleaseEventHost) != 0, "typed update event registration");
    Check(NcSchemaEvent(options.get(), owner, "OnQuery", 7, query, "AddEvent", 8,
        RegisterEvent, &host, ReleaseEventHost) != 0, "different callback signature on same method");
    Check(NcSchemaEvent(options.get(), owner, "OnUpdate", 8, update, "AddEvent", 8,
        RegisterEvent, &host, ReleaseEventHost) == 0, "duplicate event rejected");
    Check(NcSchemaEvent(options.get(), owner, "Bad", 3, number, "AddEvent", 8,
        RegisterEvent, &host, ReleaseEventHost) == 0, "nonfunction callback rejected");
    Check(host.releases == 0, "failed event registration does not take callback ownership");
    NcResult* rejectedRaw = nullptr;
    const char* invalid = "void F(int dt){} GameObject.AddEvent(\"OnUpdate\",F);";
    Program rejected(NcCompileWithOptions(invalid, Length(invalid), "event.nc", 8, debug, options.get(), &rejectedRaw), &NcProgramDestroy);
    Rejected(rejectedRaw, "event callback mismatch rejected at compile time");
    Check(!rejected, "invalid event callback has no program");
    Program program = Compile(
        "int count=0; void OnUpdate(float dt){count+=toint(dt);}\n"
        "int OnQuery(string name){return count+name.len();}\n"
        "GameObject.AddEvent(\"OnUpdate\",OnUpdate);\n"
        "GameObject.AddEvent(\"OnQuery\",OnQuery);\n"
        "export int Count(){return count;}\n", debug, options.get());
    Instance first = Create(program.get()), second = Create(program.get());
    const uint64_t firstId = NcInstanceId(first.get()), secondId = NcInstanceId(second.get());
    Check(firstId != 0 && secondId != 0 && firstId != secondId, "instance identities are distinct");
    Check(NcInstanceId(nullptr) == 0 && NcHostCallerInstanceId(nullptr) == 0, "null identity handles");
    Done(NcRun(first.get()), "register first instance events");
    Done(NcRun(second.get()), "register second instance events");
    Check(host.registrations.size() == 4, "two event registrations per instance");
    Check(host.registrations[0].caller == firstId && host.registrations[2].caller == secondId &&
        host.registrations[0].receiver == 7 && host.registrations[2].receiver == 7,
        "same receiver keeps distinct caller instance identities");
    Arguments tick(NcArgumentsCreate(), &NcArgumentsDestroy);
    Check(NcArgumentsFloat(tick.get(), 5.0) != 0, "event argument");
    Done(NcCallFunction(first.get(), host.registrations[0].function.get(), tick.get(), -1, 0), "private update function event");
    Int(Call(first.get(), "Count"), 5, "first event mutates caller globals");
    Int(Call(second.get(), "Count"), 0, "peer instance remains unchanged");
    Arguments name(NcArgumentsCreate(), &NcArgumentsDestroy);
    Check(NcArgumentsString(name.get(), "abc", 3) != 0, "query event argument");
    Int(NcCallFunction(first.get(), host.registrations[1].function.get(), name.get(), -1, 0), 8, "typed event return value");
    Rejected(Call(first.get(), "OnUpdate", tick.get()), "callback remains private to named host lookup");
    host.registrations.clear();
    Done(NcReset(first.get()), "host clears old registrations before reset");
    Check(NcInstanceId(first.get()) == firstId, "reset preserves caller ID");
    options.reset(); program.reset(); first.reset(); second.reset();
    Check(host.releases == 3, "event registrations release ownership once");
}

struct Services
{
    int loads = 0;
    std::atomic<int> sends;
    std::atomic<int> released;
    std::atomic<bool> valid;
    Services() : sends(0), released(0), valid(true) {}
};
void ReleaseServices(void* user) { ++static_cast<Services*>(user)->released; }
int32_t Module(void* user, const char* name, uint32_t length, const char*, uint32_t, NcModuleOutput* output)
{
    Services& service = *static_cast<Services*>(user); ++service.loads;
    if (std::string(name, length) != "custom") { NcModuleError(output, "module absent", 13); return 0; }
    std::string source = "public int Get(){return 17;}";
    NcModuleSource(output, source.data(), static_cast<uint32_t>(source.size()), "custom.nc", 9);
    source.assign(source.size(), 'x'); return 1;
}
void Http(void* user, const NcHttpRequest* request, NcHttpResponse* response)
{
    Services& service = *static_cast<Services*>(user);
    uint32_t length = 0; const char* text = NcHttpUrl(request, &length);
    const bool url = std::string(text, length) == "https://sample.invalid/echo";
    text = NcHttpMethod(request, &length); const std::string method(text, length);
    bool valid = url && NcHttpTimeout(request) == 5000 && !NcHttpIsCancelled(request);
    if (method == "POST")
    {
        text = NcHttpBody(request, &length); valid = valid && std::string(text, length) == "payload";
        valid = valid && NcHttpHeaderCount(request) == 1;
        text = NcHttpHeaderName(request, 0, &length); valid = valid && std::string(text, length) == "X-Check";
        text = NcHttpHeaderValue(request, 0, &length); valid = valid && std::string(text, length) == "yes";
    }
    else valid = valid && method == "GET";
    if (!valid) service.valid = false;
    ++service.sends;
    std::string body = "reply";
    NcHttpComplete(response, valid, body.data(), static_cast<uint32_t>(body.size()), "", 0);
    body.assign(body.size(), 'x');
}
void ModulesAndHttp(bool debug)
{
    Services service; Options options(NcOptionsCreate(), &NcOptionsDestroy);
    Check(NcOptionsSetModuleLoader(options.get(), Module, &service, ReleaseServices) != 0, "register module loader");
    Check(NcOptionsSetHttpTransport(options.get(), Http, &service, ReleaseServices) != 0, "register HTTP transport");
    Program program = Compile(
        "import system; import custom; int count=0; bool correct=true;\n"
        "void Got(bool ok,string body){correct=correct&&ok&&body==\"reply\";count+=1;}\n"
        "AsyncRequest r=system.async_create();r.get(5000,\"https://sample.invalid/echo\",Got);r.wait();r.close();\n"
        "r=system.async_create();r.add_header(\"X-Check\",\"yes\");r.post(5000,\"https://sample.invalid/echo\",\"payload\",Got);r.wait();r.close();\n"
        "export int Verify(){if(correct&&count==2)return custom.Get();return 0;}\n", debug, options.get());
    Check(service.loads == 1, "module loader called once");
    Instance instance = Create(program.get()); options.reset(); program.reset();
    Check(service.released == 0, "instance owns module and transport callbacks");
    Int(Call(instance.get(), "Verify"), 17, "typed module and async HTTP completed");
    Check(service.sends == 2 && service.valid, "request accessors preserve HTTP inputs");
    instance.reset(); Check(service.released == 2, "last instance releases both services");
}

struct Cancellation
{
    std::atomic<bool> started;
    std::atomic<bool> cancelled;
    std::atomic<int> released;
    Cancellation() : started(false), cancelled(false), released(0) {}
};
void ReleaseCancellation(void* user) { ++static_cast<Cancellation*>(user)->released; }
void BlockingHttp(void* user, const NcHttpRequest* request, NcHttpResponse* response)
{
    Cancellation& state = *static_cast<Cancellation*>(user); state.started = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!NcHttpIsCancelled(request) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    state.cancelled = NcHttpIsCancelled(request) != 0;
    NcHttpComplete(response, 0, "", 0, "cancelled", 9);
}
void HttpCancellation()
{
    Cancellation state; Options options(NcOptionsCreate(), &NcOptionsDestroy);
    Check(NcOptionsSetHttpTransport(options.get(), BlockingHttp, &state, ReleaseCancellation) != 0, "register cancellable transport");
    Program program = Compile(
        "import system; int completed=0;void Got(bool ok,string body){completed+=1;}\n"
        "AsyncRequest r=system.async_create();export void Start(){r.get(-1,\"https://sample.invalid/wait\",Got);}\n"
        "export int Count(){return completed;}\n", true, options.get());
    Instance instance = Create(program.get()); options.reset(); program.reset();
    Done(Call(instance.get(), "Start"), "start pending transport");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!state.started && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    Check(state.started, "transport worker started");
    Done(NcReset(instance.get()), "reset discards pending request ownership");
    Int(Call(instance.get(), "Count"), 0, "reset does not invoke stale request callback");
    instance.reset(); Check(state.cancelled, "runtime shutdown cooperatively cancels active transport");
    Check(state.released == 1, "transport released after worker exits");
}

void JsonAndValueCopies(bool debug)
{
    Program program = Compile(
        "struct Data {int id;string name;list<float> samples;Vector3 point;};\n"
        "export Data Echo(Data value){return value;}\n"
        "export map<string,Data> Wrap(Data value){map<string,Data> entries={};entries[\"item\"]=value;return entries;}\n"
        "export Vector3 p=Vector3(1,2,3);export void Bump(){p.x+=1.0;}\n", debug);
    Instance instance = Create(program.get());
    const char* json="{\"id\":9007199254740993,\"name\":\"sample\",\"samples\":[1,2.5],\"point\":[1,2,3]}";
    Value data=Capture(NcParseJson(program.get(),"Data",4,json,Length(json),1),NC_STRUCT,"typed JSON decode");
    Int(NcValueAt(data.get(),0),INT64_C(9007199254740993),"JSON int64 precision");
    Value text=Capture(NcValueAt(data.get(),1),NC_STRING,"JSON string field");
    uint32_t length=0;const char* bytes=NcValueString(text.get(),&length);
    Check(length==6&&std::memcmp(bytes,"sample",6)==0,"JSON string copied");
    Result encoded(NcToJson(program.get(),data.get(),1),&NcResultDestroy);
    Status(encoded.get(),NC_COMPLETED,"typed JSON encode");
    bytes=NcResultString(encoded.get(),&length);
    Value roundtrip=Capture(NcParseJson(program.get(),"Data",4,bytes,length,1),NC_STRUCT,"typed JSON roundtrip");
    Int(NcValueAt(roundtrip.get(),0),INT64_C(9007199254740993),"roundtrip int64");
    for(const char* invalid:{"{}","{\"id\":1.5,\"name\":\"x\",\"samples\":[],\"point\":[1,2,3]}",
        "{\"id\":1,\"name\":\"x\",\"samples\":[],\"point\":[1,2,3],\"extra\":0}"})
        Rejected(NcParseJson(program.get(),"Data",4,invalid,Length(invalid),1),"JSON static shape rejects mismatch");
    Arguments fields(NcArgumentsCreate(),&NcArgumentsDestroy);
    for(uint64_t index=0;index<4;++index)
    {
        Value field(NcResultValue(Result(NcValueAt(data.get(),index),&NcResultDestroy).get()),&NcValueDestroy);
        Check(NcArgumentsValue(fields.get(),field.get())!=0,"append struct field");
    }
    Value constructed=Capture(NcValueCreateFields(program.get(),"Data",4,fields.get()),NC_STRUCT,"construct typed struct fields");
    Arguments args(NcArgumentsCreate(),&NcArgumentsDestroy);
    Check(NcArgumentsValue(args.get(),constructed.get())!=0,"struct argument");
    Value map=Capture(Call(instance.get(),"Wrap",args.get()),NC_MAP,"struct-valued map");
    Value key=String("item");Value element=Capture(NcValueGet(map.get(),key.get()),NC_STRUCT,"struct map read");
    Done(NcValueSet(element.get(),0,Integer(99).get()),"edit map read copy");
    Value retained=Capture(NcValueGet(map.get(),key.get()),NC_STRUCT,"read unchanged map value");
    Int(NcValueAt(retained.get(),0),INT64_C(9007199254740993),"map getter preserves struct value semantics");
    Done(Call(instance.get(),"Bump"),"boxed vector global component update");
    Value vector=Capture(NcReadGlobal(instance.get(),"p",1),NC_VECTOR3,"read boxed vector global");
    float components[3]={};Check(NcValueVector(vector.get(),components,3)&&components[0]==2,"boxed global read value");
    Done(NcWriteGlobal(instance.get(),"p",1,vector.get()),"write boxed vector global");
    Done(Call(instance.get(),"Bump"),"boxed vector global preserved write");
    vector=Capture(NcReadGlobal(instance.get(),"p",1),NC_VECTOR3,"read rewritten vector global");
    Check(NcValueVector(vector.get(),components,3)&&components[0]==3,"boxed global write value");

    Options options(NcOptionsCreate(),&NcOptionsDestroy);
    uint32_t type=NcSchemaStruct(options.get(),"Payload",7);
    NcField layout[]={{"amount",6,NcTypeBuiltin(NC_INT)},{"name",4,NcTypeBuiltin(NC_STRING)}};
    Check(type&&NcSchemaStructFields(options.get(),type,layout,2),"host declared struct schema");
    Program schemaProgram=Compile("export Payload Echo(Payload p){p.amount+=1;return p;}",debug,options.get());
    Arguments input(NcArgumentsCreate(),&NcArgumentsDestroy);NcArgumentsInt(input.get(),9);NcArgumentsString(input.get(),"host",4);
    Value payload=Capture(NcValueCreateFields(schemaProgram.get(),"Payload",7,input.get()),NC_STRUCT,"construct host schema struct");
    Arguments payloadArg(NcArgumentsCreate(),&NcArgumentsDestroy);NcArgumentsValue(payloadArg.get(),payload.get());
    Instance schemaInstance=Create(schemaProgram.get());
    Value returned=Capture(Call(schemaInstance.get(),"Echo",payloadArg.get()),NC_STRUCT,"host schema struct argument");
    Int(NcValueAt(returned.get(),0),10,"host schema struct result");Int(NcValueAt(payload.get(),0),9,"host schema struct argument copied");
}

int32_t LoadJson(void*, NcHostCall* call)
{
    uint32_t size = 0;
    const char* bytes = NcHostString(call, 0, &size);
    const std::string path(bytes, size);
    const char* json = path == "valid" ?
        "{\"id\":9007199254740993,\"name\":\"loaded\",\"values\":[1,2.5],\"note\":\"old\",\"note\":null}" :
        path == "missing" ? "{\"id\":1}" :
        "{\"id\":1.5,\"name\":\"fraction\",\"values\":[],\"note\":null}";
    return NcHostReturnJson(call, json, Length(json), path == "relaxed" ? 0 : 1);
}

int32_t LoadWideJson(void*, NcHostCall* call)
{
    const char* json = "9223372036854775808";
    return NcHostReturnJson(call, json, Length(json), NcHostBool(call, 0));
}

void ScriptJsonAndHostReturn(bool debug)
{
    Host host;
    Options options(NcOptionsCreate(), &NcOptionsDestroy);
    const uint32_t resource = NcSchemaObject(options.get(), "ResourceType", 12);
    const uint32_t payload = NcSchemaStruct(options.get(), "JsonPayload", 11);
    const uint32_t numberList = NcTypeCollection(options.get(), NC_LIST, NcTypeBuiltin(NC_FLOAT), 0, 0);
    const NcField fields[] = {
        {"id", 2, NcTypeBuiltin(NC_INT)},
        {"name", 4, NcTypeBuiltin(NC_STRING)},
        {"values", 6, numberList},
        {"note", 4, NcTypeNullable(options.get(), NcTypeBuiltin(NC_STRING))}
    };
    Check(payload && numberList && NcSchemaStructFields(options.get(), payload, fields, 4), "JSON host result schema");
    const uint32_t pathType = NcTypeBuiltin(NC_STRING);
    Check(NcOptionsSetResolver(options.get(), Resolve, &host, 0) &&
        NcSchemaGlobal(options.get(), "Resource", 8, resource, 7), "JSON host resource binding");
    Check(NcSchemaMethod(options.get(), resource, "LoadJson", 8, payload, &pathType, 1, LoadJson, 0, 0) != 0,
        "JSON callback has a static result shape");
    const uint32_t strictType = NcTypeBuiltin(NC_BOOL);
    Check(NcSchemaMethod(options.get(), resource, "LoadWide", 8, NcTypeBuiltin(NC_FLOAT), &strictType, 1,
        LoadWideJson, 0, 0) != 0, "JSON numeric policy callback schema");
    Program program = Compile(
        "import system;\n"
        "export int loads=0;\n"
        "export JsonPayload Load(string path){JsonPayload p=Resource.LoadJson(path);loads+=1;return p;}\n"
        "export JsonPayload Decode(string text){return system.jsonDecode<JsonPayload>(text);}\n"
        "export string Encode(JsonPayload value){return system.jsonEncode(value,true);}\n"
        "export float Wide(bool strict){return Resource.LoadWide(strict);}\n", debug, options.get());
    Instance instance = Create(program.get());
    Arguments path(NcArgumentsCreate(), &NcArgumentsDestroy);
    Check(NcArgumentsString(path.get(), "valid", 5) != 0, "JSON callback path");
    Value loaded = Capture(Call(instance.get(), "Load", path.get()), NC_STRUCT, "host JSON converted to declared struct");
    Int(NcValueAt(loaded.get(), 0), INT64_C(9007199254740993), "callback JSON int64 precision");
    Result note(NcValueAt(loaded.get(), 3), &NcResultDestroy);
    Status(note.get(), NC_COMPLETED, "callback JSON nullable field");
    Check(NcResultKind(note.get()) == NC_NULL, "callback JSON last duplicate null preserves field");
    Value values = Capture(NcValueAt(loaded.get(), 2), NC_LIST, "callback JSON nested list");
    Result number(NcValueAt(values.get(), 1), &NcResultDestroy);
    Status(number.get(), NC_COMPLETED, "callback JSON nested value");
    Check(NcResultKind(number.get()) == NC_FLOAT && NcResultFloat(number.get()) == 2.5, "callback JSON float value");
    Arguments encodedArg(NcArgumentsCreate(), &NcArgumentsDestroy);
    Check(NcArgumentsValue(encodedArg.get(), loaded.get()) != 0, "script encode struct argument");
    Result encoded(Call(instance.get(), "Encode", encodedArg.get()), &NcResultDestroy);
    Status(encoded.get(), NC_COMPLETED, "script JSON encode through C ABI");
    uint32_t size = 0;
    const char* bytes = NcResultString(encoded.get(), &size);
    Check(std::string(bytes, size).find("\"note\":null") != std::string::npos, "script encoding retains nullable field");
    Arguments text(NcArgumentsCreate(), &NcArgumentsDestroy);
    Check(NcArgumentsString(text.get(), bytes, size) != 0, "script JSON text argument");
    Value decoded = Capture(Call(instance.get(), "Decode", text.get()), NC_STRUCT, "script JSON decode through C ABI");
    Int(NcValueAt(decoded.get(), 0), INT64_C(9007199254740993), "script JSON roundtrip int64 precision");
    Result decodedNote(NcValueAt(decoded.get(), 3), &NcResultDestroy);
    Check(NcResultKind(decodedNote.get()) == NC_NULL, "script JSON roundtrip nullable field");
    for (const char* invalid : {"missing", "fraction", "relaxed"})
    {
        Arguments bad(NcArgumentsCreate(), &NcArgumentsDestroy);
        Check(NcArgumentsString(bad.get(), invalid, Length(invalid)) != 0, "invalid JSON callback path");
        Result failure(Call(instance.get(), "Load", bad.get()), &NcResultDestroy);
        Status(failure.get(), NC_FAILED, "callback JSON shape rejected");
        Check(!Error(failure.get()).empty(), "callback JSON error retained");
        if (debug) Check(NcResultLine(failure.get()) == 3, "callback JSON failure source line");
        Int(NcReadGlobal(instance.get(), "loads", 5), 1, "failed callback does not execute following statement");
    }
    Arguments strict(NcArgumentsCreate(), &NcArgumentsDestroy);
    Check(NcArgumentsBool(strict.get(), 1) != 0, "strict JSON numeric policy");
    Rejected(Call(instance.get(), "Wide", strict.get()), "strict JSON rejects integer tokens outside int64");
    Arguments relaxed(NcArgumentsCreate(), &NcArgumentsDestroy);
    Check(NcArgumentsBool(relaxed.get(), 0) != 0, "relaxed JSON numeric policy");
    Result wide(Call(instance.get(), "Wide", relaxed.get()), &NcResultDestroy);
    Status(wide.get(), NC_COMPLETED, "relaxed JSON permits float64 fallback for wide integer tokens");
    Check(NcResultKind(wide.get()) == NC_FLOAT && NcResultFloat(wide.get()) == 9223372036854775808.0,
        "relaxed JSON preserves representable wide float value");
    Arguments malformed(NcArgumentsCreate(), &NcArgumentsDestroy);
    Check(NcArgumentsString(malformed.get(), "{}", 2) != 0, "invalid script JSON input");
    Rejected(Call(instance.get(), "Decode", malformed.get()), "script JSON rejects missing struct fields");
}

void BulkArrays()
{
    Options options(NcOptionsCreate(),&NcOptionsDestroy);
    Check(NcTypeCollection(options.get(),NC_ARRAY,NcTypeBuiltin(NC_INT),0,0)!=0,"register int64 array type");
    Check(NcTypeCollection(options.get(),NC_ARRAY,NcTypeBuiltin(NC_FLOAT),0,0)!=0,"register float64 array type");
    Check(NcTypeCollection(options.get(),NC_ARRAY32,NcTypeBuiltin(NC_INT),0,0)!=0,"register int32 array type");
    const uint32_t compact=NcTypeCollection(options.get(),NC_ARRAY32,NcTypeBuiltin(NC_FLOAT),0,0);
    Check(compact!=0,"register float32 array type");
    Host host;const uint32_t owner=NcSchemaObject(options.get(),"BufferHost",10);
    Check(NcOptionsSetResolver(options.get(),Resolve,&host,0)&&NcSchemaGlobal(options.get(),"Host",4,owner,7),"compact buffer host");
    Check(NcSchemaMethod(options.get(),owner,"Echo",4,compact,&compact,1,Echo,&host,0)!=0,"compact array host method signature");
    Program program=Compile("export array32<float> Echo(array32<float> a){return Host.Echo(a);}",false,options.get());
    Value i64=Capture(NcValueCreate(program.get(),"array<int>",10),NC_ARRAY,"create int64 array");
    Value f64=Capture(NcValueCreate(program.get(),"array<float>",12),NC_ARRAY,"create float64 array");
    Value i32=Capture(NcValueCreate(program.get(),"array32<int>",12),NC_ARRAY,"create int32 array");
    Value f32=Capture(NcValueCreate(program.get(),"array32<float>",14),NC_ARRAY,"create float32 array");
    for(NcValue* value:{i64.get(),f64.get(),i32.get(),f32.get()}) Done(NcValueResize(value,3),"prepare bulk array length");
    const int64_t ints[]={INT64_MIN,INT64_C(9007199254740993),INT64_MAX};int64_t intOut[3]={};
    const double doubles[]={-0.0,1.25,(std::numeric_limits<double>::max)()};double doubleOut[3]={};
    const int32_t narrowInts[]={INT32_MIN,123,INT32_MAX};int32_t narrowIntOut[3]={};
    const float floats[]={-0.0f,0.125f,(std::numeric_limits<float>::max)()};float floatOut[3]={};
    Done(NcArrayWriteInt64(i64.get(),0,ints,3),"bulk int64 write");Done(NcArrayReadInt64(i64.get(),0,intOut,3),"bulk int64 read");
    Done(NcArrayWriteFloat64(f64.get(),0,doubles,3),"bulk float64 write");Done(NcArrayReadFloat64(f64.get(),0,doubleOut,3),"bulk float64 read");
    Done(NcArrayWriteInt32(i32.get(),0,narrowInts,3),"bulk int32 write");Done(NcArrayReadInt32(i32.get(),0,narrowIntOut,3),"bulk int32 read");
    Done(NcArrayWriteFloat32(f32.get(),0,floats,3),"bulk float32 write");Done(NcArrayReadFloat32(f32.get(),0,floatOut,3),"bulk float32 read");
    Check(std::memcmp(ints,intOut,sizeof(ints))==0&&std::memcmp(doubles,doubleOut,sizeof(doubles))==0&&
        std::memcmp(narrowInts,narrowIntOut,sizeof(narrowInts))==0&&std::memcmp(floats,floatOut,sizeof(floats))==0,"bulk APIs preserve all storage bits");
    Instance instance=Create(program.get());Arguments compactArgs(NcArgumentsCreate(),&NcArgumentsDestroy);
    Check(NcArgumentsValue(compactArgs.get(),f32.get())!=0,"compact array typed argument");
    Value returned=Capture(Call(instance.get(),"Echo",compactArgs.get()),NC_ARRAY,"compact array typed return");
    Check(NcValueSameStorage(f32.get(),returned.get())!=0,"compact array identity crosses host call");
    Rejected(NcArrayWriteInt32(i64.get(),0,narrowInts,3),"bulk mismatched storage rejects conversion");
    Rejected(NcArrayWriteInt64(i64.get(),2,ints,2),"bulk write rejects complete out-of-bounds range");
    Rejected(NcArrayReadFloat32(f32.get(),UINT64_MAX,floatOut,1),"bulk read rejects wide offset");
    Rejected(NcArrayWriteInt64(i64.get(),0,0,1),"bulk write rejects null input");
    Done(NcArrayReadInt64(i64.get(),3,0,0),"empty bulk read at end");
    Int(NcValueAt(i64.get(),2),INT64_MAX,"rejected bulk write is atomic");
}

void HostStructObjects(bool debug)
{
    Host host;Options options(NcOptionsCreate(),&NcOptionsDestroy);
    Check(NcOptionsSetResolver(options.get(),Resolve,&host,0)!=0,"host struct resolver");
    uint32_t entity=NcSchemaObject(options.get(),"Entity",6);
    uint32_t bundle=NcSchemaStruct(options.get(),"Bundle",6);
    NcField fields[]={{"owner",5,entity},{"position",8,NcTypeBuiltin(NC_VECTOR3)}};
    Check(NcSchemaStructFields(options.get(),bundle,fields,2)!=0,"host struct object and vector fields");
    Check(NcSchemaProperty(options.get(),entity,"Value",5,NcTypeBuiltin(NC_INT),Getter,0,&host,0)!=0,"nested object property");
    Check(NcSchemaMethod(options.get(),entity,"Echo",4,bundle,&bundle,1,Echo,&host,0)!=0,"host struct return callback");
    Check(NcSchemaGlobal(options.get(),"Host",4,entity,7)!=0,"host struct service global");
    Program program=Compile("export Bundle Echo(Bundle b){return Host.Echo(b);}export int Read(Bundle b){return b.owner.Value+toint(b.position.x);}",debug,options.get());
    Arguments values(NcArgumentsCreate(),&NcArgumentsDestroy);const float position[]={2,3,4};
    Check(NcArgumentsObject(values.get(),"Entity",6,7)&&NcArgumentsVector(values.get(),NC_VECTOR3,position),"host struct typed fields");
    Value source=Capture(NcValueCreateFields(program.get(),"Bundle",6,values.get()),NC_STRUCT,"host struct with object and vector");
    Instance instance=Create(program.get());Arguments args(NcArgumentsCreate(),&NcArgumentsDestroy);NcArgumentsValue(args.get(),source.get());
    Value returned=Capture(Call(instance.get(),"Echo",args.get()),NC_STRUCT,"callback-produced typed struct");
    Value object=Capture(NcValueAt(returned.get(),0),NC_OBJECT,"returned struct object field");
    Check(NcValueObject(object.get())==7,"returned struct retains object ID");
    Value vector=Capture(NcValueAt(returned.get(),1),NC_VECTOR3,"returned struct vector field");
    float copied[3]={};Check(NcValueVector(vector.get(),copied,3)&&copied[0]==2&&copied[2]==4,"returned struct retains vector type");
    Int(Call(instance.get(),"Read",args.get()),42,"nested host object read");
    Arguments invalid(NcArgumentsCreate(),&NcArgumentsDestroy);NcArgumentsInt(invalid.get(),7);NcArgumentsVector(invalid.get(),NC_VECTOR3,position);
    Rejected(NcValueCreateFields(program.get(),"Bundle",6,invalid.get()),"wrong native field rejected before execution");
    host.valid=false;Rejected(Call(instance.get(),"Read",args.get()),"nested struct object ID still checked at runtime");
}

int32_t Initialize(void* user,NcHostCall* call)
{ ++static_cast<Host*>(user)->initializations;NcHostReturnInt(call,1);return 1; }
void OwnershipPreflight()
{
    Host host;Options options(NcOptionsCreate(),&NcOptionsDestroy);
    const uint32_t type=NcSchemaObject(options.get(),"InitHost",8);
    Check(NcOptionsSetResolver(options.get(),Resolve,&host,0)&&NcSchemaGlobal(options.get(),"Host",4,type,7),"preflight host registry");
    Check(NcSchemaMethod(options.get(),type,"Hit",3,NcTypeBuiltin(NC_INT),0,0,Initialize,&host,0)!=0,"preflight initializer callback");
    Program target=Compile("int initialized=Host.Hit();export int Read(list<int> x){return x[0];}export fn()->int Make(){int captured=initialized;return fn()->int{return captured;};}export int Take(fn()->int f){return f();}",true,options.get());
    Instance pending=Create(target.get());
    Program other=Compile("export list<int> Get(){return [1];}",false);Instance source=Create(other.get());
    Value foreign=Capture(Call(source.get(),"Get"),NC_LIST,"foreign list");
    Arguments wrongRuntime(NcArgumentsCreate(),&NcArgumentsDestroy);NcArgumentsValue(wrongRuntime.get(),foreign.get());
    Rejected(Call(pending.get(),"Read",wrongRuntime.get()),"foreign runtime rejected before initialization");
    Check(host.initializations==0,"foreign collection does not execute target initializer");
    Instance origin=Create(target.get());Value closure=Capture(Call(origin.get(),"Make"),NC_FUNCTION,"origin closure");
    Check(host.initializations==1,"origin initialization executed exactly once");
    Rejected(NcCallFunction(pending.get(),closure.get(),0,-1,0),"foreign instance closure rejected before initialization");
    Check(host.initializations==1,"foreign closure call does not execute target initializer");
    Arguments wrongInstance(NcArgumentsCreate(),&NcArgumentsDestroy);NcArgumentsValue(wrongInstance.get(),closure.get());
    Rejected(Call(pending.get(),"Take",wrongInstance.get()),"foreign closure argument rejected before initialization");
    Check(host.initializations==1,"foreign closure argument does not execute target initializer");
}
}

int main()
{
    try { for (bool debug : { false, true }) { Collections(debug); BindingsAndImages(debug); OutParameters(debug); Events(debug); ModulesAndHttp(debug); JsonAndValueCopies(debug); ScriptJsonAndHostReturn(debug); HostStructObjects(debug); } HttpCancellation(); BulkArrays(); OwnershipPreflight(); }
    catch (const std::exception& error) { std::fprintf(stderr, "NeoCore Unity integration: %s\n", error.what()); return 1; }
    std::printf("NeoCore Unity integration: %d checks passed\n", checks); return 0;
}
