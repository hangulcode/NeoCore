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
    std::map<std::uint64_t, GameObject> objects;
    std::vector<EventRecord> records;
    std::size_t resolutions = 0;

    World()
    {
        objectType_ = schema.AddObject("GameObject");
        serviceType_ = schema.AddObject("Services");
        Require(schema.AddProperty(objectType_, "Id", schema.Types().Int(), true, false), schema.LastError());
        Require(schema.AddProperty(objectType_, "Position", schema.Types().Vector3()), schema.LastError());
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
    }

    std::uint64_t Create(float x)
    {
        // 파괴된 ID를 재사용하지 않으므로 저장된 핸들이 새 객체를 가리키지 않는다.
        const std::uint64_t id = nextId_++;
        objects.insert(std::make_pair(id, GameObject(id, x)));
        return id;
    }

    void Destroy(std::uint64_t id) { objects.erase(id); }

private:
    static const std::uint64_t ServiceId = 1000000;
    std::uint64_t nextId_ = 1;
    NeoCore::TypeId objectType_ = NeoCore::InvalidType;
    NeoCore::TypeId serviceType_ = NeoCore::InvalidType;

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

void Frame(std::vector<std::unique_ptr<NeoCore::Instance>>& instances)
{
    for (const auto& instance : instances)
    {
        // 한 인스턴스는 동시에 하나의 실행만 소유한다. 대기 중 이벤트는 엔진 큐에 둔다.
        if (instance->IsSuspended()) Complete(instance->Resume(0.25), "resume frame");
        else Complete(instance->Call("OnUpdate", {NeoCore::Scalar::Float(0.25)}), "update frame");
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
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, *world.bindings, error, filename, true);
    Require(bool(program), "compile: " + error.message);
    std::vector<std::unique_ptr<NeoCore::Instance>> instances;
    for (std::size_t i = 0; i < count; ++i)
    {
        std::unique_ptr<NeoCore::Instance> instance = program->CreateInstance(error);
        Require(bool(instance), "create instance: " + error.message);
        Require(instance->SetHostObject("GameObject", NeoCore::ObjectHandle(ids[i]), error), error.message);
        Complete(instance->Run(), "global initialization");
        const NeoCore::Result origin = instance->Call("OriginX");
        Complete(origin, "read origin");
        Require(origin.value.asFloat() == static_cast<double>(i * 10), "per-instance global binding before init");
        instances.push_back(std::move(instance));
    }

    Frame(instances);
    Frame(instances);
    Complete(instances[1]->Call("OnEvent", {NeoCore::Scalar::Int(3)}), "dispatch event");
    NeoCore::Result waiting = instances[0]->Call("DelayedEvent");
    Require(waiting.ok() && waiting.suspended() && instances[0]->IsSuspended(), "sleep yields to the engine");
    const NeoCore::Result busy = instances[0]->Call("OnEvent", {NeoCore::Scalar::Int(99)});
    Require(!busy.ok() && instances[0]->IsSuspended(), "busy event must not replace sleeping invocation");
    Frame(instances);
    Frame(instances);

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
    Complete(instances[1]->Call("OnUpdate", {NeoCore::Scalar::Float(0.25)}), "instance retains compiled program");
    Require(ReadInt(*instances[1], "UpdateCount") == 5, "shared program survives factory destruction");

    waiting = instances[2]->Call("DelayedEvent");
    Require(waiting.ok() && waiting.suspended(), "sleep before object destruction");
    world.Destroy(ids[2]);
    const std::uint64_t replacement = world.Create(90.0f);
    Require(instances[2]->SetHostObject("GameObject", NeoCore::ObjectHandle(replacement), error), error.message);
    const NeoCore::Result destroyed = instances[2]->Resume(0.25);
    Require(!destroyed.ok() && !instances[2]->IsSuspended(), "saved object ID fails after destruction");
    Require(destroyed.error.line > 0 && destroyed.error.sourceName == filename, "resume error retains script location");
    Require(world.objects.at(replacement).position.y == 0.0f, "old ID must not redirect to replacement");
    Require(ReadInt(*instances[0], "UpdateCount") == 3, "failed neighbor does not corrupt another instance");

    // 대기 중 인스턴스 파괴도 다른 인스턴스의 호출 프레임을 건드리지 않는다.
    waiting = instances[0]->Call("DelayedEvent");
    Require(waiting.ok() && waiting.suspended(), "sleep before instance destruction");
    instances[0].reset();
    Require(ReadInt(*instances[1], "UpdateCount") == 5, "pending invocation teardown is isolated");
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
