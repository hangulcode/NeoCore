#include "NeoCore.h"
#include "NeoCoreHost.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
int checks = 0;
void Check(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void Integer(const NeoCore::Result& result, std::int64_t expected, const char* label) {
    Check(result.ok() && !result.suspended() && result.value.asInt() == expected,
        std::string(label) + ": " + result.error.message +
        " (actual " + std::to_string(result.value.asInt()) + ", expected " + std::to_string(expected) + ")");
}
std::int64_t Owned() {
    const auto s = NeoCore::GetAllocationStats();
    return s.strings + s.maps + s.lists + s.sets + s.coroutines + s.modules +
        s.asyncs + s.vectors + s.arrays + s.closures;
}
struct Probe {
    NeoCore::HostSchema schema;
    std::unique_ptr<NeoCore::HostBindings> bindings;
    NeoCore::Runtime* runtime = nullptr;
    static void* Resolve(void* data, NeoCore::TypeId, NeoCore::ObjectHandle id) {
        return id.value == 1 ? data : nullptr;
    }
    Probe() {
        const auto type = schema.AddObject("StorageProbe");
        const auto live = schema.AddMethod(type, "Live", schema.Types().Int(), {});
        Check(schema.AddGlobal("Probe", type), "probe global");
        bindings.reset(new NeoCore::HostBindings(schema, Resolve, this));
        Check(bindings->Bind(live, [this](NeoCore::HostCall& call) {
            runtime->CollectCycles(true);
            runtime->TrimMemory(true);
            call.ReturnInt(Owned());
            return true;
        }), "probe binding");
        Check(bindings->BindGlobal("Probe", NeoCore::ObjectHandle(1)), "probe instance");
    }
};
const char* source = R"nc(
export list<string> saved = null;
StorageProbe savedObject = null;
StorageProbe CopyObject(StorageProbe value) { StorageProbe local = value; return local; }
export int ObjectCopies() {
    StorageProbe empty;
    if (empty != null) return -1;
    StorageProbe local = CopyObject(Probe); StorageProbe alias = local;
    local = null; alias = alias; savedObject = alias;
    if (savedObject != Probe || local != null || savedObject.Live() < 0) return -2;
    savedObject = null;
    return 1;
}
int Add(int x) { int result = x + 1; return result; }
int Recurse(int n) {
    string held = tostring(n);
    if (n == 0) return held.len();
    int next = Recurse(n - 1);
    return next + held.len();
}
void Allocate(int n) {
    string s = tostring(n);
    list<string> a = [s]; list<string> alias = a;
    alias = alias; a = null;
    array<int> values = array<int>(2, n);
    array<int> other = values; values = null;
    map<int,string> m = {n:s}; map<int,string> peer = m; m = peer;
    if (other[0] != n || alias[0] != s || m[n] != s) { int z=0; print(1/z); }
}
export int Churn(int n) {
    int sum = 0;
    for (int i in 0,n) { Allocate(i); sum = Add(sum); }
    if (sum != n) { int z=0; print(1/z); }
    return Probe.Live();
}
Vector3 VectorWork(int n) {
    Vector3 initial = Vector3(n, 2, 3);
    array<Vector3> values = array<Vector3>(2, initial);
    values.resize(3);
    Vector3 held = values[0];
    values[1] = held;
    values[0].x += 1;
    held.z = 7;
    if (initial.x != n || values[1].x != n || values[1].z != 3 ||
        values[2].x != n || values[2].z != 3 || held.x != n || held.z != 7) {
        int zero = 0; print(1 / zero);
    }
    return values[0];
}
export int VectorChurn(int n) {
    int live = 0;
    for (int i in 0,n) {
        Vector3 value = VectorWork(i);
        if (value.x != i + 1 || value.y != 2 || value.z != 3) return -1;
        if (i == 7) live = Probe.Live();
        if (i > 7 && (i & 63) == 7 && Probe.Live() != live) return -2;
    }
    return n;
}
export int BlockChurn(int n) {
    int sum = 0; int live = 0;
    list<string> escaped = null;
    fn()->string first = null; fn()->string current = null;
    for (int i in 0,n) {
        {
            string value = tostring(i);
            list<string> payload = [value]; list<string> alias = payload;
            payload = null; payload = alias;
            escaped = payload;
            current = fn()->string { return value; };
            if (i == 0) first = current;
        }
        {
            int value = i; list<int> payload = [value];
            sum += payload[0];
        }
        {
            string value; list<int> payload; fn()->int action;
            if (value != null || payload != null || action != null) return -1;
            value = tostring(i); payload = [i];
            action = fn()->int { return i; };
            if (action() != i) return -2;
        }
        if ((i & 1) == 0) {
            string branch = tostring(i);
            if (branch != escaped[0]) return -3;
        } else {
            array<int> branch = array<int>(2, i);
            if (branch[0] != i) return -4;
        }
        if (current() != tostring(i) || first() != "0" || escaped[0] != tostring(i)) return -5;
        if (i == 7) live = Probe.Live();
        if (i > 7 && (i & 63) == 7) {
            int currentLive = Probe.Live();
            if (currentLive != live) return -6;
        }
    }
    return sum;
}
export string BlockReturn(bool choose) {
    string saved = tostring(31);
    if (choose) { list<string> item = [saved]; return item[0]; }
    else { array<int> item = array<int>(1, 7); if (item[0] != 7) return "bad"; }
    return saved;
}
export int BlockControl() {
    int count = 0;
    for (int i in 0,10) {
        string value = tostring(i); list<string> payload = [value];
        if (i == 2) continue;
        if (i == 5) break;
        count += payload[0].len();
    }
    return count;
}
export int BlockFail(bool choose) {
    if (choose) {
        string value = tostring(1); list<string> payload = [value];
        int zero = 0; return payload[0].len() / zero;
    } else {
        list<int> value = [1]; string payload = tostring(2);
        int zero = 0; return (value[0] + payload.len()) / zero;
    }
}
fn()->string Capture(string text) {
    string local = text .. "!";
    return fn()->string { return local; };
}
export string Captured() {
    fn()->string f = Capture("kept");
    Allocate(4); return f();
}
export string Keep() { string local = "value" .. tostring(42); return local; }
export int Recursive() { return Recurse(12); }
export int Skip(bool condition) {
    if (condition) { string skipped=tostring(12); list<string> items=[skipped]; }
    return 42;
}
export int Fail() {
    string s = tostring(42); list<string> a = [s];
    int zero = 0; return a[0].len() / zero;
}
export int Wait() {
    string s = tostring(1234); list<string> a = [s];
    sleep(0); return a[0].len();
}
export void Fill(int n) { saved = [tostring(n)]; }
string MakeName(int i) { return "n" .. tostring(i); }
Vector3 MakePosition(float x) { return Vector3(x, 1.0, 2.0); }
list<int> MakeList(int i) { return [i, i + 1]; }
export int DiscardedResults(int n) {
    int live = 0;
    for (int i in 0, n) {
        MakeName(i); MakePosition(1.0); MakeList(i);
        if (i == 7) live = Probe.Live();
    }
    if (Probe.Live() != live) return -1;
    return n;
}
)nc";

void Exercise(bool debug, int iterations) {
    Probe probe; NeoCore::Error error;
    auto runtime = NeoCore::Runtime::Create(*probe.bindings, error);
    Check(runtime && error.ok(), "create runtime"); probe.runtime = runtime.get();
    auto program = runtime->Compile(source, error, "storage.nc", debug);
    Check(program && error.ok(), "compile: " + error.message);
    std::vector<std::uint8_t> image;
    Check(program->SaveImage(image, error), "save layout: " + error.message);
    auto loaded = runtime->LoadImage(image, error);
    Check(loaded && error.ok(), "load layout: " + error.message);
    auto instance = loaded->CreateInstance(error);
    auto peer = loaded->CreateInstance(error);
    Check(instance && peer && error.ok(), "shared instances");
    Check(instance->Run().ok() && peer->Run().ok(), "initialize instances");
    Integer(instance->Call("ObjectCopies"), 1, "host IDs copy without pointer ownership");
    const auto warm = instance->Call("Churn", {NeoCore::Scalar::Int(10)});
    Check(warm.ok(), "warm ownership paths: " + warm.error.message);
    const auto baseline = instance->Call("Churn", {NeoCore::Scalar::Int(0)});
    Check(baseline.ok(), "live baseline");
    Integer(instance->Call("Churn", {NeoCore::Scalar::Int(iterations)}), baseline.value.asInt(),
        "returned frames release ownership while caller and VM remain live");
    Integer(instance->Call("VectorChurn", {NeoCore::Scalar::Int(2000)}), 2000,
        "Vector3 array reads, writes, resize and returns retain independent values");
    Integer(instance->Call("Churn", {NeoCore::Scalar::Int(0)}), baseline.value.asInt(),
        "Vector3 array ownership released before VM destruction");
    Integer(instance->Call("BlockChurn", {NeoCore::Scalar::Int(2000)}), 1999000,
        "sibling scopes, repeated declarations and escaping captures preserve ownership");
    Integer(instance->Call("Churn", {NeoCore::Scalar::Int(0)}), baseline.value.asInt(),
        "block-local ownership released on function return");
    Integer(instance->Call("BlockControl"), 4, "break and continue preserve live block locals");
    Integer(instance->Call("DiscardedResults", {NeoCore::Scalar::Int(2000)}), 2000,
        "statement calls release discarded string, vector and list results");
    for (bool choose : {false, true}) {
        const auto result = instance->Call("BlockReturn", {NeoCore::Scalar::Bool(choose)});
        Check(result.ok() && result.value.asString() == "31", "branch return preserves owned value");
    }
    Integer(instance->Call("Recursive"), 16, "nested owning and numeric frames");
    Integer(instance->Call("ObjectCopies"), 1, "host ID frames reuse previous owning stack slots");
    Check(instance->Call("Captured").value.asString() == "kept!", "capture survives frame cleanup");
    auto held = instance->Call("Keep");
    Check(held.ok() && held.value.asString() == "value42", "return value retains ownership");
    Integer(instance->Call("Skip", {NeoCore::Scalar::Bool(false)}), 42, "skipped declarations");
    Integer(instance->Call("Skip", {NeoCore::Scalar::Bool(true)}), 42, "initialized declarations");
    Check(held.value.asString() == "value42", "later calls preserve retained result");
    held = NeoCore::Result();
    runtime->CollectCycles(true); runtime->TrimMemory(true);
    const std::int64_t settled = Owned();
    for (int i=0;i<20;++i) {
        Check(!instance->Call("Fail").ok(), "failed initializer frame unwinds");
        Check(!instance->Call("BlockFail", {NeoCore::Scalar::Bool((i & 1) != 0)}).ok(),
            "failure unwinds only initialized branches");
    }
    runtime->CollectCycles(true); runtime->TrimMemory(true);
    Check(Owned() == settled, "failure does not retain owning locals");
    Check(instance->Call("Wait").suspended(), "sleep retains locals");
    Integer(instance->Resume(), 4, "resume reads retained values");
    Check(instance->Call("Wait").suspended(), "sleep before cancel");
    Check(instance->Cancel(error), "cancel");
    runtime->CollectCycles(true); runtime->TrimMemory(true);
    Check(Owned() == settled, "cancel releases locals once: " +
        std::to_string(Owned()) + " vs " + std::to_string(settled));
    Check(instance->Call("Fill", {NeoCore::Scalar::Int(7)}).ok(), "global assignment");
    Check(instance->ReadGlobal("saved").value.At(0).asString() == "7", "global survives return");
    Check(peer->ReadGlobal("saved").value.kind() == NeoCore::Scalar::Kind::Null, "peer globals are independent");
    for (int i=0;i<20;++i) Check(instance->Call("Fill", {NeoCore::Scalar::Int(i)}).ok(), "global replacement");
    Check(instance->Reset(error), "reset");
    Check(instance->ReadGlobal("saved").value.kind() == NeoCore::Scalar::Kind::Null, "reset reinitializes globals");
    Integer(instance->Call("Skip", {NeoCore::Scalar::Bool(false)}), 42, "reset remains callable");
    instance.reset(); peer.reset(); loaded.reset(); program.reset();
    runtime->CollectCycles(true);
}
}
int main(int argc, char** argv) {
    try {
        const bool stress = argc > 1 && std::strcmp(argv[1], "--stress") == 0;
        Exercise(false, stress ? 10000000 : 2000);
        if (!stress) Exercise(true, 2000);
        std::printf("storage: %d checks passed%s\n", checks, stress ? "; 10M allocating calls and 10M numeric calls" : "");
        return 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "storage: %s\n", ex.what()); return 1;
    }
}
