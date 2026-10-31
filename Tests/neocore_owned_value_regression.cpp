#include "NeoCore.h"
#include "NeoCoreDebugAccess.h"
#include "NeoCoreHostBridge.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <limits>
#include <stdexcept>

namespace
{
int checks = 0;
void Check(bool success, const char* label)
{
    ++checks;
    if (!success) throw std::runtime_error(label);
}

void Verify(bool debug)
{
    using namespace NeoCoreBackend;
    NeoCore::Error error;
    std::shared_ptr<NeoCore::Program> program(NeoCore::Program::Compile(
        "export list<int> values = [3, 5];\n"
        "export list<int> Get() { return values; }\n"
        "export int Read(list<int> input) { return input[0]; }\n"
        "export list<fn()->int> callbacks = [];\n"
        "export int Invoke() { return callbacks[0](); }\n"
        "export int Use(fn()->int callback) { return callback(); }\n"
        "export fn()->int Make() { int n = 4; return fn()->int { n += 1; return n; }; }\n",
        error, "owned-values.nc", debug).release());
    Check(bool(program), error.message.c_str());
    Check(program->Run().ok(), "initialization");
    NeoScript::IRuntime* runtime = nullptr;
    NeoScript::ProgramHandle image;
    NeoScript::InstanceHandle instance;
    const NeoCore::StaticCompileResult* checked = nullptr;
    const NeoCore::ModuleBundle* bundle = nullptr;
    Check(NeoCore::DebugAccess::Get(*program, runtime, image, instance, checked, bundle), "backend access");
    ValuePtr saved;
    {
        NeoScript::Invocation call = runtime->Call(instance, "Get");
        Check(call.invoke() == NeoScript::RunStatus::Completed, "collection return");
        saved = CaptureResult(call, program);
    }
    Check(ValueSize(saved) == 2, "retained collection after invocation");
    ValueData data;
    Check(ReadValue(ValueAt(saved, 0), data) && data.integer == 3, "retained element");
    ValuePtr alias = ReadGlobal(*runtime, instance, "values", program);
    Check(ValueSameStorage(saved, alias), "separate captures preserve identity");
    data = ValueData(); data.kind = ValueKind::Int; data.integer = 9;
    ValuePtr nine = MakeValue(*runtime, data, program);
    Check(ValueSet(saved, 0, nine), "retained collection write");
    {
        NeoScript::Invocation call = runtime->Call(instance, "Read");
        Check(AppendValue(call, saved) && call.invoke() == NeoScript::RunStatus::Completed && call.retInt() == 9,
            "owned collection argument");
    }
    ValuePtr callback;
    {
        NeoScript::Invocation call = runtime->Call(instance, "Make");
        Check(call.invoke() == NeoScript::RunStatus::Completed, "closure return");
        callback = CaptureResult(call, program);
    }
    for (int expected = 5; expected <= 6; ++expected)
    {
        NeoScript::Invocation call = CallFunction(*runtime, instance, callback);
        Check(bool(call) && call.invoke() == NeoScript::RunStatus::Completed && call.retInt() == expected,
            "retained capture state");
    }
    ValuePtr callbacks = MakeCollection(*runtime, ValueKind::List, ArrayStorage::Int64, program);
    Check(ValueAppend(callbacks, callback), "host-created closure collection");
    {
        NeoScript::Invocation call = CallFunction(*runtime, instance, ValueAt(callbacks, 0));
        Check(bool(call) && call.invoke() == NeoScript::RunStatus::Completed && call.retInt() == 7,
            "closure collection preserves creator");
    }
    NeoScript::InstanceDesc peerDesc;
    peerDesc.runGlobalInit = false;
    NeoScript::InstanceHandle peer = runtime->CreateInstance(image, peerDesc);
    Check(bool(peer) && runtime->RunGlobalInit(peer) == NeoScript::RunStatus::Completed, "peer instance");
    Check(!CallFunction(*runtime, peer, callback), "cross-instance closure call rejected");
    {
        NeoScript::Invocation call = runtime->Call(peer, "Use");
        Check(!AppendValue(call, callback), "cross-instance closure argument rejected");
    }
    ValuePtr peerCallbacks = ReadGlobal(*runtime, peer, "callbacks", program);
    Check(ValueAppend(peerCallbacks, callback), "shared collection can hold a live closure");
    {
        NeoScript::Invocation call = runtime->Call(peer, "Invoke");
        Check(call.invoke() == NeoScript::RunStatus::Failed, "shared-container alias cannot bypass creator check");
    }
    runtime->DestroyInstance(peer);
    peerCallbacks.reset();
    ValuePtr map = MakeCollection(*runtime, ValueKind::Map, ArrayStorage::Int64, program);
    data = ValueData(); data.kind = ValueKind::String; data.text.assign("a\0b", 3);
    ValuePtr key = MakeValue(*runtime, data, program);
    Check(ValuePut(map, key, saved) && ValueContains(map, key), "map embedded-NUL key");
    Check(ValueSameStorage(ValueAt(map, 0), saved) && ValueSameStorage(ValueFind(map, key), saved), "map value identity");
    Check(ReadValue(ValueKeyAt(map, 0), data) && data.text == std::string("a\0b", 3), "owned map key");
    {
        std::vector<std::pair<ValuePtr, ValuePtr>> entries;
        Check(SnapshotMap(map, entries) && entries.size() == 1 && ValueSameStorage(entries[0].second, saved),
            "map snapshot retains nested identity");
        Check(ReadValue(entries[0].first, data) && data.text == std::string("a\0b", 3), "map snapshot retains exact key bytes");
        Check(!SnapshotMap(saved, entries) && entries.empty(), "map snapshot rejects a list");
    }
    ValuePtr array = MakeCollection(*runtime, ValueKind::Array, ArrayStorage::Vector4, program);
    Check(ValueResize(array, 2), "vector array resize");
    data = ValueData(); data.kind = ValueKind::Vector4; data.vector[0] = 1; data.vector[3] = 4;
    ValuePtr vector = MakeValue(*runtime, data, program);
    Check(ValueSet(array, 0, vector), "vector array write");
    Check(ReadValue(ValueAt(array, 0), data) && data.vector[0] == 1 && data.vector[3] == 4, "vector array copy");
    Check(!ValueAt(array, 2), "array bounds");
    Check(ValueAppend(array, vector) && ValueSize(array) == 3, "vector array append");
    Check(ReadValue(ValueAt(array, 2), data) && data.vector[3] == 4, "appended vector value");
    const ArrayStorage storages[] = {ArrayStorage::Int64, ArrayStorage::Float64, ArrayStorage::Int32, ArrayStorage::Float32};
    for (ArrayStorage storage : storages)
    {
        ValuePtr packed = MakeCollection(*runtime, ValueKind::Array, storage, program);
        Check(ValueResize(packed, 3), "bulk array resize");
        const std::uint64_t input[3] = {1, 2, 3};
        std::uint64_t output[3] = {};
        const std::size_t bytes = (storage == ArrayStorage::Int32 || storage == ArrayStorage::Float32) ? 12 : 24;
        Check(WriteArrayBuffer(packed, storage, 0, input, 3) &&
            ReadArrayBuffer(packed, storage, 0, output, 3) && std::memcmp(input, output, bytes) == 0, "exact-storage bulk round trip");
        Check(!ReadArrayBuffer(packed, ArrayStorage::Bool, 0, output, 3), "bulk rejects wrong storage");
        Check(!WriteArrayBuffer(packed, storage, 2, input, 2), "bulk write bounds checked before mutation");
        Check(!ReadArrayBuffer(packed, storage, (std::numeric_limits<std::size_t>::max)(), output, 1), "bulk offset overflow rejected");
        Check(!ReadArrayBuffer(packed, storage, 0, nullptr, 1), "bulk nonempty null buffer rejected");
        Check(ReadArrayBuffer(packed, storage, 3, nullptr, 0), "bulk empty range at end");
    }

    NeoCore::Error otherError;
    std::shared_ptr<NeoCore::Program> other(NeoCore::Program::Compile("export int Read(list<int> x){return x[0];}", otherError).release());
    NeoScript::IRuntime* otherRuntime = nullptr;
    NeoScript::ProgramHandle otherImage;
    NeoScript::InstanceHandle otherInstance;
    Check(other && other->Run().ok() && NeoCore::DebugAccess::Get(*other, otherRuntime, otherImage, otherInstance, checked, bundle),
        "second runtime");
    {
        NeoScript::Invocation call = otherRuntime->Call(otherInstance, "Read");
        Check(!AppendValue(call, saved), "cross-runtime argument rejected");
    }
    Check(program->Reset(error), "reset with retained collections");
    Check(ReadValue(ValueAt(saved, 0), data) && data.integer == 9, "retained data survives reset");
    Check(!CallFunction(*runtime, instance, callback), "closure invalidated by reset");
    {
        NeoScript::Invocation call = runtime->Call(instance, "Read");
        Check(!AppendValue(call, callback), "expired closure cannot be passed back");
    }
    Check(program->Run().ok(), "reinitialize");
    ValuePtr fresh = ReadGlobal(*runtime, instance, "values", program);
    Check(!ValueSameStorage(saved, fresh), "reset creates separate globals");
    Check(ValueRemove(map, key) && !ValueContains(map, key), "map remove");
    Check(ValueClear(array) && ValueSize(array) == 0, "array clear");
    std::weak_ptr<NeoCore::Program> weak = program;
    program.reset();
    Check(!weak.expired() && ReadValue(ValueAt(saved, 0), data) && data.integer == 9, "owned value keeps runtime alive");
    fresh.reset(); callback.reset(); callbacks.reset(); saved.reset(); alias.reset(); nine.reset(); map.reset(); key.reset(); array.reset(); vector.reset();
    Check(weak.expired(), "last owned value releases runtime");
}
}

int main()
{
    try { Verify(false); Verify(true); }
    catch (const std::exception& error) { std::fprintf(stderr, "owned value regression: %s\n", error.what()); return 1; }
    std::printf("NeoCore owned value regression: %d checks passed\n", checks);
    return 0;
}
