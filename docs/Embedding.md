# Embedding NeoCore

Include [NeoCore.h](../NeoSource/NeoCore.h) and link `NeoCore::Runtime` in CMake.
Native Windows hosts reference `Windows/NeoCore.vcxproj`; the outputs follow
`NeoCore32/64[-d].lib`. The library and public C++ API require C++11. Modern MSVC's
default C++14 mode does not change that source-compatibility requirement.

```cpp
NeoCore::Error error;
auto program = NeoCore::Program::Compile(
    "export int Add(int a,int b){return a+b;}", error, "example.nc");
if (!program) throw std::runtime_error(error.message);
auto instance = program->CreateInstance(error);
if (!instance) throw std::runtime_error(error.message);
NeoCore::Result result = instance->Call("Add", {
    NeoCore::Scalar::Int(2), NeoCore::Scalar::Int(3) });
if (!result.ok() || result.suspended()) throw std::runtime_error(result.error.message);
const std::int64_t sum = result.value.asInt();
```

Check every error/result in application code. `Program::Compile` always checks
NeoCore source first; it does not expose dynamic compilation as a fallback.
A host schema and callbacks are supplied through the overload taking
`HostBindings`; see [HostBindings.md](HostBindings.md).

## Programs, instances and values

One program shares compiled code and metadata across independent instances.
Each instance owns globals, bound global object IDs and its pending execution.
Instances retain their program implementation even if the public Program object
is destroyed. Program convenience calls target its own default instance.
Instances sharing a program execute serially on one host thread.

`Scalar` is the owning public boundary value despite its historical name. It
represents scalars, vectors, typed object IDs, collections, structs and function
values. `CreateValue(typeName, error)` creates a typed empty collection or a
struct with valid defaults; the overload taking fields constructs a struct with
explicit positional values. Types must belong to the compiled program's registry.
Collections retain shared identity; structs copy by value. Independent programs
cannot exchange runtime-owned values.

`At`, `KeyAt`, `Get`, `Set`, `Put`, `Append`, `Remove`, `Resize`, `Reserve`, `Clear`,
`Clone` and `SameStorage` expose checked host collection operations. Invalid kinds,
indices and values report failure rather than enabling untyped storage.
`Instance::ReadGlobal` and `WriteGlobal` operate only on exported globals.

`Instance::Call(functionValue, args)` invokes a retained typed function. Named
functions belong to their program. Closures belong to their creating instance;
calling one in a peer instance or after reset fails, including through containers.
Host callbacks use `HostCall::Argument` and `ReturnValue` for aggregate values.

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
services. Allocation statistics are process-wide rather than per-instance.

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
`CompileOptions.httpTransport` supplies the cancellable HTTP/HTTPS implementation.
The core does not automatically link sockets or an HTTP library. The native
console can link libcurl; other hosts explicitly install their transport.

`SaveImage` writes actual backend bytecode, checked types, exports, source mappings
and the host schema. `LoadImage` validates and loads that data without recompiling
source. Reattach callbacks, resolvers, transports and current object IDs through
options/bindings. The current image format requires the same host schema and
registration order. Live pointers are never serialized. Images have corruption
checks, not cryptographic authentication. The current backend version is `0150`;
older images must be regenerated after incompatible opcode changes.

[JSON.md](JSON.md) describes typed JSON conversion.
[Unity.md](Unity.md) describes the C ABI and managed wrappers.
The [mini-game host](../Samples/neocore/minigame/README.md) is the complete
shared-program, multiple-instance example.
