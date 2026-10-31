#include "NeoCore.h"
#include "NeoCoreHost.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

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

#ifdef _WIN32
std::string Utf8(const std::wstring& value)
{
    if (value.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    Require(size > 0, "cannot convert command line to UTF-8");
    std::string result(static_cast<std::size_t>(size), '\0');
    Require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.c_str(), static_cast<int>(value.size()), &result[0], size, nullptr, nullptr) == size,
        "cannot convert command line to UTF-8");
    return result;
}

std::wstring Wide(const std::string& value)
{
    if (value.empty()) return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    Require(size > 0, "invalid UTF-8 path");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    Require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), static_cast<int>(value.size()), &result[0], size) == size,
        "invalid UTF-8 path");
    return result;
}
#endif

std::vector<std::string> Arguments(int argc, char** argv)
{
#ifdef _WIN32
    // Windows의 좁은 argv에서 손실될 수 있는 녹화·스크립트 경로를 복원한다.
    int count = 0;
    wchar_t** native = CommandLineToArgvW(GetCommandLineW(), &count);
    Require(native != nullptr, "cannot read command line");
    const std::unique_ptr<void, decltype(&LocalFree)> owned(native, &LocalFree);
    std::vector<std::string> result;
    for (int i = 0; i < count; ++i) result.push_back(Utf8(native[i]));
    (void)argc;
    (void)argv;
    return result;
#else
    return std::vector<std::string>(argv, argv + argc);
#endif
}

std::FILE* Open(const std::string& path, bool write)
{
#ifdef _WIN32
    std::FILE* file = nullptr;
    _wfopen_s(&file, Wide(path).c_str(), write ? L"wb" : L"rb");
    return file;
#else
    return std::fopen(path.c_str(), write ? "wb" : "rb");
#endif
}

typedef std::unique_ptr<std::FILE, int (*)(std::FILE*)> File;

std::string Read(const std::string& path)
{
    File file(Open(path, false), &std::fclose);
    Require(bool(file), "cannot read script: " + path);
    std::string source;
    char buffer[8192];
    std::size_t size;
    while ((size = std::fread(buffer, 1, sizeof(buffer), file.get())) != 0) source.append(buffer, size);
    Require(!std::ferror(file.get()), "failed while reading script: " + path);
    return source;
}

void WriteRecording(const std::string& path, const std::string& frames)
{
    File file(Open(path, true), &std::fclose);
    Require(bool(file), "cannot write recording: " + path);
    const std::string text = "window.neoScene = {\"fps\":60,\"frames\":[\n" + frames + "\n]};\n";
    Require(std::fwrite(text.data(), 1, text.size(), file.get()) == text.size(), "failed while writing recording: " + path);
    Require(std::fclose(file.release()) == 0, "failed while closing recording: " + path);
}

struct Body
{
    std::uint64_t id;
    std::string name;
    NeoCore::Vector3Value position;
    NeoCore::Vector3Value reference;
    std::uint64_t callerId = 0;
    NeoCore::Scalar onUpdate;
    std::unique_ptr<NeoCore::Instance> instance;

    Body(std::uint64_t value, const char* label, float x, float y, float z)
        : id(value), name(label), position(x, y, z), reference(x, y, z) {}
};

class Scene
{
public:
    NeoCore::HostSchema schema;
    std::unique_ptr<NeoCore::HostBindings> bindings;
    std::unique_ptr<NeoCore::Runtime> runtime;
    std::unique_ptr<NeoCore::Program> program;
    std::vector<Body> bodies;
    std::vector<std::string> messages;

    Scene()
    {
        bodies.reserve(2);
        bodies.emplace_back(1, "Red", -2.0f, 0.0f, -0.5f);
        bodies.emplace_back(2, "Blue", 2.0f, 0.5f, 0.5f);
        objectType_ = schema.AddObject("GameObjectRef");
        serviceType_ = schema.AddObject("SceneServices");
        Require(schema.AddProperty(objectType_, "Name", schema.Types().String(), true, false), schema.LastError());
        Require(schema.AddProperty(objectType_, "Position", schema.Types().Vector3()), schema.LastError());
        Require(schema.AddEvent(objectType_, "OnUpdate", schema.Types().FunctionOf(schema.Types().Void(),
            {schema.Types().Float()})) != NeoCore::InvalidHostBinding, schema.LastError());
        Require(schema.AddMethod(serviceType_, "Log", schema.Types().Void(),
            {schema.Types().String()}) != NeoCore::InvalidHostBinding, schema.LastError());
        Require(schema.AddGlobal("GameObject", objectType_), schema.LastError());
        Require(schema.AddGlobal("Services", serviceType_), schema.LastError());
        bindings.reset(new NeoCore::HostBindings(schema, &Resolve, this));
        for (const NeoCore::HostBinding& binding : schema.Bindings())
        {
            bool bound = false;
            if (binding.ownerType == serviceType_)
                bound = bindings->Bind(binding.id, [this](NeoCore::HostCall& call) {
                    messages.push_back(call.String(0));
                    std::cout << "[script] " << messages.back() << '\n';
                    return true;
                });
            else if (binding.name == "Name")
                bound = bindings->Bind(binding.id, [](NeoCore::HostCall& call) {
                    call.ReturnString(static_cast<Body*>(call.Receiver())->name);
                    return true;
                });
            else if (!binding.eventName.empty())
                bound = bindings->Bind(binding.id, [](NeoCore::HostCall& call) {
                    // OnUpdate의 서명은 컴파일 때 확정했다. 함수와 등록한 인스턴스의 ID를 보관한다.
                    Body& body = *static_cast<Body*>(call.Receiver());
                    body.onUpdate = call.Argument(1);
                    body.callerId = call.CallerInstance().Id();
                    return true;
                });
            else if (binding.kind == NeoCore::HostBindingKind::Getter)
                bound = bindings->Bind(binding.id, [](NeoCore::HostCall& call) {
                    call.ReturnVector3(static_cast<Body*>(call.Receiver())->position);
                    return true;
                });
            else
                bound = bindings->Bind(binding.id, [](NeoCore::HostCall& call) {
                    const NeoCore::Vector3Value position = call.Vector3(0);
                    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
                    {
                        call.Fail("GameObject.Position must be finite");
                        return false;
                    }
                    static_cast<Body*>(call.Receiver())->position = position;
                    return true;
                });
            Require(bound, "cannot bind " + binding.name);
        }
        Require(bindings->BindGlobal("GameObject", NeoCore::ObjectHandle(1)), "default GameObject binding");
        Require(bindings->BindGlobal("Services", NeoCore::ObjectHandle(ServiceId)), "Services binding");
        NeoCore::Error error;
        runtime = NeoCore::Runtime::Create(*bindings, error);
        Require(bool(runtime), "cannot create runtime: " + error.message);
    }

    ~Scene()
    {
        // 함수 값이 프로그램·캡처를 보유하므로 인스턴스 파괴 전에 호스트 등록을 해제한다.
        for (Body& body : bodies)
        {
            body.onUpdate = NeoCore::Scalar();
            body.callerId = 0;
        }
    }

    void Load(const std::string& path)
    {
        NeoCore::Error error;
        program = runtime->Compile(Read(path), error, path, true);
        Require(bool(program), error.sourceName + ":" + std::to_string(error.line) + ": " + error.message);
        for (Body& body : bodies)
        {
            body.instance = program->CreateInstance(error);
            Require(bool(body.instance), "cannot create instance: " + error.message);
            Require(body.instance->SetHostObject("GameObject", NeoCore::ObjectHandle(body.id), error), error.message);
            Complete(body.instance->Run(), body.name + " initialization");
            Require(body.onUpdate.kind() == NeoCore::Scalar::Kind::Function && body.callerId == body.instance->Id(),
                body.name + " did not register OnUpdate");
        }
    }

    void Update(int frame, bool verify)
    {
        // 상속한 샘플과 같이 float 1/60을 스칼라 인자로 전달한다. 실시간 대기는 하지 않는다.
        const double dt = static_cast<double>(1.0f / 60.0f);
        elapsed_ += dt;
        NeoCore::ExecutionLimits limits;
        limits.instructionBudget = 100000;
        for (std::size_t i = 0; i < bodies.size(); ++i)
        {
            Body& body = bodies[i];
            NeoCore::Instance* caller = runtime->FindInstance(body.callerId);
            Require(caller != nullptr, "event caller instance no longer exists");
            const NeoCore::Scalar callback = body.onUpdate;
            Complete(caller->Call(callback, {NeoCore::Scalar::Float(dt)}, limits), body.name + " update");
            const double speed = i == 0 ? 1.0 : -0.5;
            body.reference.x = static_cast<float>(body.reference.x + speed * dt);
            body.reference.y = static_cast<float>((i == 0 ? 0.0 : 0.5) + std::sin(elapsed_ * 3.0) * 0.5);
            if (verify)
            {
                const NeoCore::Result count = body.instance->Call("UpdateCount");
                Complete(count, body.name + " update count");
                Require(count.value.kind() == NeoCore::Scalar::Kind::Int && count.value.asInt() == frame,
                    body.name + " independent update counter");
                Require(std::fabs(body.position.x - body.reference.x) < 0.000001f &&
                    std::fabs(body.position.y - body.reference.y) < 0.000001f && body.position.z == body.reference.z,
                    body.name + " trajectory differs at frame " + std::to_string(frame));
            }
        }
    }

    void Verify()
    {
        Require(messages.size() == 4 && messages[0] == "Red: script attached" && messages[1] == "Blue: script attached" &&
            messages[2] == "Red: 60 updates in my own instance" && messages[3] == "Blue: 60 updates in my own instance",
            "per-instance log events differ");
        for (std::size_t i = 0; i < bodies.size(); ++i)
        {
            const NeoCore::Vector3Value& position = bodies[i].position;
            Require(std::fabs(position.x - (i == 0 ? 0.0f : 1.0f)) < 0.001f &&
                std::fabs(position.y - ((i == 0 ? 0.0f : 0.5f) + std::sin(6.0f) * 0.5f)) < 0.001f &&
                position.z == (i == 0 ? -0.5f : 0.5f), "original 120-frame final position");
        }
        std::cout << "NeoCore motion: all checks passed (1 program, 2 instances, 120 updates each)\n";
    }

private:
    static const std::uint64_t ServiceId = 1000000;
    NeoCore::TypeId objectType_ = NeoCore::InvalidType;
    NeoCore::TypeId serviceType_ = NeoCore::InvalidType;
    double elapsed_ = 0.0;

    static void* Resolve(void* context, NeoCore::TypeId type, NeoCore::ObjectHandle handle)
    {
        Scene& scene = *static_cast<Scene*>(context);
        if (type == scene.serviceType_ && handle.value == ServiceId) return &scene;
        if (type == scene.objectType_)
            for (Body& body : scene.bodies) if (body.id == handle.value) return &body;
        return nullptr;
    }
};

void Record(std::ostream& stream, const Scene& scene, int frame)
{
    if (frame) stream << ",\n";
    stream << '[';
    for (std::size_t i = 0; i < scene.bodies.size(); ++i)
    {
        if (i) stream << ',';
        const NeoCore::Vector3Value& position = scene.bodies[i].position;
        stream << '[' << position.x << ',' << position.y << ',' << position.z << ']';
    }
    stream << ']';
}

void Print(const Scene& scene, int frame)
{
    std::cout << "frame " << std::setw(3) << frame;
    for (const Body& body : scene.bodies)
        std::cout << " | " << body.name << " (" << body.position.x << ", " << body.position.y << ", " << body.position.z << ')';
    std::cout << '\n';
}

}

int RunMotion(int argc, char** argv)
{
    const std::vector<std::string> args = Arguments(argc, argv);
    std::string script = NEOCORE_REPLAY_SCRIPT;
    std::string recording;
    bool verify = false;
    for (std::size_t i = 1; i < args.size(); ++i)
    {
        const std::string& argument = args[i];
        if (argument == "--motion") continue;
        if (argument == "--verify") verify = true;
        else if ((argument == "--file" || argument == "--script" || argument == "--record") && i + 1 < args.size())
        {
            const std::string value = args[++i];
            if (argument == "--record") recording = value;
            else script = value;
        }
        else if (argument == "--help")
        {
            std::cout << "Usage: neocore_minigame --motion [--verify] [--file replay.nc] [--record scene-data.js]\n"
                << "Runs 120 fixed 1/60-second simulation ticks without timers.\n";
            return 0;
        }
        else throw std::runtime_error("unknown or incomplete motion argument: " + argument);
    }
    Scene scene;
    scene.Load(script);
    std::ostringstream frames;
    frames.imbue(std::locale::classic());
    frames << std::setprecision(9);
    Record(frames, scene, 0);
    std::cout << std::fixed << std::setprecision(3);
    Print(scene, 0);
    for (int frame = 1; frame <= 120; ++frame)
    {
        scene.Update(frame, verify);
        Record(frames, scene, frame);
        if (frame % 30 == 0) Print(scene, frame);
    }
    if (verify) scene.Verify();
    if (!recording.empty())
    {
        WriteRecording(recording, frames.str());
        std::cout << "Recorded " << recording << ". Use scene-data.js beside viewer.html for replay.\n";
    }
    return 0;
}
