# Embedding NeoCore

Include [NeoCore.h](../NeoSource/NeoCore.h) and link `NeoCore::Runtime` in CMake.
Native Windows hosts reference `Windows/NeoCore.vcxproj`; the outputs follow
`NeoCore32/64[-d].lib`. The library and public C++ API require C++11. Modern MSVC's
default C++14 mode does not change that source-compatibility requirement.

```cpp
NeoCore::Error error;
auto runtime = NeoCore::Runtime::Create(error);
if (!runtime) throw std::runtime_error(error.message);
auto program = runtime->Compile(
    "export int Add(int a,int b){return a+b;}", error, "example.nc");
if (!program) throw std::runtime_error(error.message);
auto instance = program->CreateInstance(error);
if (!instance) throw std::runtime_error(error.message);
NeoCore::Result result = instance->Call("Add", {
    NeoCore::Scalar::Int(2), NeoCore::Scalar::Int(3) });
if (!result.ok() || result.suspended()) throw std::runtime_error(result.error.message);
const std::int64_t sum = result.value.asInt();
```

Check every error/result in application code. Both runtime and isolated program
compilation check NeoCore source first; neither exposes dynamic compilation as a
fallback. A host schema and callbacks are supplied through the `Runtime::Create`
overload taking `HostBindings`; see [HostBindings.md](HostBindings.md).

## Programs, instances and values

Use a public `Runtime` when a host runs several script assets. Its `Compile` and
`LoadImage` methods create programs in one backend VM with one binding snapshot
and execution-context pool. Static `Program::Compile`/`LoadImage` are convenient
isolated entry points and create their own runtime; they are not the engine-wide
sharing path.

`Runtime::Create` uses `CompileOptions.httpTransport` and the supplied bindings
to establish runtime-wide services. Pass module loaders, definitions and assembly
output options to each `Runtime::Compile` call; `Create`
does not retain them as compilation defaults. A runtime's transport cannot be
replaced by later compile/load options. Compile, load and save-image operations
fail while any instance in that runtime is executing; defer them until control
has returned to the host. A suspended instance does not prevent those operations.

One program shares compiled code and metadata across independent instances.
Each instance owns globals, bound global object IDs and its pending execution.
Instances retain their program implementation even if the public Program object
is destroyed, and programs retain their runtime implementation. Program convenience
calls target its own default instance. All programs/instances of one runtime execute
serially on one host thread.

An instance exposes a host-facing `Id()` and non-owning `SetUserData`/`GetUserData`
context. During a binding callback, `HostCall::CallerInstance()` identifies the
caller even if the receiver is a different object or a shared service;
`InstanceUserData()` returns that caller's context. This is distinct from the
runtime-wide resolver context returned by `UserData()`.
Retain an instance ID for delayed events and resolve it with `Runtime::FindInstance`
at delivery time. A destroyed caller is not a valid event target. Keep the public
runtime wrapper available for this lookup; the returned instance pointer is borrowed.
`Reset` keeps the instance ID and host user data, but replaces script state and
invalidates captured closures. Cancel queued events from the old state explicitly.

`Scalar` is the owning public boundary value despite its historical name. It
represents scalars, vectors, typed object IDs, collections, structs and function
values. `CreateValue(typeName, error)` creates a typed empty collection or a
struct with valid defaults; the overload taking fields constructs a struct with
explicit positional values. Types must belong to the compiled program's registry.
Collections retain shared identity; structs copy by value. Different programs
cannot exchange runtime-owned values, including programs in the same runtime.
Their types, function indices and captured execution state remain program-local.
Transfer plain values or reconstruct a destination-typed collection explicitly
when implementing a cross-program host API.

`At`, `KeyAt`, `Get`, `Set`, `Put`, `Append`, `Remove`, `Resize`, `Reserve`, `Clear`,
`Clone` and `SameStorage` expose checked host collection operations. Invalid kinds,
indices and values report failure rather than enabling untyped storage.
`Instance::ReadGlobal` and `WriteGlobal` operate only on exported globals.

`Instance::Call(functionValue, args)` invokes a retained typed function. Named
functions belong to their program. Closures belong to their creating instance;
calling one in a peer instance or after reset fails, including through containers.
Host callbacks use `HostCall::Argument` and `ReturnValue` for aggregate values.

Event callbacks do not have to be exported. Register their signatures with
`HostSchema::AddEvent` and let the script pass the function value:

```cpp
void OnUpdate(float dt) { /* update script state */ }
GameObject.AddEvent("OnUpdate", OnUpdate);
```

The compiler resolves the literal event name and checks the function signature.
Each event has a direct host binding ID, including when different event names use
different signatures. The registration adapter retains `Argument(1)`, the caller
instance ID and receiver object ID; delivery uses `Instance::Call(function, args)`
on that original caller. Calling `Call("OnUpdate", args)` instead still requires
an exported function. The host owns registration storage and must remove old
callbacks before reset, script replacement or destruction. See
[typed event registration](HostBindings.md#typed-event-registration).

## Suspension and lifetime

Calls return Completed, Suspended or Failed. `ok()` alone does not mean completion;
check `suspended()` before reading a final result. `ExecutionLimits` supplies a
cooperative time slice and instruction budget. Native callbacks must return before
the VM can act on a slice boundary. Preliminary global initialization retains its
existing execution policy.

Resume a suspended instance before starting another call. `Cancel` abandons its
pending execution; `Reset` recreates its script state. Do not reset or destroy an
executing instance or access shared runtime state concurrently. Positive `sleep`
uses real elapsed milliseconds; the reserved Resume delta does not fast-forward
that timer. The mini-game sample demonstrates frame/event dispatch and suspension.

`TrimMemory`, `CollectCycles` and pool settings preserve VM memory-management
services. Program memory-management methods act on the shared runtime and can
reclaim idle storage released by its other programs; they do not invalidate live
values. Allocation statistics are process-wide rather than per-instance.

## Compile-time definitions

`CompileOptions.defines` accepts named `CompileDefine` records with Identifier,
Int, Float, String, Bool or Null kinds and their literal spelling. Definitions
replace identifier tokens before module resolution and static checking; comments
and string contents are untouched. An identifier replacement is expanded once,
not recursively. Signed numeric definitions remain one expression. Duplicate or
reserved names, invalid literals and out-of-range int64 literals are rejected even
when unused. Diagnostics retain the original use location.
String definitions must be representable as source literals; embedded NUL,
`\0`, `\u0000` and unpaired Unicode surrogates are rejected. Runtime host strings
remain separate length-aware values.

```cpp
NeoCore::CompileOptions options;
options.defines.push_back(NeoCore::CompileDefine(
    "MAX_ITEMS", NeoCore::CompileDefineKind::Int, "256"));
```

Each module receives the same definitions. Saved images retain the resolved
compiled values; load options do not recompile them. Source-based hosts can change
definitions and explicitly rebuild their programs.

## Modules, HTTP and serialized programs

`CompileOptions.moduleLoader` supplies `.nc` module source and canonical names.
`CompileOptions.httpTransport` supplies the cancellable HTTP/HTTPS implementation
when creating the runtime. The isolated `Program::Compile`/`LoadImage` helpers
forward that option to their private runtime.
The core does not automatically link sockets or an HTTP library. The native
console can link libcurl; other hosts explicitly install their transport.

`SaveImage` writes actual backend bytecode, checked types, exports, source mappings
and the host schema. `LoadImage` validates and loads that data without recompiling
source. Reattach callbacks, resolvers, transports and current object IDs through
options/bindings. The current image format requires the same host schema and
registration order. Live pointers are never serialized. Images have corruption
checks, not cryptographic authentication. The current backend version is `0151`;
older images must be regenerated after incompatible opcode changes.

[JSON.md](JSON.md) describes typed JSON conversion.
[Unity.md](Unity.md) describes the C ABI and managed wrappers.
[native-vector-api.md](native-vector-api.md) describes the sized vector read
overloads shared by the C++ API, the C ABI and the Unity bindings.
The [mini-game host](../Samples/neocore/minigame/README.md) is the complete
shared-program, multiple-instance example.

## Embedded debugging

Include [NeoCoreDebugger.h](../NeoSource/NeoCoreDebugger.h) and use the debugger
owned by `Runtime::GetDebugger()`. Compile programs with debug information, attach
the instance with `Attach`, and install breakpoints using `DebugLocation` source
names and one-based lines. `GetSourceFiles` and `GetExecutableLocations` discover
the valid locations for a program, including imported modules.

`IDebugListener::OnStopped` identifies the actual instance and its stop reason.
`GetLocation`, `GetStackTrace` and `GetFrameVariables` return owning source/type/value
snapshots rather than exposing backend pointers or handles. Inspect these while
the instance is paused, or inspect the saved exception snapshot after failure.
An exception snapshot is read-only; it does not turn a failed execution into a
resumable one. A new execution, cancellation, reset or detach clears that snapshot.
During the stop notification, inspect snapshots and return to the host loop;
`Run`, `Call` and `Resume` reject reentry into that runtime from the listener.

`Continue`, `StepInto`, `StepOver`, `StepOut` and `Pause` configure the next stop
mode. They do not execute bytecode. Keep execution in `Instance::Run`, `Call` and
`Resume` so that typed pending calls, return values and errors remain intact.
`SetSliceBudget` supplies the attached instance's default cooperative budget for
`Run`, `Call` and `Resume`. A nonzero budget passed to `Call` or `Resume` overrides
that default; zero inherits it. Detaching clears the debugger budget. A native
callback must still return before the VM can stop. Call the debugger on the
runtime's owning thread.

The listener is borrowed and must outlive its registration. Clear it before
destroying the listener. Reset or destruction detaches the instance and invalidates
its debug snapshot; reattach after reset if debugging should continue. Debugger
attachment does not retain an instance or permit destroying one during execution.
The console DAP adapter uses this same public typed boundary.

This complete example pauses in an exported function, reads its source location,
then finishes the same pending call through the typed instance API:

```cpp
#include "NeoCoreDebugger.h"
#include <iostream>
#include <stdexcept>

int main()
{
    NeoCore::Error error;
    auto runtime = NeoCore::Runtime::Create(error);
    if (!runtime) throw std::runtime_error(error.message);
    auto program = runtime->Compile(
        "export int Update() {\n"
        "    int value = 1;\n"
        "    return value + 1;\n"
        "}\n", error, "debug-example.nc", true);
    if (!program) throw std::runtime_error(error.message);
    auto instance = program->CreateInstance(error);
    if (!instance) throw std::runtime_error(error.message);
    NeoCore::Debugger* debugger = runtime->GetDebugger();
    if (!debugger->Attach(*instance, error))
        throw std::runtime_error(error.message);
    NeoCore::DebugLocation breakpoint;
    breakpoint.sourceName = "debug-example.nc";
    breakpoint.line = 3;
    if (!debugger->SetBreakpoints(*instance, {breakpoint}, error))
        throw std::runtime_error(error.message);

    NeoCore::Result result = instance->Call("Update");
    if (!result.ok()) throw std::runtime_error(result.error.message);
    NeoCore::DebugLocation stopped;
    if (!result.suspended() || !debugger->GetLocation(*instance, stopped))
        throw std::runtime_error("expected a breakpoint stop");
    std::cout << stopped.sourceName << ':' << stopped.line << '\n';
    if (!debugger->SetBreakpoints(*instance, {}, error))
        throw std::runtime_error(error.message);
    if (!debugger->Continue(*instance))
        throw std::runtime_error("could not continue the paused instance");
    result = instance->Resume();
    if (!result.ok() || result.suspended())
        throw std::runtime_error("debugged call did not complete: " + result.error.message);
    std::cout << result.value.asInt() << '\n';
    debugger->Detach(*instance);
}
```
