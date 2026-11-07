#include "NeoCore.h"
#include "NeoCoreHost.h"
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void CompleteInt(const NeoCore::Result& result, std::int64_t expected, const char* label)
{
    Require(result.ok() && result.status == NeoCore::ExecutionStatus::Completed &&
        result.value.kind() == NeoCore::Scalar::Kind::Int && result.value.asInt() == expected,
        std::string(label) + ": " + result.error.message);
}

void Suspended(const NeoCore::Result& result, const char* label)
{
    Require(result.ok() && result.suspended(), std::string(label) + ": " + result.error.message);
}

std::unique_ptr<NeoCore::Program> Compile(const std::string& source, bool debug,
    const NeoCore::CompileOptions& options = NeoCore::CompileOptions())
{
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(source, error, "options.nc", debug, options);
    Require(program && error.ok(), "compile: " + error.sourceName + ":" + std::to_string(error.line) + ": " + error.message);
    return program;
}

void CheckBudgets(bool debug)
{
    std::unique_ptr<NeoCore::Program> program = Compile(
        "int invocations = 0;\n"
        "export int Work(int limit) {\n"
        "    invocations += 1;\n"
        "    int sum = 0;\n"
        "    for (int i in 0, limit) sum += i;\n"
        "    return sum;\n"
        "}\n"
        "export int Count() { return invocations; }\n", debug);
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Instance> peer = program->CreateInstance(error);
    Require(peer && error.ok(), "budget peer instance");
    NeoCore::ExecutionLimits limits;
    limits.instructionBudget = 7;
    NeoCore::Result result = program->Call("Work", {NeoCore::Scalar::Int(1000)}, limits);
    Suspended(result, "initial instruction budget yields");
    Require(program->IsSuspended(), "yielded budget is tracked");
    const NeoCore::Result busy = program->Call("Count");
    Require(!busy.ok() && program->IsSuspended(), "busy call preserves budget suspension");
    CompleteInt(peer->Call("Work", {NeoCore::Scalar::Int(5)}), 10, "peer runs during budget suspension");
    Suspended(program->Resume(0.0, 1), "Resume applies its explicit instruction budget");
    unsigned slices = 0;
    do
    {
        result = program->Resume(0.0, 17);
        Require(result.ok(), "budget resume: " + result.error.message);
        ++slices;
    } while (result.suspended() && slices < 10000);
    CompleteInt(result, 499500, "sliced return retained");
    Require(slices > 1 && !program->IsSuspended(), "multiple slices finish");
    CompleteInt(program->Call("Count"), 1, "budget resume does not repeat entry effects");
    CompleteInt(program->Call("Work", {NeoCore::Scalar::Int(1000)}), 499500,
        "later unlimited Call does not inherit a previous instruction budget");
    CompleteInt(program->Call("Count"), 2, "second call enters exactly once");
    Suspended(program->Call("Work", {NeoCore::Scalar::Int(1000)}, limits), "second budgeted call");
    CompleteInt(program->Resume(), 499500, "default Resume clears the previous slice budget");
    Require(!program->Resume().ok(), "completed call cannot be resumed again");
}

void CheckDeferredLimits(bool debug)
{
    std::unique_ptr<NeoCore::Program> program = Compile(
        "int initialized = 0;\n"
        "sleep(0); initialized += 1;\n"
        "int calls = 0;\n"
        "export int Work(int count) {\n"
        "    calls += 1; int sum = 0;\n"
        "    for (int i in 0, count) sum += i;\n"
        "    return initialized * 1000000 + calls * 10000 + sum;\n"
        "}\n", debug);
    NeoCore::ExecutionLimits limits;
    limits.instructionBudget = 7;
    std::vector<NeoCore::Scalar> arguments(1, NeoCore::Scalar::Int(100));
    Suspended(program->Call("Work", arguments, limits), "top-level initialization suspends before deferred call");
    arguments[0] = NeoCore::Scalar::Int(2);
    limits.instructionBudget = 0;
    Suspended(program->Resume(), "deferred call retains its original instruction budget");
    CompleteInt(program->Resume(), 1014950, "deferred arguments and entry effects survive initialization");
    CompleteInt(program->Call("Work", {NeoCore::Scalar::Int(2)}), 1020001,
        "completed initialization is not replayed and later call is unlimited");
}

void CheckTimeouts(bool debug)
{
    std::unique_ptr<NeoCore::Program> program = Compile(
        "int entered = 0;\n"
        "export void Spin() { entered = 0; while (entered < 20000) { entered += 1; } }\n"
        "export int Probe() { return entered; }\n", debug);
    NeoCore::ExecutionLimits limits;
    limits.timeoutMs = -2;
    const NeoCore::Result invalid = program->Call("Spin", {}, limits);
    Require(!invalid.ok() && !invalid.suspended(), "invalid negative timeout rejected");
    CompleteInt(program->Call("Probe"), 0, "invalid timeout has no function effects");
    limits.timeoutMs = 0;
    NeoCore::Result timedOut = program->Call("Spin", {}, limits);
    Suspended(timedOut, "zero timeout yields a cooperative time slice");
    unsigned slices = 0;
    do { timedOut = program->Resume(); ++slices; }
    while (timedOut.ok() && timedOut.suspended() && slices < 10000);
    Require(timedOut.ok() && !timedOut.suspended() && timedOut.value.kind() == NeoCore::Scalar::Kind::Void,
        "time slices eventually complete and preserve void result");
    Require(slices > 1, "Resume preserves the active call's time slice");
    CompleteInt(program->Call("Probe"), 20000, "time-sliced effects are not repeated");
    const NeoCore::Result unlimited = program->Call("Spin");
    Require(unlimited.ok() && !unlimited.suspended(), "later unlimited Call does not inherit timeout");

    program = Compile("sleep(0); export int Spin() { int i = 0; while (i < 20000) { i += 1; } return i; }", debug);
    Suspended(program->Call("Spin", {}, limits), "timeout-bearing deferred call yields in initialization");
    NeoCore::Result deferredTimeout = program->Resume();
    Suspended(deferredTimeout, "deferred call retains timeout across initialization");
    slices = 0;
    do { deferredTimeout = program->Resume(); ++slices; }
    while (deferredTimeout.ok() && deferredTimeout.suspended() && slices < 10000);
    CompleteInt(deferredTimeout, 20000, "deferred time-sliced result retained");
}

void Location(const NeoCore::Result& result, const std::string& source, int line, const char* label)
{
    Require(!result.ok() && result.status == NeoCore::ExecutionStatus::Failed &&
        result.error.sourceName == source && result.error.line == line,
        std::string(label) + ": " + result.error.sourceName + ":" + std::to_string(result.error.line) + ": " + result.error.message);
}

void CheckModuleDiagnostics()
{
    std::map<std::string, std::string> sources;
    sources["leaf"] =
        "public int Crash() {\n"
        "    int zero = 0;\n"
        "    return 9 / zero;\n"
        "}\n"
        "public int Later() { sleep(0); return Crash(); }\n";
    sources["middle"] = "import leaf;\npublic int Direct() { return leaf.Crash(); }\npublic int Later() { return leaf.Later(); }\n";
    sources["init"] = "int zero = 0;\nint broken = 1 / zero;\npublic int Read() { return broken; }\n";
    sources["resumeinit"] = "int zero = 0;\nsleep(0);\nint broken = 1 / zero;\npublic int Read() { return broken; }\n";
    sources["closure"] = "public int FromLambda() {\n"
        "    fn()->int callback = fn()->int {\n"
        "        list<int> values = [1];\n"
        "        return values[9];\n"
        "    };\n"
        "    return callback();\n"
        "}\n";
    NeoCore::CompileOptions options;
    options.moduleLoader = [&sources](const std::string& name, const std::string& importer,
        NeoCore::ModuleSource& source, NeoCore::Error& error) {
        Require(!importer.empty(), "loader receives importing source name");
        const std::map<std::string, std::string>::const_iterator found = sources.find(name);
        if (found == sources.end()) { error.message = "missing fixture module"; return false; }
        source.source = found->second;
        source.sourceName = "modules/" + name + ".nc";
        return true;
    };
    std::unique_ptr<NeoCore::Program> program = Compile(
        "int prefix = 1;\nimport middle;\n"
        "export int Direct() { return middle.Direct(); }\n"
        "export int Later() { return middle.Later(); }\n"
        "export int Root() { int zero = 0; return prefix / zero; }\n", true, options);
    Location(program->Call("Direct"), "modules/leaf.nc", 3, "direct imported function runtime location");
    Suspended(program->Call("Later"), "nested module call suspends");
    Location(program->Resume(), "modules/leaf.nc", 3, "resumed imported function runtime location");
    Location(program->Call("Root"), "options.nc", 5, "root runtime location after inserted modules");

    program = Compile("import init;\nexport int Read() { return init.Read(); }", true, options);
    Location(program->Run(), "modules/init.nc", 2, "imported top-level runtime location");
    Location(program->Call("Read"), "modules/init.nc", 2, "cached initialization error retains original module location");

    program = Compile("import resumeinit;\nexport int Read() { return resumeinit.Read(); }", true, options);
    Suspended(program->Call("Read"), "imported initialization suspends");
    Location(program->Resume(), "modules/resumeinit.nc", 3, "resumed imported initialization runtime location");
    Location(program->Call("Read"), "modules/resumeinit.nc", 3, "cached resumed initialization error location");

    program = Compile("int before = 1;\nimport closure;\nexport int Read() { return closure.FromLambda(); }", true, options);
    Location(program->Call("Read"), "modules/closure.nc", 4,
        "lambda helper line remap composes with original module source mapping");

    sources["bad"] = "public int Bad() {\n    return \"wrong\";\n}\n";
    NeoCore::Error error;
    program = NeoCore::Program::Compile("import bad;", error, "options.nc", true, options);
    Require(!program && error.sourceName == "modules/bad.nc" && error.line == 2 && error.column == 12,
        "CompileOptions loader type diagnostic maps directly without caller translation: " + error.sourceName +
        ":" + std::to_string(error.line) + ":" + std::to_string(error.column) + ": " + error.message);
}

void CheckIndirectHostEffects(bool debug)
{
    NeoCore::HostSchema schema;
    const NeoCore::TypeId type = schema.AddObject("ReviewHost");
    schema.AddGlobal("Host", type);
    const NeoCore::HostBindingId swap = schema.AddMethod(type, "Swap", schema.Types().Int(), {});
    int calls = 0;
    NeoCore::HostBindings bindings(schema,
        [](void* data, NeoCore::TypeId, NeoCore::ObjectHandle object) -> void* {
            return object.value == 1 ? data : nullptr;
        }, &calls);
    Require(bindings.BindGlobal("Host", NeoCore::ObjectHandle(1)), "bind review host");
    Require(bindings.Bind(swap, [&calls](NeoCore::HostCall& call) {
        ++calls;
        const NeoCore::Result nested = call.CallScript("Swap");
        if (!nested.ok() || nested.suspended()) return false;
        call.ReturnInt(4);
        return true;
    }), "bind host reentry operation");
    NeoCore::Error error;
    std::unique_ptr<NeoCore::Program> program = NeoCore::Program::Compile(
        "struct S { int value; };\n"
        "S original = S(3);\n"
        "int First(S item, int argument) { return item.value * 100 + argument; }\n"
        "int Replacement(S item, int argument) { return 9000; }\n"
        "fn(S,int)->int callback = First;\n"
        "export void Swap() { callback = Replacement; original.value = 9; }\n"
        "export int Probe() { int value = callback(original, Host.Swap()); return value * 10 + original.value; }\n",
        bindings, error, "indirect-host.nc", debug);
    Require(program && error.ok(), "compile indirect host effects: " + error.message);
    CompleteInt(program->Call("Probe"), 3049,
        "indirect callee and struct argument are captured before later host reentry effects");
    Require(calls == 1, "indirect argument invokes host exactly once");
}

class RecordingTransport : public NeoCore::IHttpTransport
{
public:
    NeoCore::HttpResponse Send(const NeoCore::HttpRequest& request) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back(request);
        NeoCore::HttpResponse response;
        response.success = !request.IsCancelled();
        response.body = request.method == "POST" ? request.body : "mock reply";
        if (!response.success) response.error = "cancelled fixture request";
        return response;
    }

    std::vector<NeoCore::HttpRequest> Requests()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests_;
    }

private:
    std::mutex mutex_;
    std::vector<NeoCore::HttpRequest> requests_;
};

void CheckHttp(bool debug)
{
    std::shared_ptr<RecordingTransport> transport(new RecordingTransport());
    NeoCore::CompileOptions options;
    options.httpTransport = transport;
    std::unique_ptr<NeoCore::Program> program = Compile(
        "import system;\n"
        "int completed = 0; bool correct = true;\n"
        "void Got(bool ok, string text) { correct = correct && ok && text == \"mock reply\"; completed += 1; }\n"
        "fn(bool,string)->void Captured(string expected) {\n"
        "    return fn(bool ok,string text)->void { correct = correct && ok && text == expected; completed += 1; };\n"
        "}\n"
        "AsyncRequest request = system.async_create();\n"
        "request.get(1234, \"https://fixture.invalid/get\", Got);\n"
        "if (!request.wait()) correct = false;\n"
        "request.close();\n"
        "request = system.async_create();\n"
        "request.add_header(\"Content-Type\", \"text/plain\");\n"
        "request.post(2345, \"https://fixture.invalid/post\", \"mock reply\", Captured(\"mock reply\"));\n"
        "if (!request.wait()) correct = false;\n"
        "request.close();\n"
        "export bool Verify() { return correct && completed == 2; }\n", debug, options);
    NeoCore::Result result = program->Call("Verify");
    for (unsigned poll = 0; result.ok() && result.suspended() && poll < 2000; ++poll)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        result = program->Resume();
    }
    Require(result.ok() && !result.suspended() && result.value.kind() == NeoCore::Scalar::Kind::Bool && result.value.asBool(),
        "injected transport and typed callback complete: " + result.error.message);
    const std::vector<NeoCore::HttpRequest> requests = transport->Requests();
    Require(requests.size() == 2, "one transport operation per script request");
    Require(requests[0].method == "GET" && requests[0].url == "https://fixture.invalid/get" &&
        requests[0].timeoutMs == 1234 && requests[0].body.empty(), "GET request preserves typed arguments");
    Require(requests[1].method == "POST" && requests[1].url == "https://fixture.invalid/post" &&
        requests[1].timeoutMs == 2345 && requests[1].body == "mock reply", "POST request preserves typed arguments");
    bool contentType = false;
    for (const auto& header : requests[1].headers)
        if (header.first == "Content-Type" && header.second == "text/plain") contentType = true;
    Require(contentType, "typed async header reaches injected transport");
}
}

int main()
{
    try
    {
        for (bool debug : {false, true})
        {
            CheckBudgets(debug);
            CheckDeferredLimits(debug);
            CheckTimeouts(debug);
            CheckHttp(debug);
            CheckIndirectHostEffects(debug);
        }
        CheckModuleDiagnostics();
        std::puts("NeoCore runtime options passed: budgets, deferred limits, timeouts, module diagnostics, mock HTTP");
        return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
