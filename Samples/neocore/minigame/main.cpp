#include "NeoCore.h"
#include "NeoCoreHost.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void Complete(const NeoCore::Result& result, const std::string& operation)
{
    Require(result.ok() && !result.suspended(), operation + ": " + result.error.message);
}

struct GameObject
{
    std::uint64_t id;
    NeoCore::Vector3Value position;
    GameObject(std::uint64_t value, float x) : id(value), position(x, 0, 0) {}
};

struct EventRecord
{
    std::uint64_t id;
    std::string name;
    std::int64_t updates;
};

class World
{
public:
    NeoCore::HostSchema schema;
    std::unique_ptr<NeoCore::HostBindings> bindings;
    std::unique_ptr<NeoCore::Runtime> runtime;
    std::map<std::uint64_t, GameObject> objects;
    std::vector<EventRecord> records;
    std::size_t resolutions = 0;

    World()
    {
        objectType_ = schema.AddObject("GameObject");
        serviceType_ = schema.AddObject("Services");
        Require(schema.AddProperty(objectType_, "Id", schema.Types().Int(), true, false), schema.LastError());
        Require(schema.AddProperty(objectType_, "Position", schema.Types().Vector3()), schema.LastError());
        Require(schema.AddEvent(objectType_, "OnUpdate", schema.Types().FunctionOf(schema.Types().Void(),
            {schema.Types().Float()})) != NeoCore::InvalidHostBinding, schema.LastError());
        Require(schema.AddEvent(objectType_, "OnEvent", schema.Types().FunctionOf(schema.Types().Void(),
            {schema.Types().Int()})) != NeoCore::InvalidHostBinding, schema.LastError());
        Require(schema.AddEvent(objectType_, "DelayedEvent", schema.Types().FunctionOf(schema.Types().Void(),
            {})) != NeoCore::InvalidHostBinding, schema.LastError());
        Require(schema.AddMethod(serviceType_, "Record", schema.Types().Void(),
            {schema.Types().Int(), schema.Types().String()}) != NeoCore::InvalidHostBinding, schema.LastError());
        Require(schema.AddGlobal("GameObject", objectType_), schema.LastError());
        Require(schema.AddGlobal("Services", serviceType_), schema.LastError());
        bindings.reset(new NeoCore::HostBindings(schema, &Resolve, this));
        for (const NeoCore::HostBinding& binding : schema.Bindings())
        {
            // 이름과 서명은 등록 시 한 번만 분기하고 실행 시에는 선택한 콜백을 직접 호출한다.
            bool bound;
            if (binding.ownerType == serviceType_)
                bound = bindings->Bind(binding.id, [this](NeoCore::HostCall& call) { return Record(call); });
            else if (!binding.eventName.empty())
            {
                const std::string eventName = binding.eventName;
                bound = bindings->Bind(binding.id, [this, eventName](NeoCore::HostCall& call) {
                    // 수신 객체와 콜백 소유 인스턴스는 서로 다를 수 있으므로 ID를 따로 보관한다.
                    Subscription entry = {call.CallerInstance().Id(), call.Argument(1)};
                    subscriptions_[std::make_pair(call.ReceiverId().value, eventName)] = entry;
                    return true;
                });
            }
            else if (binding.name == "Id")
                bound = bindings->Bind(binding.id, [](NeoCore::HostCall& call) {
                    GameObject* object = static_cast<GameObject*>(call.Receiver());
                    if (!object) return false;
                    call.ReturnInt(static_cast<std::int64_t>(object->id));
                    return true;
                });
            else if (binding.kind == NeoCore::HostBindingKind::Getter)
                bound = bindings->Bind(binding.id, [](NeoCore::HostCall& call) {
                    GameObject* object = static_cast<GameObject*>(call.Receiver());
                    if (!object) return false;
                    call.ReturnVector3(object->position);
                    return true;
                });
            else
                bound = bindings->Bind(binding.id, [](NeoCore::HostCall& call) {
                    GameObject* object = static_cast<GameObject*>(call.Receiver());
                    if (!object) return false;
                    object->position = call.Vector3(0);
                    return true;
                });
            Require(bound, "bind " + binding.name);
        }
        // 각 인스턴스는 실행 전에 GameObject를 자기 ID로 덮어쓴다. 서비스 ID만 공유한다.
        Require(bindings->BindGlobal("GameObject", NeoCore::ObjectHandle(1)), "default object binding");
        Require(bindings->BindGlobal("Services", NeoCore::ObjectHandle(ServiceId)), "service binding");
        NeoCore::Error error;
        runtime = NeoCore::Runtime::Create(*bindings, error);
        Require(bool(runtime), "create shared runtime: " + error.message);
    }

    std::uint64_t Create(float x)
    {
        // 파괴된 ID를 재사용하지 않으므로 저장된 핸들이 새 객체를 가리키지 않는다.
        const std::uint64_t id = nextId_++;
        objects.insert(std::make_pair(id, GameObject(id, x)));
        return id;
    }

    void Destroy(std::uint64_t id)
    {
        objects.erase(id);
        for (auto it = subscriptions_.begin(); it != subscriptions_.end();)
            if (it->first.first == id) it = subscriptions_.erase(it);
            else ++it;
    }

    void CancelEvents(std::uint64_t instanceId)
    {
        // Reset은 인스턴스 ID를 유지한다. 리셋·재로드·파괴 전에 이전 등록을 명시적으로 해제한다.
        for (auto it = subscriptions_.begin(); it != subscriptions_.end();)
            if (it->second.instanceId == instanceId) it = subscriptions_.erase(it);
            else ++it;
    }

    NeoCore::Result Dispatch(std::uint64_t receiverId, const std::string& eventName,
        const std::vector<NeoCore::Scalar>& arguments = {})
    {
        const auto found = subscriptions_.find(std::make_pair(receiverId, eventName));
        NeoCore::Instance* instance = found == subscriptions_.end() ? nullptr :
            runtime->FindInstance(found->second.instanceId);
        if (objects.find(receiverId) == objects.end() || !instance)
        {
            NeoCore::Result result;
            result.status = NeoCore::ExecutionStatus::Failed;
            result.error.message = "event receiver, registration, or caller instance no longer exists";
            return result;
        }
        // 콜백이 자기 등록을 바꾸어도 현재 함수 값은 호출이 끝날 때까지 보관한다.
        const NeoCore::Scalar callback = found->second.callback;
        return instance->Call(callback, arguments);
    }

private:
    static const std::uint64_t ServiceId = 1000000;
    std::uint64_t nextId_ = 1;
    NeoCore::TypeId objectType_ = NeoCore::InvalidType;
    NeoCore::TypeId serviceType_ = NeoCore::InvalidType;
    struct Subscription
    {
        std::uint64_t instanceId;
        NeoCore::Scalar callback;
    };
    std::map<std::pair<std::uint64_t, std::string>, Subscription> subscriptions_;

    static void* Resolve(void* data, NeoCore::TypeId type, NeoCore::ObjectHandle handle)
    {
        World& world = *static_cast<World*>(data);
        ++world.resolutions;
        if (type == world.serviceType_ && handle.value == ServiceId) return &world;
        const auto found = world.objects.find(handle.value);
        return type == world.objectType_ && found != world.objects.end() ? &found->second : nullptr;
    }

    bool Record(NeoCore::HostCall& call)
    {
        // 서비스에서 스크립트로 재진입해도 서비스를 호출한 인스턴스의 상태를 읽는다.
        const std::uint64_t id = static_cast<std::uint64_t>(call.Int(0));
        const std::string event = call.String(1);
        const NeoCore::Result count = call.CallScript("UpdateCount");
        if (!count.ok() || count.suspended()) return false;
        EventRecord record = {id, event, count.value.asInt()};
        records.push_back(record);
        return true;
    }
};

std::int64_t ReadInt(NeoCore::Instance& instance, const char* name)
{
    const NeoCore::Result result = instance.Call(name);
    Complete(result, name);
    return result.value.asInt();
}

void Frame(World& world, const std::vector<std::uint64_t>& ids,
    std::vector<std::unique_ptr<NeoCore::Instance>>& instances)
{
    for (std::size_t i = 0; i < instances.size(); ++i)
    {
        const auto& instance = instances[i];
        // 한 인스턴스는 동시에 하나의 실행만 소유한다. 대기 중 이벤트는 엔진 큐에 둔다.
        if (instance->IsSuspended()) Complete(instance->Resume(0.25), "resume frame");
        else Complete(world.Dispatch(ids[i], "OnUpdate", {NeoCore::Scalar::Float(0.25)}), "update frame");
    }
}

void Verify(const std::string& filename)
{
    std::ifstream input(filename.c_str(), std::ios::binary);
    Require(bool(input), "could not open " + filename);
    const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    World world;
    const std::size_t count = 3;
    std::vector<std::uint64_t> ids;
    for (std::size_t i = 0; i < count; ++i) ids.push_back(world.Create(static_cast<float>(i * 10)));

    // 컴파일은 한 번만 한다. 아래 모든 인스턴스가 같은 프로그램을 사용한다.
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = world.runtime->Compile(source, error, filename, true);
    Require(bool(program), "compile: " + error.message);
    program->FindFunction("OnUpdate", error);
    Require(!error.ok(), "registered callbacks do not need export");
    std::vector<std::unique_ptr<NeoCore::Instance>> instances;
    for (std::size_t i = 0; i < count; ++i)
    {
        std::unique_ptr<NeoCore::Instance> instance = program->CreateInstance(error);
        Require(bool(instance), "create instance: " + error.message);
        Require(instance->SetHostObject("GameObject", NeoCore::ObjectHandle(ids[i]), error), error.message);
        Complete(instance->Run(), "global initialization");
        Require(ReadInt(*instance, "UpdateCount") == 0, "registration must not invoke callbacks during initialization");
        const NeoCore::Result origin = instance->Call("OriginX");
        Complete(origin, "read origin");
        Require(origin.value.asFloat() == static_cast<double>(i * 10), "per-instance global binding before init");
        instances.push_back(std::move(instance));
    }

    Frame(world, ids, instances);
    Frame(world, ids, instances);
    Complete(world.Dispatch(ids[1], "OnEvent", {NeoCore::Scalar::Int(3)}), "dispatch event");
    NeoCore::Result waiting = world.Dispatch(ids[0], "DelayedEvent");
    Require(waiting.ok() && waiting.suspended() && instances[0]->IsSuspended(), "sleep yields to the engine");
    const NeoCore::Result busy = world.Dispatch(ids[0], "OnEvent", {NeoCore::Scalar::Int(99)});
    Require(!busy.ok() && instances[0]->IsSuspended(), "busy event must not replace sleeping invocation");
    Frame(world, ids, instances);
    Frame(world, ids, instances);

    for (std::size_t i = 0; i < count; ++i)
    {
        Require(ReadInt(*instances[i], "UpdateCount") == (i == 0 ? 3 : 4), "independent update counters");
        Require(ReadInt(*instances[i], "EventCount") == (i == 1 ? 1 : 0), "event isolation");
        Require(ReadInt(*instances[i], "WakeCount") == (i == 0 ? 1 : 0), "sleep resume isolation");
        const float expectedX[] = {0.75f, 12.0f, 21.0f};
        Require(world.objects.at(ids[i]).position.x == expectedX[i], "per-instance position write");
    }
    Require(world.records.size() == 2 && world.records[0].id == ids[1] && world.records[0].name == "event"
        && world.records[1].id == ids[0] && world.records[1].name == "awake", "service dispatch retains invoking object");
    Require(world.records[0].updates == 2 && world.records[1].updates == 2, "service reentry uses invoking instance");

    // 프로그램 팩토리의 수명과 실행 인스턴스의 수명을 분리한다.
    program.reset();
    Complete(world.Dispatch(ids[1], "OnUpdate", {NeoCore::Scalar::Float(0.25)}), "instance retains compiled program");
    Require(ReadInt(*instances[1], "UpdateCount") == 5, "shared program survives factory destruction");

    waiting = world.Dispatch(ids[2], "DelayedEvent");
    Require(waiting.ok() && waiting.suspended(), "sleep before object destruction");
    world.Destroy(ids[2]);
    Require(!world.Dispatch(ids[2], "OnUpdate", {NeoCore::Scalar::Float(0.25)}).ok(), "destroyed object has no retained callbacks");
    const std::uint64_t replacement = world.Create(90.0f);
    Require(instances[2]->SetHostObject("GameObject", NeoCore::ObjectHandle(replacement), error), error.message);
    const NeoCore::Result destroyed = instances[2]->Resume(0.25);
    Require(!destroyed.ok() && !instances[2]->IsSuspended(), "saved object ID fails after destruction");
    Require(destroyed.error.line > 0 && destroyed.error.sourceName == filename, "resume error retains script location");
    Require(world.objects.at(replacement).position.y == 0.0f, "old ID must not redirect to replacement");
    Require(ReadInt(*instances[0], "UpdateCount") == 3, "failed neighbor does not corrupt another instance");

    // 다른 객체에 등록한 함수도 그 객체의 스크립트가 아니라 등록한 인스턴스에서 실행한다.
    Complete(instances[0]->Call("SubscribeTo", {NeoCore::Scalar::Object("GameObject", ids[1])}), "cross-object registration");
    Complete(world.Dispatch(ids[1], "OnEvent", {NeoCore::Scalar::Int(7)}), "cross-object event");
    Require(ReadInt(*instances[0], "EventCount") == 1 && ReadInt(*instances[1], "EventCount") == 1,
        "event receiver must not replace the callback's caller instance");
    Require(world.records.back().id == ids[0], "cross-object callback retains its GameObject global");
    const std::uint64_t resetId = instances[0]->Id();
    world.CancelEvents(resetId);
    Require(instances[0]->Reset(error), "reset instance: " + error.message);
    Require(instances[0]->Id() == resetId, "reset keeps instance identity");
    Require(!world.Dispatch(ids[1], "OnEvent", {NeoCore::Scalar::Int(7)}).ok(), "reset unregisters old callbacks");
    Require(instances[0]->SetHostObject("GameObject", NeoCore::ObjectHandle(ids[0]), error), error.message);
    Complete(instances[0]->Run(), "register callbacks after reset");
    Complete(world.Dispatch(ids[0], "OnUpdate", {NeoCore::Scalar::Float(0.25)}), "fresh callback after reset");
    Require(ReadInt(*instances[0], "UpdateCount") == 1 && ReadInt(*instances[0], "EventCount") == 0,
        "reset callbacks use fresh instance state");

    // 대기 중 인스턴스 파괴도 다른 인스턴스의 호출 프레임을 건드리지 않는다.
    waiting = world.Dispatch(ids[0], "DelayedEvent");
    Require(waiting.ok() && waiting.suspended(), "sleep before instance destruction");
    world.CancelEvents(instances[0]->Id());
    instances[0].reset();
    Require(world.runtime->FindInstance(resetId) == nullptr, "destroyed caller ID is invalid");
    Require(!world.Dispatch(ids[0], "OnUpdate", {NeoCore::Scalar::Float(0.25)}).ok(), "destroyed instance has no retained callbacks");
    Require(ReadInt(*instances[1], "UpdateCount") == 5, "pending invocation teardown is isolated");
    for (const auto& instance : instances)
        if (instance) world.CancelEvents(instance->Id());
    instances.clear();
    std::printf("NeoCore minigame: all checks passed (1 program, %zu instances, frame/event/sleep/ID lifetime)\n", count);
}
}

int RunMotion(int argc, char** argv);

int main(int argc, char** argv)
{
    try
    {
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) == "--motion" || std::string(argv[i]) == "--record")
                return RunMotion(argc, argv);
        std::string filename = NEOCORE_MINIGAME_SCRIPT;
        for (int i = 1; i < argc; ++i)
        {
            const std::string option = argv[i];
            if (option == "--verify") continue;
            if (option == "--file" && i + 1 < argc) filename = argv[++i];
            else throw std::runtime_error("usage: neocore_minigame [--verify] [--file mover.nc]");
        }
        Verify(filename);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "NeoCore minigame failed: %s\n", error.what());
        return 1;
    }
}
