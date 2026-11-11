#include "NeoCore.h"
#include "NeoCoreCompiler.h"
#include "NeoCoreHost.h"
#include "NeoCoreHostSchemaIO.h"

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

// 호스트 API 의 Out 매개변수: 스키마 등록, 서명 검사, 호출 뒤 되가져오기, 이미지·스키마 파일 계약을 확인한다.
namespace
{
int checks = 0, failures = 0;
void Check(bool condition, const std::string& text)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", text.c_str()); }
}

using NeoCore::ApiParameterMode;
using NeoCore::HostCall;
using NeoCore::TypeId;

const ApiParameterMode In = ApiParameterMode::In;
const ApiParameterMode Out = ApiParameterMode::Out;

struct Fixture
{
    NeoCore::HostSchema schema;
    std::unique_ptr<NeoCore::HostBindings> bindings;
    TypeId store = NeoCore::InvalidType, item = NeoCore::InvalidType;
    std::map<std::string, std::int64_t> numbers;
    NeoCore::Scalar savedItem, savedList;
    int calls = 0;

    static void* Resolve(void* data, TypeId, NeoCore::ObjectHandle id) { return id.value == 1 || id.value == 2 ? data : nullptr; }

    NeoCore::HostBindingId Add(const char* name, TypeId result, const std::vector<TypeId>& parameters,
        const std::vector<std::string>& names, const std::vector<ApiParameterMode>& modes, NeoCore::HostFunction function)
    {
        const NeoCore::HostBindingId id = schema.AddMethod(store, name, result, parameters, names, modes);
        Check(id != NeoCore::InvalidHostBinding, std::string("register ") + name + ": " + schema.LastError());
        functions.push_back(std::make_pair(id, function));
        return id;
    }
    std::vector<std::pair<NeoCore::HostBindingId, NeoCore::HostFunction>> functions;

    explicit Fixture(bool withOutputs = true)
    {
        NeoCore::TypeRegistry& types = schema.Types();
        store = schema.AddObject("Store");
        item = types.DeclareStruct("Item");
        Check(types.DefineStructFields(item, {{"count", types.Int()}, {"tags", types.ListOf(types.String())}}), "define host item");
        const TypeId ints = types.ListOf(types.Int());
        numbers["alpha"] = 7;
        const std::vector<ApiParameterMode> keyOut = withOutputs ? std::vector<ApiParameterMode>{In, Out} : std::vector<ApiParameterMode>();
        Add("TryGetInt", types.Bool(), {types.String(), types.Int()}, {"key", "value"}, keyOut, [this](HostCall& call) {
            ++calls;
            const auto found = call.IsNull(0) ? numbers.end() : numbers.find(call.String(0));
            if (found != numbers.end()) call.OutInt(1, found->second);
            call.ReturnBool(found != numbers.end());
            return true; });
        if (!withOutputs)
        {
            Finish();
            return;
        }
        Add("TryGetText", types.Bool(), {types.String(), types.String()}, {"key", "value"}, {In, Out}, [](HostCall& call) {
            if (call.String(0) == "null") { call.OutNull(1); call.ReturnBool(true); return true; }
            const bool found = call.String(0) == "name";
            // 현재 값도 일반 인자처럼 읽을 수 있다.
            if (found) call.OutString(1, (call.IsNull(1) ? std::string("<null>") : call.String(1)) + "+host");
            call.ReturnBool(found);
            return true; });
        Add("TryGetFloat", types.Bool(), {types.Float()}, {"value"}, {Out}, [](HostCall& call) { call.OutFloat(0, 2.5); call.ReturnBool(true); return true; });
        Add("TryGetFlag", types.Bool(), {types.Bool()}, {"value"}, {Out}, [](HostCall& call) { call.OutBool(0, true); call.ReturnBool(true); return true; });
        Add("TryGetVector", types.Bool(), {types.Bool(), types.Vector3()}, {"hit", "value"}, {In, Out}, [](HostCall& call) {
            if (call.Bool(0)) call.OutVector3(1, NeoCore::Vector3Value(10, 20, 30));
            call.ReturnBool(call.Bool(0));
            return true; });
        Add("TryGetQuaternion", types.Bool(), {types.Quaternion()}, {"value"}, {Out}, [](HostCall& call) {
            call.OutQuaternion(0, NeoCore::QuaternionValue(1, 2, 3, 4)); call.ReturnBool(true); return true; });
        Add("TryGetStore", types.Bool(), {types.Bool(), store}, {"hit", "value"}, {In, Out}, [](HostCall& call) {
            if (call.Bool(0)) call.OutObject(1, NeoCore::ObjectHandle(2)); else call.OutNull(1);
            call.ReturnBool(true);
            return true; });
        Add("Remember", types.Void(), {item, ints}, {"value", "values"}, {}, [this](HostCall& call) {
            savedItem = call.Argument(0); savedList = call.Argument(1); return true; });
        Add("TryGetItem", types.Bool(), {types.Bool(), item}, {"hit", "value"}, {In, Out}, [this](HostCall& call) {
            if (call.Bool(0)) call.OutValue(1, savedItem);
            call.ReturnBool(call.Bool(0));
            return true; });
        Add("TryGetList", types.Bool(), {ints}, {"values"}, {Out}, [this](HostCall& call) { call.OutValue(0, savedList); call.ReturnBool(true); return true; });
        // 반환이 없고 Out 이 둘인 메서드.
        Add("Split", types.Void(), {types.Int(), types.Int(), types.Int()}, {"value", "quotient", "remainder"}, {In, Out, Out}, [](HostCall& call) {
            call.OutInt(1, call.Int(0) / 10); call.OutInt(2, call.Int(0) % 10); return true; });
        Add("WriteInput", types.Int(), {types.Int()}, {"value"}, {}, [](HostCall& call) { call.OutInt(0, 1); call.ReturnInt(0); return true; });
        Add("WrongKind", types.Bool(), {types.Int()}, {"value"}, {Out}, [](HostCall& call) { call.OutString(0, "text"); call.ReturnBool(true); return true; });
        Add("FailAfterOut", types.Bool(), {types.Int()}, {"value"}, {Out}, [](HostCall& call) { call.OutInt(0, 99); call.Fail("host rejected the request"); return false; });
        Finish();
    }

    void Finish()
    {
        Check(schema.AddGlobal("Host", store), "register host global");
        bindings.reset(new NeoCore::HostBindings(schema, &Resolve, this));
        for (const auto& entry : functions) Check(bindings->Bind(entry.first, entry.second), "bind host method");
        Check(bindings->BindGlobal("Host", NeoCore::ObjectHandle(1)), "bind host global");
        std::string error;
        Check(bindings->Validate(error), "complete host bindings: " + error);
    }
};

void ExpectInt(Fixture& host, const std::string& source, std::int64_t expected, const char* label, bool debug)
{
    NeoCore::Error error;
    auto program = NeoCore::Program::Compile(source, *host.bindings, error, "host_out.nc", debug);
    Check(bool(program), std::string(label) + " compiles: " + error.message);
    if (!program) return;
    const NeoCore::Result result = program->Call("Probe");
    Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::Int && result.value.asInt() == expected,
        std::string(label) + ": " + (result.ok() ? std::to_string(result.value.asInt()) : result.error.message));
}

void Values(bool debug)
{
    Fixture host;
    ExpectInt(host, "export int Probe(){int v=99;if(!Host.TryGetInt(\"alpha\",out v))return -1;int a=v;if(Host.TryGetInt(\"missing\",out v))return -2;return a*100+v;}", 707,
        "host out hits and misses preserve the variable", debug);
    ExpectInt(host, "int g=5;export int Probe(){Host.TryGetInt(\"alpha\",out g);return g;}", 7, "host out writes a script global", debug);
    ExpectInt(host, "export int Probe(){string s=\"old\";if(!Host.TryGetText(\"name\",out s))return -1;if(s!=\"old+host\")return -2;"
        "if(Host.TryGetText(\"none\",out s)||s!=\"old+host\")return -3;if(!Host.TryGetText(\"null\",out s)||s!=null)return -4;return 1;}", 1,
        "host out strings, current value and null", debug);
    ExpectInt(host, "export int Probe(){float f=0.0;bool b=false;if(!Host.TryGetFloat(out f)||!Host.TryGetFlag(out b))return -1;return b&&f==2.5?1:0;}", 1,
        "host out float and bool", debug);
    ExpectInt(host, "export int Probe(){Vector3 v=Vector3(1,2,3);v.x=4;Host.TryGetVector(false,out v);float miss=v.x+v.z;"
        "Host.TryGetVector(true,out v);v.y=5;return toint(miss*1000+v.x+v.y+v.z);}", 7045,
        "host out vector with a component-written local", debug);
    ExpectInt(host, "export int Probe(){Quaternion q=Quaternion();Host.TryGetQuaternion(out q);return toint(q.w*1000+q.x*100+q.y*10+q.z);}", 1234,
        "host out quaternion keeps w,x,y,z", debug);
    ExpectInt(host, "export int Probe(){Store other=null;if(!Host.TryGetStore(true,out other)||other==null||other==Host)return -1;"
        "Host.TryGetStore(false,out other);return other==null?1:0;}", 1, "host out native object and null", debug);
    ExpectInt(host, "export int Probe(){list<string> tags=[\"a\"];list<int> values=[3];Host.Remember(Item(2,tags),values);"
        "Item v=Item(0,[]);if(Host.TryGetItem(false,out v)||v.count!=0)return -1;if(!Host.TryGetItem(true,out v))return -2;"
        "v.count=9;Item again=Item(0,[]);Host.TryGetItem(true,out again);list<int> got=[];Host.TryGetList(out got);"
        "return v.count*100+again.count*10+got[0];}", 923, "host out struct copies by value and lists arrive", debug);
    ExpectInt(host, "export int Probe(){int q=0;int r=0;Host.Split(47,out q,out r);return q*10+r;}", 47, "void host method with two out parameters", debug);
    ExpectInt(host, "export int Probe(){int v=3;fn()->int f=fn()->int{Host.TryGetInt(\"alpha\",out v);return v;};return f()*10+v;}", 73,
        "host out inside a closure writes the closure's copy", debug);
    ExpectInt(host, "int Key(){return 1;}export int Probe(){int v=0;int n=0;for(int i in 0,5){if(Host.TryGetInt(i==2?\"missing\":\"alpha\",out v))n+=v;}return n;}", 28,
        "host out in a loop with a conditional argument", debug);

    const struct { const char* source; const char* message; } runtimeFailures[] = {
        {"export int Probe(){return Host.WriteInput(1);}", "is not an out parameter"},
        {"export int Probe(){int v=0;Host.WrongKind(out v);return v;}", "disagrees with the declared signature"},
        {"export int Probe(){int v=4;Host.FailAfterOut(out v);return v;}", "host rejected the request"},
        {"export int Probe(){Store s=null;int v=0;s.TryGetInt(\"alpha\",out v);return v;}", "null host object"}
    };
    for (const auto& test : runtimeFailures)
    {
        NeoCore::Error error;
        auto program = NeoCore::Program::Compile(test.source, *host.bindings, error, "host_out_fail.nc", debug);
        Check(bool(program), std::string("runtime failure case compiles: ") + error.message);
        if (!program) continue;
        const NeoCore::Result result = program->Call("Probe");
        Check(!result.ok() && result.error.message.find(test.message) != std::string::npos,
            std::string("host out runtime failure: ") + test.source + " => " + result.error.message);
    }
    // 실패한 호출 뒤에도 값이 새지 않고 다음 호출이 정상이다.
    ExpectInt(host, "export int Probe(){string s=\"keep\";Host.TryGetText(\"name\",out s);return s==\"keep+host\"?1:0;}", 1,
        "host out works after failed calls", debug);
}

void Rejections(bool debug)
{
    Fixture host;
    const struct { const char* source; const char* diagnostic; } invalid[] = {
        {"export int Probe(){int v=0;Host.TryGetInt(\"a\",v);return v;}", "'Store.TryGetInt' argument 2 'value' is an out parameter"},
        {"export int Probe(){float v=0.0;Host.TryGetInt(\"a\",out v);return 0;}", "requires an out variable of type 'int', not 'float'"},
        {"export int Probe(){int v=0;Host.TryGetInt(out v,v);return v;}", "'Store.TryGetInt' argument 1 'key' is not an out parameter"},
        {"struct S{int x;};export int Probe(){S s=S();Host.TryGetInt(\"a\",out s.x);return 0;}", "requires a variable after 'out'"},
        {"export int Probe(){list<int> v=[0];Host.TryGetInt(\"a\",out v[0]);return 0;}", "requires a variable after 'out'"},
        {"const int v=0;export int Probe(){Host.TryGetInt(\"a\",out v);return 0;}", "cannot modify a const value"},
        {"export int Probe(){int v=0;Host.TryGetInt(\"a\");return v;}", "'Store.TryGetInt' requires 2 arguments, not 1"},
        {"export int Probe(){Host.TryGetStore(true,out Host);return 0;}", "host global handles cannot be rebound"},
        {"export int Probe(){int q=0;Host.Split(1,out q,q);return q;}", "'Store.Split' argument 3 'remainder' is an out parameter"},
        {"void F(out int x){}export int Probe(){return 0;}", "out parameters belong to registered API signatures only"}
    };
    for (const auto& test : invalid)
    {
        NeoCore::Error error;
        auto program = NeoCore::Program::Compile(test.source, *host.bindings, error, "host_out_error.nc", debug);
        Check(!program && error.message.find(test.diagnostic) != std::string::npos,
            std::string("host out rejected: ") + test.source + " => " + error.message);
    }
}

void Contracts(bool debug)
{
    // 등록 규칙.
    {
        NeoCore::HostSchema schema;
        NeoCore::TypeRegistry& types = schema.Types();
        const TypeId owner = schema.AddObject("Owner");
        const std::size_t before = schema.Bindings().size();
        Check(schema.AddMethod(owner, "A", types.Void(), {types.Int()}, {"x", "y"}) == NeoCore::InvalidHostBinding, "name count must match");
        Check(schema.AddMethod(owner, "A", types.Void(), {types.Int()}, {}, {In, Out}) == NeoCore::InvalidHostBinding, "mode count must match");
        Check(schema.AddMethod(owner, "A", types.Void(), {types.Int()}, {"if"}) == NeoCore::InvalidHostBinding, "parameter names are identifiers, not keywords");
        const std::vector<TypeId> many(32, types.Int());
        std::vector<ApiParameterMode> modes(32, In);
        modes[31] = Out;
        Check(schema.AddMethod(owner, "A", types.Void(), many, {}, modes) == NeoCore::InvalidHostBinding, "out parameters are limited to the first 31");
        Check(schema.Bindings().size() == before, "rejected registrations consume no binding id");
        const NeoCore::HostBindingId id = schema.AddMethod(owner, "TryGet", types.Bool(), {types.String(), types.Int()}, {"key", "value"}, {In, Out});
        Check(id != NeoCore::InvalidHostBinding, "register a method with an out parameter: " + schema.LastError());
        const NeoCore::HostBinding* binding = schema.FindBinding(id);
        Check(binding && binding->parameterNames.size() == 2 && binding->parameterNames[1] == "value" &&
            binding->parameterModes.size() == 2 && binding->parameterModes[1] == Out, "the binding keeps names and modes");
        std::string error;
        Check(schema.Validate(error), "schema with an out parameter validates: " + error);
    }
    // 스키마 파일: 이름·방향이 왕복하고, 그 필드가 없는 예전 파일도 읽힌다.
    {
        Fixture host;
        NeoCore::HostSchemaDocument document;
        document.schema = host.schema;
        std::string json, error;
        Check(document.ToJson(json, error), "export schema with out parameters: " + error);
        Check(json.find("\"parameterModes\"") != std::string::npos && json.find("\"parameterNames\"") != std::string::npos, "schema file records names and modes");
        NeoCore::HostSchemaDocument restored;
        Check(NeoCore::HostSchemaDocument::FromJson(json, restored, error), "import schema with out parameters: " + error);
        const NeoCore::HostBinding* original = host.schema.FindMethod(host.store, "Split");
        const NeoCore::HostBinding* copy = restored.schema.FindMethod(restored.schema.Types().LookupNamed("Store"), "Split");
        Check(original && copy && copy->id == original->id && copy->parameterModes == original->parameterModes &&
            copy->parameterNames == original->parameterNames, "restored schema keeps the same names and modes");
        std::string again;
        Check(restored.ToJson(again, error) && again == json, "schema with out parameters has a canonical encoding");
        Fixture plain(false);
        NeoCore::HostSchemaDocument old;
        old.schema = plain.schema;
        // 이름만 있고 방향이 없는 스키마는 모두 입력이다.
        Check(old.ToJson(json, error) && json.find("\"parameterModes\"") == std::string::npos, "input-only schema writes no mode field");
        Check(NeoCore::HostSchemaDocument::FromJson(json, restored, error), "input-only schema imports: " + error);
    }
    // 이미지: 저장·로드가 왕복하고, Out 계약이 다른 호스트에서는 로드가 거부된다.
    {
        Fixture host;
        NeoCore::Error error;
        auto program = NeoCore::Program::Compile("export int Probe(){int v=1;Host.TryGetInt(\"alpha\",out v);return v;}",
            *host.bindings, error, "host_out_image.nc", debug);
        Check(bool(program), "compile image program: " + error.message);
        if (program)
        {
            std::vector<std::uint8_t> image;
            Check(program->SaveImage(image, error), "save image: " + error.message);
            auto loaded = NeoCore::Program::LoadImage(image, *host.bindings, error);
            Check(bool(loaded), "load image with the same host contract: " + error.message);
            if (loaded) { const NeoCore::Result result = loaded->Call("Probe"); Check(result.ok() && result.value.asInt() == 7, "image host out result: " + result.error.message); }
            Fixture plain(false);
            auto rejected = NeoCore::Program::LoadImage(image, *plain.bindings, error);
            Check(!rejected, "an image compiled against out parameters is rejected by a host without them");
        }
    }
}
}

int main()
{
    for (int mode = 0; mode < 2; ++mode)
    {
        const bool debug = mode != 0;
        Values(debug);
        Rejections(debug);
        Contracts(debug);
    }
    std::printf("NeoCore host out regression: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
