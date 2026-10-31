#include "NeoCore.h"
#include "NeoCoreDebugAccess.h"
#include "NeoCoreHost.h"

#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
int checks = 0;
void Check(bool value, const std::string& message)
{
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void Integer(const NeoCore::Result& result, std::int64_t expected, const char* label)
{
    Check(result.ok() && !result.suspended() && result.value.kind() == NeoCore::Scalar::Kind::Int &&
        result.value.asInt() == expected, std::string(label) + ": " + result.error.message);
}
void Rejected(const NeoCore::Result& result, const char* label)
{
    Check(!result.ok() && result.status == NeoCore::ExecutionStatus::Failed && !result.error.message.empty(), label);
}
std::unique_ptr<NeoCore::Program> Compile(NeoCore::Runtime& runtime, const std::string& source,
    bool debug, const std::string& name = "shared.nc")
{
    NeoCore::Error error;
    auto program = runtime.Compile(source, error, name, debug);
    Check(program && error.ok(), "compile " + name + ": " + error.message);
    return program;
}
std::unique_ptr<NeoCore::Instance> Instance(NeoCore::Program& program)
{
    NeoCore::Error error;
    auto instance = program.CreateInstance(error);
    Check(instance && error.ok(), "create instance: " + error.message);
    return instance;
}
NeoScript::IRuntime* Backend(NeoCore::Program& program, NeoScript::ProgramHandle* image = nullptr)
{
    NeoScript::IRuntime* runtime = nullptr;
    NeoScript::ProgramHandle compiled;
    NeoScript::InstanceHandle instance;
    const NeoCore::StaticCompileResult* checked = nullptr;
    const NeoCore::ModuleBundle* bundle = nullptr;
    Check(NeoCore::DebugAccess::Get(program, runtime, compiled, instance, checked, bundle), "inspect shared backend");
    if (image) *image = compiled;
    return runtime;
}

void IndependentPrograms(bool debug)
{
    NeoCore::Error error;
    auto runtime = NeoCore::Runtime::Create(error);
    Check(runtime && error.ok(), "create shared runtime");
    auto first = Compile(*runtime,
        "int state=10;export int Step(){state+=1;return state;}export int Wait(){sleep(0);state+=10;return state;}", debug, "first.nc");
    auto second = Compile(*runtime,
        "int state=100;export int Step(){state+=2;return state;}export int Wait(){sleep(0);state+=20;return state;}", debug, "second.nc");
    NeoScript::ProgramHandle firstImage, secondImage;
    Check(Backend(*first, &firstImage) == Backend(*second, &secondImage), "programs use the same VM runtime");
    Check(firstImage.id != secondImage.id, "program handles are distinct");
    auto a = Instance(*first); auto aPeer = Instance(*first); auto b = Instance(*second);
    Check(a->Id() && aPeer->Id() && b->Id() && a->Id() != aPeer->Id() && a->Id() != b->Id() &&
        aPeer->Id() != b->Id(), "instance IDs are unique within shared runtime");
    Check(runtime->FindInstance(a->Id()) == a.get() && runtime->FindInstance(b->Id()) == b.get() &&
        !runtime->FindInstance(0), "runtime resolves live typed instances");
    int firstData = 1, secondData = 2;
    a->SetUserData(&firstData); b->SetUserData(&secondData);
    Check(a->GetUserData() == &firstData && b->GetUserData() == &secondData && !aPeer->GetUserData(), "user data is per instance");
    Integer(a->Call("Step"), 11, "first globals");
    Integer(aPeer->Call("Step"), 11, "same program independent globals");
    Integer(b->Call("Step"), 102, "second program globals");
    Check(a->Call("Wait").suspended(), "first program sleeps");
    auto third = Compile(*runtime, "export int Step(){return 303;}", debug, "third.nc");
    Integer(third->Call("Step"), 303, "compile while another program is suspended");
    Integer(b->Call("Step"), 104, "sibling runs while first sleeps");
    Integer(a->Resume(), 21, "first resumes its own code");
    auto invalid = runtime->Compile("export int Broken(){return \"bad\";}", error, "invalid.nc", debug);
    Check(!invalid && !error.ok(), "typed compile failure is reported");
    Integer(b->Call("Step"), 106, "compile failure does not poison existing program");
    auto recovered = Compile(*runtime, "export int Step(){return 404;}", debug, "recovered.nc");
    Integer(recovered->Call("Step"), 404, "compile succeeds after failure");
    const std::uint64_t id = a->Id();
    Check(a->Reset(error) && a->Id() == id && a->GetUserData() == &firstData && runtime->FindInstance(id) == a.get(),
        "reset preserves public identity and user data");
    Integer(a->Call("Step"), 11, "reset reinitializes only its instance");
    const std::uint64_t removed = aPeer->Id(); aPeer.reset();
    Check(!runtime->FindInstance(removed), "destroyed instance is no longer discoverable");
    first.reset();
    Integer(a->Call("Step"), 12, "instance retains released program");
    Integer(b->Call("Step"), 108, "releasing one program preserves another");
    auto privateProgram = NeoCore::Program::Compile("export int Step(){return 9;}", error);
    Check(privateProgram && Backend(*privateProgram) != Backend(*second), "compatibility compile uses a private runtime");
    auto privateInstance = Instance(*privateProgram);
    Check(privateInstance->Id() != a->Id() && privateInstance->Id() != b->Id() &&
        !runtime->FindInstance(privateInstance->Id()), "IDs are globally unique but lookup is runtime-local");
    runtime.reset(); second.reset(); third.reset(); recovered.reset();
    Integer(a->Call("Step"), 13, "child keeps runtime alive after wrapper destruction");
    Integer(b->Call("Step"), 110, "second child keeps shared runtime alive");
}

struct Transport : NeoCore::IHttpTransport
{
    NeoCore::HttpResponse Send(const NeoCore::HttpRequest&) override { return NeoCore::HttpResponse(); }
};
void OwnedValueLifetime(bool debug)
{
    NeoCore::CompileOptions options;
    std::shared_ptr<Transport> transport(new Transport());
    std::weak_ptr<Transport> retained = transport;
    options.httpTransport = transport;
    NeoCore::Error error;
    auto runtime = NeoCore::Runtime::Create(error, options);
    Check(runtime && error.ok(), "runtime owns configured transport");
    auto program = Compile(*runtime, "export list<int> Get(){return [7,8];}", debug);
    auto instance = Instance(*program);
    NeoCore::Result result = instance->Call("Get");
    Check(result.ok() && result.value.kind() == NeoCore::Scalar::Kind::List, "capture runtime-owned list");
    NeoCore::Scalar value = result.value; result = NeoCore::Result();
    transport.reset(); options.httpTransport.reset(); runtime.reset(); program.reset(); instance.reset();
    Check(!retained.expired() && value.At(0).asInt() == 7, "owned value retains runtime after all wrappers disappear");
    value.Append(NeoCore::Scalar::Int(9));
    Check(value.Count() == 3 && value.At(2).asInt() == 9, "retained value remains mutable");
    value = NeoCore::Scalar();
    Check(retained.expired(), "last owned value releases runtime services");
}

void Images(bool debug)
{
    NeoCore::Error error;
    auto runtime = NeoCore::Runtime::Create(error);
    Check(runtime && error.ok(), "image runtime");
    auto original = Compile(*runtime, "int n=3;export int Step(){n+=1;return n;}", debug, "image-source.nc");
    std::vector<std::uint8_t> bytes;
    Check(original->SaveImage(bytes, error), "save typed image: " + error.message);
    auto loaded = runtime->LoadImage(bytes, error);
    Check(loaded && error.ok() && Backend(*original) == Backend(*loaded), "source and image share backend");
    Integer(original->Call("Step"), 4, "original image state");
    Integer(original->Call("Step"), 5, "original advances");
    Integer(loaded->Call("Step"), 4, "loaded image has independent globals");
    auto corrupt = bytes; corrupt.back() ^= 1;
    Check(!runtime->LoadImage(corrupt, error) && !error.ok(), "corrupt image rejected");
    Integer(loaded->Call("Step"), 5, "bad image does not poison loaded program");
    original.reset();
    auto after = Compile(*runtime, "export int Step(){return 80;}", debug);
    Integer(after->Call("Step"), 80, "source compile after image load");
    auto peer = Instance(*loaded); runtime.reset(); loaded.reset(); after.reset();
    Integer(peer->Call("Step"), 4, "image instance retains shared runtime");
}

void TransportContract(bool debug)
{
    NeoCore::Error error;
    NeoCore::CompileOptions fixed, same, different;
    fixed.httpTransport.reset(new Transport()); same.httpTransport = fixed.httpTransport;
    different.httpTransport.reset(new Transport());
    auto runtime = NeoCore::Runtime::Create(error, fixed); Check(runtime && error.ok(), "fixed transport runtime");
    auto program = runtime->Compile("export int Value(){return 42;}", error, "transport.nc", debug, same);
    Check(program && error.ok(), "same transport is accepted for compile");
    auto inherited = Compile(*runtime, "export int Value(){return 43;}", debug);
    Check(!runtime->Compile("export int Value(){return 0;}", error, "wrong-transport.nc", debug, different) && !error.ok(),
        "compile cannot replace runtime transport");
    std::vector<std::uint8_t> image; Check(program->SaveImage(image, error), "transport contract image");
    Check(!runtime->LoadImage(image, error, different) && !error.ok(), "image load cannot replace runtime transport");
    auto sameImage = runtime->LoadImage(image, error, same); Check(sameImage && error.ok(), "same transport accepted for image");
    auto inheritedImage = runtime->LoadImage(image, error); Check(inheritedImage && error.ok(), "null transport inherits fixed service");
    Integer(program->Call("Value"), 42, "rejected service replacement preserves original");
    Integer(inherited->Call("Value"), 43, "inherited service source remains runnable");
}

struct Host
{
    NeoCore::HostSchema schema;
    NeoCore::TypeId type;
    std::unique_ptr<NeoCore::HostBindings> bindings;
    NeoCore::Runtime* runtime = nullptr;
    NeoCore::Instance* nested = nullptr;
    std::map<std::uint64_t, void*> users;
    std::map<std::uint64_t, std::int64_t> objects;
    std::function<void(NeoCore::HostCall&)> guarded;
    const NeoCore::Scalar* returned = nullptr;
    int initialized = 0;
    int callbacks = 0;
    static void* Resolve(void* data, NeoCore::TypeId type, NeoCore::ObjectHandle id)
    {
        Host& self = *static_cast<Host*>(data);
        const auto object = self.objects.find(id.value);
        return type == self.type && object != self.objects.end() ? &object->second : nullptr;
    }
    Host()
    {
        type = schema.AddObject("Entity");
        Check(type && schema.AddProperty(type, "Value", schema.Types().Int(), true, false), "host property schema");
        Check(schema.AddMethod(type, "Add", schema.Types().Int(), {schema.Types().Int()}) != 0, "host Add schema");
        for (const char* name : {"Hit", "Owner", "Bounce", "Guard"})
            Check(schema.AddMethod(type, name, schema.Types().Int(), {}) != 0, "host method schema");
        Check(schema.AddMethod(type, "Fetch", schema.Types().ListOf(schema.Types().Int()), {}) != 0, "host owned result schema");
        Check(schema.AddGlobal("Host", type), "host global schema");
        bindings.reset(new NeoCore::HostBindings(schema, &Resolve, this));
        objects[1] = 10; objects[2] = 20;
        for (const auto& binding : schema.Bindings())
        {
            const std::string name = binding.name;
            Check(bindings->Bind(binding.id, [this, name](NeoCore::HostCall& call) {
                ++callbacks;
                Check(call.UserData() == this, "shared binding user data is distinct from instance user data");
                NeoCore::Instance& owner = call.CallerInstance();
                if (runtime) Check(runtime->FindInstance(owner.Id()) == &owner, "callback lookup returns exact caller");
                Check(owner.IsExecuting(), "callback owner is executing");
                const auto expected = users.find(owner.Id());
                if (expected != users.end()) Check(call.InstanceUserData() == expected->second, "callback user data belongs to caller");
                if (name == "Value") call.ReturnInt(*static_cast<std::int64_t*>(call.Receiver()));
                else if (name == "Add")
                {
                    std::int64_t& value = *static_cast<std::int64_t*>(call.Receiver());
                    value += call.Int(0); call.ReturnInt(value);
                }
                else if (name == "Hit") call.ReturnInt(++initialized);
                else if (name == "Owner") call.ReturnInt(static_cast<std::int64_t>(owner.Id()));
                else if (name == "Guard") { Check(bool(guarded), "guard action configured"); guarded(call); }
                else if (name == "Fetch") { Check(returned != nullptr, "owned result configured"); call.ReturnValue(*returned); }
                else
                {
                    if (nested) Integer(nested->Call("Owner"), static_cast<std::int64_t>(nested->Id()), "nested other-program owner");
                    const auto local = call.CallScript("Local");
                    Check(local.ok() && !local.suspended(), "reenter exact script program");
                    Check(&call.CallerInstance() == &owner && call.InstanceUserData() == owner.GetUserData(), "outer callback context restored");
                    call.ReturnInt(local.value.asInt());
                }
                return true;
            }), "host callback binding");
        }
        Check(bindings->BindGlobal("Host", NeoCore::ObjectHandle(1)), "default host object");
    }
};

void HostInstances(bool debug)
{
    Host host; NeoCore::Error error;
    auto runtime = NeoCore::Runtime::Create(*host.bindings, error);
    Check(runtime && error.ok(), "runtime copies host bindings"); host.runtime = runtime.get();
    auto first = Compile(*runtime,
        "export int Read(){return Host.Value;}export int Add(int x){return Host.Add(x);}export int Owner(){return Host.Owner();}export int Bounce(){return Host.Bounce();}export int Local(){return 101;}", debug);
    auto second = Compile(*runtime,
        "export int Read(){return Host.Value+1000;}export int Add(int x){return Host.Add(x)+1000;}export int Owner(){return Host.Owner();}export int Local(){return 202;}", debug);
    auto a = Instance(*first); auto peer = Instance(*first); auto b = Instance(*second);
    int dataA = 1, dataPeer = 2, dataB = 3;
    for (const auto& item : {std::make_pair(a.get(), &dataA), std::make_pair(peer.get(), &dataPeer), std::make_pair(b.get(), &dataB)})
    { item.first->SetUserData(item.second); host.users[item.first->Id()] = item.second; }
    Check(peer->SetHostObject("Host", NeoCore::ObjectHandle(2), error) && b->SetHostObject("Host", NeoCore::ObjectHandle(2), error), "per-instance receiver IDs");
    Integer(a->Call("Read"), 10, "first host receiver"); Integer(peer->Call("Read"), 20, "peer host receiver");
    Integer(b->Call("Read"), 1020, "other program receiver and code");
    Integer(a->Call("Add", {NeoCore::Scalar::Int(3)}), 13, "argument plan registered once");
    Integer(b->Call("Add", {NeoCore::Scalar::Int(4)}), 1024, "second program argument plan intact");
    host.nested = b.get(); Integer(a->Call("Bounce"), 101, "nested callbacks preserve caller program"); host.nested = nullptr;
    std::vector<std::uint8_t> image; Check(first->SaveImage(image, error), "save host image");
    auto loaded = runtime->LoadImage(image, error); Check(loaded && error.ok(), "load host image in fixed schema runtime");
    Integer(loaded->Call("Add", {NeoCore::Scalar::Int(1)}), 14, "image reuses host registration without rebuilding plans");
    const auto removed = peer->Id(); peer.reset(); Check(!runtime->FindInstance(removed), "callback owner lookup expires");
    host.objects.erase(2); Rejected(b->Call("Read"), "ID lifetime validation remains runtime work");
    Integer(a->Call("Read"), 14, "invalid sibling object does not poison live receiver");
    host.bindings.reset(); host.runtime = nullptr; runtime.reset(); first.reset(); second.reset(); loaded.reset();
    Integer(a->Call("Add", {NeoCore::Scalar::Int(2)}), 16, "callbacks retained after runtime and binding wrappers die");
}

void ActiveRuntimeGuards(bool debug)
{
    Host host; NeoCore::Error error;
    auto runtime = NeoCore::Runtime::Create(*host.bindings, error); Check(runtime && error.ok(), "active guard runtime");
    host.runtime = runtime.get();
    auto outer = Compile(*runtime, "export int Guard(){return Host.Guard();}", debug);
    auto peer = Compile(*runtime, "export int Value(){return 42;}", debug);
    std::vector<std::uint8_t> image; Check(peer->SaveImage(image, error), "prepare guard image");
    host.guarded = [&](NeoCore::HostCall& call) {
        NeoCore::Error rejected;
        Check(!runtime->Compile("export int New(){return 7;}", rejected) && !rejected.ok(), "compile during callback rejected");
        Check(!runtime->LoadImage(image, rejected) && !rejected.ok(), "image load during callback rejected");
        std::vector<std::uint8_t> pending;
        Check(!outer->SaveImage(pending, rejected) && !rejected.ok(), "active program image save rejected");
        Check(!peer->SaveImage(pending, rejected) && !rejected.ok(), "idle peer image save rejected while runtime executes");
        Integer(peer->Call("Value"), 42, "existing peer can still execute reentrantly");
        call.ReturnInt(9);
    };
    Integer(outer->Call("Guard"), 9, "guard failures do not poison callback");
    auto next = Compile(*runtime, "export int Value(){return 43;}", debug);
    auto loaded = runtime->LoadImage(image, error); Check(loaded && error.ok(), "image load works after callback");
    Check(outer->SaveImage(image, error), "image save works after callback");
    Integer(next->Call("Value"), 43, "compile works after callback");
}

void ForeignValues(bool debug)
{
    Host host; NeoCore::Error error;
    auto runtime = NeoCore::Runtime::Create(*host.bindings, error); Check(runtime && error.ok(), "foreign-value runtime");
    host.runtime = runtime.get();
    auto origin = Compile(*runtime,
        "struct Record{int n;};export list<int> values=[7];export int Value(){return 111;}export list<int> Get(){return values;}export int Read(list<int> x){return x[0];}export fn()->int Make(){int n=5;return fn()->int{return n;};}export list<fn()->int> Functions(){return [Value];}export Record Data(){return Record(9);}", debug);
    auto target = Compile(*runtime,
        "struct Record{string n;};struct Container{list<int> values;};int initialized=Host.Hit();export list<list<int>> nested=[];export int Value(){return 222;}export int Read(list<int> x){return x[0];}export int Use(fn()->int f){return f();}export int Functions(list<fn()->int> f){return f[0]();}export string Data(Record r){return r.n;}export int Fetch(){return Host.Fetch()[0];}", debug);
    auto source = Instance(*origin); auto sibling = Instance(*origin); auto pending = Instance(*target);
    const auto list = source->Call("Get"); Check(list.ok(), "origin list");
    Integer(sibling->Call("Read", {list.value}), 7, "same-program collections retain sharing");
    const NeoCore::Scalar named = origin->FindFunction("Value", error); Check(error.ok(), "origin named function");
    Integer(sibling->Call(named), 111, "same-program named function can run in peer");
    const auto closure = source->Call("Make"); const auto functions = source->Call("Functions"); const auto record = source->Call("Data");
    Check(closure.ok() && functions.ok() && record.ok(), "origin owned values");
    Rejected(pending->Call("Read", {list.value}), "cross-program collection rejected");
    Rejected(pending->Call(named), "foreign named function cannot execute equal index");
    Rejected(pending->Call(closure.value), "foreign closure cannot execute");
    Rejected(pending->Call("Use", {named}), "foreign named function argument rejected");
    Rejected(pending->Call("Functions", {functions.value}), "nested foreign functions rejected");
    Rejected(pending->Call("Data", {record.value}), "same-name foreign struct rejected before layout access");
    Check(host.initialized == 0, "ownership rejection happens before target initialization");
    NeoCore::Scalar nested = target->CreateValue("list<list<int>>", error); Check(error.ok(), "target nested list");
    bool rejectedMutation = false;
    try { nested.Append(list.value); } catch (const std::exception&) { rejectedMutation = true; }
    Check(rejectedMutation && nested.Count() == 0, "nested collection cannot import a foreign program value");
    NeoCore::Scalar local = target->CreateValue("list<int>", error); Check(error.ok(), "target local list");
    nested.Append(local); rejectedMutation = false;
    try { nested.Set(0, list.value); } catch (const std::exception&) { rejectedMutation = true; }
    Check(rejectedMutation && nested.At(0).SameStorage(local), "rejected nested replacement preserves original element");
    target->CreateValue("Container", {list.value}, error);
    Check(!error.ok(), "struct construction cannot import foreign collection field");
    target->ToJson(list.value, error);
    Check(!error.ok(), "JSON conversion cannot adopt foreign program ownership");
    Integer(pending->Call("Value"), 222, "target remains usable after rejected values");
    Check(host.initialized == 1, "target initialized exactly once when actually invoked");
    host.returned = &list.value;
    Rejected(pending->Call("Fetch"), "host cannot return a foreign program collection");
    Integer(pending->Call("Value"), 222, "host result rejection preserves target state");
}
}

int main()
{
    try
    {
        for (bool debug : {false, true})
        { IndependentPrograms(debug); OwnedValueLifetime(debug); Images(debug); TransportContract(debug); HostInstances(debug); ActiveRuntimeGuards(debug); ForeignValues(debug); }
    }
    catch (const std::exception& error) { std::fprintf(stderr, "NeoCore shared runtime: %s\n", error.what()); return 1; }
    std::printf("NeoCore shared runtime: %d checks passed\n", checks);
    return 0;
}
