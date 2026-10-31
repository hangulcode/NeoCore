#include "NeoCoreDebugger.h"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
int checks = 0, failures = 0;
void Check(bool condition, const std::string& message)
{
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message.c_str()); }
}
struct Listener : NeoCore::IDebugListener
{
    NeoCore::Instance* stopped = nullptr;
    NeoCore::DebugLocation location;
    NeoCore::DebugStopReason reason = NeoCore::DebugStopReason::Pause;
    int count = 0;
    NeoCore::Debugger* debugger = nullptr;
    NeoCore::Instance* peer = nullptr;
    bool inspection = false;
    bool reentryRejected = false;
    void OnStopped(NeoCore::Instance& instance, const NeoCore::DebugLocation& at, NeoCore::DebugStopReason why) override
    {
        stopped = &instance; location = at; reason = why; ++count;
        if (peer)
        {
            std::vector<NeoCore::DebugStackFrame> frames;
            inspection = debugger->GetStackTrace(instance, frames) && !frames.empty();
            const NeoCore::Result run = instance.Run();
            const NeoCore::Result call = instance.Call("Step", {NeoCore::Scalar::Int(0)});
            const NeoCore::Result resume = instance.Resume();
            const NeoCore::Result other = peer->Call("Step", {NeoCore::Scalar::Int(0)});
            reentryRejected = !run.ok() && !call.ok() && !resume.ok() && !other.ok();
            peer = nullptr;
        }
    }
};
NeoCore::DebugLocation Point(const char* file, int line)
{ NeoCore::DebugLocation point; point.sourceName = file; point.line = line; return point; }
}

int main()
{
    using namespace NeoCore;
    Error error;
    auto runtime = Runtime::Create(error);
    Check(runtime != nullptr, "create shared runtime"); if (!runtime) return 1;
    const std::string source = "export int Step(int v){\nint value=v+1;\nreturn value+3;\n}\n";
    auto first = runtime->Compile(source, error, "first.nc", true);
    auto second = runtime->Compile(source, error, "second.nc", true);
    Check(first && second, "compile two programs in one runtime: " + error.message); if (!first || !second) return 1;
    auto one = first->CreateInstance(error); auto peer = first->CreateInstance(error); auto two = second->CreateInstance(error);
    Check(one && peer && two, "create arbitrary instances"); if (!one || !peer || !two) return 1;
    Debugger* debugger = runtime->GetDebugger(); Listener listener;
    Check(debugger != nullptr, "runtime owns debugger"); if (!debugger) return 1;
    debugger->SetListener(&listener);
    Check(debugger->Attach(*one, error) && debugger->Attach(*peer, error) && debugger->Attach(*two, error), "attach across programs and peer instances");
    Check(debugger->SetBreakpoints(*one, {Point("first.nc",3)}, error), "first breakpoint");
    Check(debugger->SetBreakpoints(*two, {Point("second.nc",3)}, error), "second breakpoint");
    listener.debugger = debugger; listener.peer = peer.get();
    Result result = one->Call("Step", {Scalar::Int(42)});
    Check(result.suspended() && one->IsSuspended() && debugger->IsPaused(*one), "typed Call retains pending return at breakpoint");
    Check(listener.stopped == one.get() && listener.location.sourceName == "first.nc" && listener.location.line == 3, "stop identity and source location");
    Check(listener.inspection && listener.reentryRejected, "listener inspects snapshot but cannot reenter this runtime");
    std::vector<DebugStackFrame> frames; std::vector<DebugVariable> values;
    Check(debugger->GetStackTrace(*one, frames) && !frames.empty() && frames[0].functionName == "Step", "typed stack frame");
    Check(debugger->GetFrameVariables(*one, 0, values), "typed variables available");
    bool found = false; for (const auto& value : values) if (value.name == "value" && value.type == "int" && value.value == "43") found = true;
    Check(found, "source variable and static type");
    const int stops = listener.count;
    result = peer->Call("Step", {Scalar::Int(1)});
    Check(result.ok() && !result.suspended() && result.value.asInt() == 5 && listener.count == stops, "peer instance has no inherited breakpoint");
    Check(debugger->Continue(*one), "continue first instance"); result = one->Resume();
    Check(result.ok() && !result.suspended() && result.value.asInt() == 46 && !one->IsSuspended(), "typed Resume preserves final return value");
    result = two->Call("Step", {Scalar::Int(7)});
    Check(result.suspended() && listener.stopped == two.get() && listener.location.sourceName == "second.nc", "second program identity is separate");
    debugger->Detach(*two); result = two->Resume();
    Check(result.ok() && result.value.asInt() == 11, "detach releases paused instance without losing typed return");
    Check(!debugger->GetStackTrace(*two, frames), "detached snapshot unavailable");
    std::vector<DebugLocation> locations; std::vector<std::string> files;
    Check(debugger->GetExecutableLocations(*first, locations) && !locations.empty(), "public executable source locations");
    Check(debugger->GetSourceFiles(*second, files) && files.size() == 1 && files[0] == "second.nc", "public source files");
    Check(one->Reset(error), "reset attached instance");
    Check(!debugger->GetStackTrace(*one, frames) && !debugger->Continue(*one), "reset clears attachment and snapshot");
    Check(debugger->Attach(*one, error), "reattach after reset");
    result = one->Call("Step", {Scalar::Int(2)});
    Check(result.ok() && !result.suspended() && result.value.asInt() == 6, "old breakpoint not resurrected after reset");
    Check(debugger->SetBreakpoints(*one, {Point("first.nc",3)}, error), "restore breakpoint before cancellation");
    result = one->Call("Step", {Scalar::Int(10)});
    Check(result.suspended() && one->Cancel(error), "cancel paused typed call");
    Check(!debugger->GetStackTrace(*one, frames) && !debugger->IsPaused(*one), "cancel hides old stopped snapshot");
    Check(debugger->SetBreakpoints(*one, {}, error), "clear breakpoint after cancellation");
    result = one->Call("Step", {Scalar::Int(3)});
    Check(result.ok() && !result.suspended() && result.value.asInt() == 7, "fresh call after cancellation returns new value");
    auto initialProgram = runtime->Compile("int initial=1;\ninitial+=2;\nexport int Value(){return initial;}", error, "initial.nc", true);
    Check(initialProgram != nullptr, "compile global initialization program");
    if (initialProgram)
    {
        auto initial = initialProgram->CreateInstance(error);
        Check(initial && debugger->Attach(*initial, error) && debugger->Pause(*initial), "arm pause before Run");
        if (initial)
        {
            result = initial->Run();
            Check(result.suspended() && initial->IsSuspended() && debugger->IsPaused(*initial), "Run retains initialization at initial pause");
            Check(debugger->Continue(*initial), "continue paused initialization");
            result = initial->Resume();
            Check(result.ok() && !result.suspended() && !initial->IsSuspended(), "Resume completes typed initialization");
            result = initial->Call("Value");
            Check(result.ok() && result.value.asInt() == 3, "initialization executes exactly once after pause");
        }
    }
    auto faultProgram = runtime->Compile("export int Fault(int v){return 3/v;}", error, "fault.nc", true);
    Check(faultProgram != nullptr, "compile fault program");
    if (faultProgram)
    {
        auto fault = faultProgram->CreateInstance(error);
        Check(fault && debugger->Attach(*fault, error), "attach fault instance");
        if (fault)
        {
            result = fault->Call("Fault", {Scalar::Int(0)});
            Check(!result.ok() && listener.reason == DebugStopReason::Exception, "typed exception result and listener");
            Check(debugger->GetStackTrace(*fault, frames) && !frames.empty() && frames[0].location.sourceName == "fault.nc", "fault snapshot survives unwinding");
            Check(!debugger->Continue(*fault), "fault cannot resume");
            result = fault->Call("Missing");
            Check(!result.ok() && debugger->GetStackTrace(*fault, frames), "rejected call preserves prior fault snapshot");
            Check(fault->Cancel(error) && !debugger->GetStackTrace(*fault, frames), "cancel clears retained fault snapshot");
            result = fault->Call("Fault", {Scalar::Int(0)});
            Check(!result.ok() && debugger->GetStackTrace(*fault, frames), "subsequent failure captures a fresh fault snapshot");
            result = fault->Call("Fault", {Scalar::Int(1)});
            Check(result.ok() && result.value.asInt() == 3 && !debugger->GetStackTrace(*fault, frames), "new successful call clears prior fault snapshot");
            Check(fault->Reset(error) && !debugger->IsPaused(*fault), "fault reset clears pause");
            fault.reset();
        }
    }
    auto alienRuntime = Runtime::Create(error);
    auto budgetProgram = runtime->Compile("export int Sum(){int sum=0;for(int i in 0,1000){sum+=i;}return sum;}", error, "budget.nc", true);
    Check(budgetProgram != nullptr, "compile budgeted function");
    if (budgetProgram)
    {
        auto budgeted = budgetProgram->CreateInstance(error);
        Check(budgeted && budgeted->Run().ok() && debugger->Attach(*budgeted, error) && debugger->SetSliceBudget(*budgeted, 16), "set debugger default slice budget");
        if (budgeted)
        {
            result = budgeted->Call("Sum");
            Check(result.suspended() && !debugger->IsPaused(*budgeted), "default debugger budget yields typed Call");
            result = budgeted->Resume();
            Check(result.suspended() && !debugger->IsPaused(*budgeted), "default debugger budget also yields Resume");
            result = budgeted->Resume(0.0, 100000);
            Check(result.ok() && !result.suspended() && result.value.asInt() == 499500, "explicit Resume budget overrides debugger default and preserves return");
            debugger->Detach(*budgeted);
            result = budgeted->Call("Sum");
            Check(result.ok() && !result.suspended() && result.value.asInt() == 499500, "detach clears default debugger budget");
        }
    }
    auto alienProgram = alienRuntime->Compile(source, error, "alien.nc", true);
    auto alien = alienProgram->CreateInstance(error);
    Check(!debugger->Attach(*alien, error), "reject instance from another runtime");
    one.reset(); peer.reset(); two.reset();
    auto replacement = first->CreateInstance(error);
    Check(replacement && debugger->Attach(*replacement, error), "destroyed instance is not retained and replacement attaches");
    debugger->SetListener(nullptr);
    std::printf("NeoCore debugger: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
