# NeoCore — statically typed scripting

NeoCore is an independent scripting-language project derived from
[NeoScript](https://github.com/hangulcode/NeoScript). It keeps the existing VM as an
initial execution backend while adding explicit variable types, C-style function
declarations, and compile-time checking. The static language is under active development.

**NeoCore source files use `.nc`.** Dynamic `.ns` samples and regression fixtures
remain in the separate NeoScript repository. NeoCore's editor protocols compile
through its static frontend; they do not accept dynamic NeoScript source.

**[Language design and implementation status](docs/StaticTyping.md)** ·
**[Typed host bindings](docs/HostBindings.md)** ·
**[Converted samples and run commands](docs/SampleMigration.md)** ·
**[Source repository](https://github.com/hangulcode/NeoCore)**

## Why NeoCore

NeoCore started from NeoScript to pursue higher execution performance and more
reliable scripts through static typing. Its compiler aims to resolve types,
function signatures, member access, and applicable operations before execution.
This moves errors that can be detected from the program and host declarations to
compile time and reduces repeated runtime type checks and name lookups.

Automatic memory management remains part of the design, retaining the backend's
reference counting and cycle-collection capabilities. Script authors should be able
to use managed values and containers without manually allocating and freeing their
storage. Hosts still control safe points for cycle collection. Static typing is
intended to improve execution and validation while preserving that convenience.

The project follows these goals:

- **Compile-time contracts:** explicit types, function signatures, nullability,
  value semantics, and host schemas make invalid operations diagnosable before a script
  runs. Unsupported features fail explicitly instead of silently becoming dynamic.
- **Efficient execution:** carry resolved type information into dedicated VM
  instructions and progressively remove transitional compiler and runtime helpers.
  Performance improvements must be measured; static typing alone is not evidence
  that NeoCore already runs faster than NeoScript.
- **Safe host integration:** resolve member names and signatures during compilation,
  while validating object IDs and current object capabilities during execution.
  Engine objects remain owned by the host; script garbage collection does not
  replace the host's lifetime rules.
- **Defined behavior:** preserve evaluation order, numeric rules, value/reference
  semantics, and useful error locations as the compiler and VM evolve. Bounds,
  allocation failures, and other conditions that depend on runtime state still
  require runtime handling.
- **Portable embedding:** retain the bytecode VM execution model and C++11-compatible
  core, with a clear boundary between the scripting language and its host engine.

These are development goals, not a claim that the static compiler, host integrations,
or performance work is complete. The current implementation is described below.

## Current milestone

The frontend checks scalar/vector declarations, structs, typed arrays, nested lists/maps, sets, typed function values and closures, named functions,
calls and returns, including forward calls and recursion. It rejects `var`, `fun`, undeclared types,
and implicit dynamic fallback. Numeric defaults are initialized; non-null strings
require an initializer. Nullable values do not implicitly unwrap; native object
locals support member access inside an explicit non-null `if` branch.

NeoCore does not support pointers or `T&` reference declarations, parameters, or
returns. Structs and vectors copy by value; functions return modified values for
the caller to assign explicitly. Struct copies include independent nested fields.
See the [struct sample](Samples/neocore/scripts/structs.nc) and
[vector sample](Samples/neocore/scripts/vectors.nc). Vectors have float32 components,
value copies, and checked named component writes; Quaternion uses `w, x, y, z` order.
Vector2/3/4 support same-dimension addition/subtraction and scalar multiplication/
division. Typed arrays keep a fixed element type and support vector element updates;
the [particles sample](Samples/neocore/scripts/particles.nc) checks them against a
scalar reference without timers.
Built-in imports expose 65 checked math functions plus string/regex, collection,
time and HTTP APIs. Typed JSON uses `system.jsonEncode(value)` and
`system.jsonDecode<T>(text)`, preserving struct field names and validating the
declared shape; see [JSON](docs/JSON.md). Script modules, typed foreach and
signature-bound coroutines are supported; see the [migration guide](docs/SampleMigration.md).

Typed host schemas declare nominal object types, structs, globals, methods, and
read/write properties before compilation. The frontend resolves members and
checks declared arguments and return types; execution calls a fixed binding without a
per-member name lookup. Hosts retain ownership and resolve object IDs when a
member call begins. See the [binding contract and current limits](docs/HostBindings.md).

```cpp
int Fib(int n)
{
    if (n < 2)
        return n;
    return Fib(n - 1) + Fib(n - 2);
}

int result = Fib(12);
print(result);
```

`int` is a signed 64-bit integer and `float` is a 64-bit floating-point value.
**`int32` and `float32` are not scalar types that scripts can declare.** The agreed
32-bit support is limited to `array32<int>` / `array32<float>` storage and vector
components. Compact-array reads produce ordinary int64 / float64 values; writes
check int32 range or round to float32.

Checked numeric expressions now use a typed intermediate representation (IR),
retaining literal values, resolved symbols, operand types, and original source
locations. The bytecode emitter reads that IR directly instead of reparsing
numeric operation strings. Arithmetic, integer bit operations, int/float conversions,
comparisons, comparison branches, and numeric negation use 30 dedicated I64/F64
bytecode operations.
Another 59 instructions cover vector arithmetic/construction/components,
typed array storage/methods, and internal reference cells. Array/vector compound
writes and numeric list compound updates expand into typed arithmetic and storage
instructions at compile time, without runtime helper calls.
Conditions use fused comparison branches, and numeric `!` preserves unordered
comparisons through result inversion.
The dispatch loop, call frames, eight-byte instructions, and 16-byte value slots
remain in place. Declarations and control flow still use a transitional textual
skeleton. Named scalar assignments, compound assignments, and prefix/postfix updates
now carry their variable targets and flat struct-field paths in IR. Targets are
resolved after the RHS; the stored value or old postfix value is retained as the
expression result. Script calls carry resolved signatures and ordered argument
roots, with explicit numeric/nullable conversions and struct value copies. Return
records retain the owning function, result type, and original source location.
Structs store raw field values; clone helpers preserve nested value copies.
Internal boxed vector locals still support component writes, without exposing
language references. Host/builtin calls, remaining aggregate operations, and
unmigrated selectors keep ordered compatibility paths. The typed VM is not complete.
An early numeric-VM snapshot measured on 2026-10-31 showed a 7.5% increase in the
sum of eight case medians versus its paired generic-op control; see the historical
[measurement report](docs/TypedVmBenchmark.md). A later
[typed IR snapshot](docs/TypedIrBenchmark.md) of commit `fd0e229` measured a sum of
case medians 75.1% above the archived NeoScript baseline in the same session. The
[three-way snapshot](docs/ThreeWayBenchmark.md) of commit `91d6d58` measured a sum
14.3% above Lua, faster than Lua in five of eight cases and slower in `loop_sum`,
`array_rw` and `map_str`. The typed path has not been measured against the
inherited VM since `fd0e229`.
The typed host boundary also supports collections, structs and retained function
values. VS Code uses the same frontend for diagnostics and debugging. The
remaining compiler/VM migration is described in the
[status and roadmap](docs/StaticTyping.md).

This NeoCore build uses bytecode image version `0150`; regenerate cached images.
Generic three-operand arithmetic, compound arithmetic, numeric negation, and fused
read/arithmetic instructions and their handlers are excluded from the NeoCore library. Five generic indexed compound-arithmetic instructions
were also removed. The original NeoScript repository retains its dynamic backend
profile; NeoCore rejects that profile's images and earlier image versions.
Rebuild C++ hosts against the matching headers and library, because the internal
backend compile descriptor carries typed-lowering metadata. The NeoCore C ABI is
a separate API; existing NeoScript C#/C hosts must migrate to its checked contracts.

## Build and run

Requirements: **CMake 3.21+, a C++11-compatible compiler, and a C99 compiler** for
the inherited PCRE2 source. MSVC's default C++14 mode does not raise the source
requirement; non-Windows checks use strict C++11.

```sh
cmake -S . -B Build/neocore -DNEOCORE_BUILD_TESTS=ON -DNEOCORE_BUILD_EXAMPLES=ON
cmake --build Build/neocore --config Release --parallel
ctest --test-dir Build/neocore -C Release --output-on-failure
```

C++ hosts link `NeoCore::Runtime`, which uses `neocore_backend`. Generic
binary/compound arithmetic, numeric negation, fused read/arithmetic, generic logical
operators, increments, runtime type queries and assignment-result reads have been
removed from its opcode set and VM dispatch; typed numeric and vector operations
remain available. Bool logical inversion reads
its statically checked bool value directly.
The frontend no longer exposes an untyped comparison mode. Constant folding uses
compiler operations independently of VM opcode numbers.

`NEOCORE_BUILD_TESTS` controls typed regressions, and `NEOCORE_BUILD_EXAMPLES`
controls the typed game-host example. Enable `NEOCORE_BUILD_UNITY_BRIDGE` for the
C ABI plugin and its checks. `NEOCORE_CONSOLE_HTTP` adds libcurl HTTPS transport
to the console. Dynamic NeoScript test and console targets are no longer built
from this checkout.

For a multi-configuration CMake build such as Visual Studio:

```powershell
Build/neocore/bin/Release/neocore.exe --check Samples/neocore/scripts/fibonacci.nc
Build/neocore/bin/Release/neocore.exe --file Samples/neocore/scripts/fibonacci.nc
```

For the checked-in Visual Studio projects, open **[NeoCore.sln](NeoCore.sln)** at
the repository root. Select `Console` as the startup project. Its project file is
[`Samples/neocore/console.vcxproj`](Samples/neocore/console.vcxproj). The existing
`Samples/console/console.sln` path opens the same NeoCore library and `.nc` console
projects. For the library alone, open **[Windows/NeoCore.sln](Windows/NeoCore.sln)**.
F5 opens the interactive sample menu. Select a sample by number or name, enter
`smoke` to run all offline functional samples, or `q` to quit. The same menu opens
when `neocore` has no command-line arguments. See the
[console feature matrix](docs/SampleMigration.md) for smoke tests, diagnostics,
manual benchmarks, and the remaining NeoCore tooling limitations.
From a Visual Studio developer prompt, run these commands at the root:

```bat
MSBuild NeoCore.sln /p:Configuration=Release /p:Platform=x64 /m
Build\Windows\bin\x64\Release\neocore.exe --check Samples\neocore\scripts\fibonacci.nc
Build\Windows\bin\x64\Release\neocore.exe --file Samples\neocore\scripts\fibonacci.nc
```

[`Windows/NeoCore.vcxproj`](Windows/NeoCore.vcxproj) builds the frontend and its
execution backend into one static library. Reference it from a C++ host and include
`NeoCore.h`; do not also link the inherited `NeoScript` library into that executable.

| Configuration | Library under `Build/Windows/lib` | CRT |
| --- | --- | --- |
| Release x64 | `NeoCore64.lib` | `/MT` |
| Debug x64 | `NeoCore64-d.lib` | `/MTd` |
| Release x86 | `NeoCore32.lib` | `/MT` |
| Debug x86 | `NeoCore32-d.lib` | `/MTd` |

Console output is `Build/Windows/bin/<platform>/<configuration>/neocore.exe`, using
`Win32` for the x86 project platform. Match the host's platform, CRT, and toolset.
The native Windows `.nc` console links libcurl for HTTPS. CMake enables that
optional console transport with `-DNEOCORE_CONSOLE_HTTP=ON`; the core remains
transport-independent. The dynamic `.ns` runner remains in the original NeoScript
repository; this checkout contains only the `.nc` console.

The [NeoCore mini game host](Samples/neocore/minigame/README.md) compiles one `.nc`
program for three independent instances. It checks frame/event dispatch, `sleep`
and resume, service callback reentry, and object-ID validation after destruction.
It is included in the CMake regression suite and in `NeoCore.sln` as
`NeoCoreMiniGame`; no graphics engine is required.

`--check <file>` checks and compiles without executing top-level code.
`--file <file>` runs a checked program. `--emit-backend <file>` prints the checked,
lowered statement skeleton for diagnostics. Its private compiler intrinsics and
numeric IR markers require the matching compile metadata, including
`StaticCompileResult::typedIR`; this output is not standalone NeoScript source.

Execution omits debug metadata by default. Add `--debug` before the mode, for example
`neocore --debug --file source.nc`, to include runtime source-line information.
Frontend compilation errors identify their source location in either mode.
Run timed benchmarks without `--debug`.

[Samples/bench/check.nc](Samples/bench/check.nc) checks small-input results for all
eight benchmark algorithms without timers. [Samples/bench/bench.nc](Samples/bench/bench.nc)
contains the full workloads with explicit conversions and `map.get(key, 0)` for
nonnull reads. These preserve checksums, not identical bytecode. Current VM and
helper costs remain part of the measurement; static typing alone is not a speed claim.

The [first NeoCore benchmark snapshot](docs/FirstNcBenchmark.md) records seven
rotated serial rounds against NeoScript, Lua, and C++, with all eight checksums
matching. It measures the initial source-lowering backend before typed VM instructions.
The historical [typed numeric VM comparison](docs/TypedVmBenchmark.md) records an
early typed/generic comparison in the same executable, alongside NeoScript, Lua,
and C++. The [typed IR snapshot](docs/TypedIrBenchmark.md) measures commit `fd0e229`,
the first build with generic arithmetic compiled out, against the same NeoScript,
Lua, and C++ binaries; its NeoCore sum of case medians is 75.1% above NeoScript
without a paired generic-op control. The [three-way snapshot](docs/ThreeWayBenchmark.md)
measures commit `91d6d58` against Lua and C++ only. Each report identifies its
measured snapshot; none of them reports a later build.

For embedding, `NeoSource/NeoCore.h` is the static entry point in namespace
`NeoCore`. A public `Runtime` owns the shared backend VM and fixed host bindings;
its `Compile`/`LoadImage` methods create programs, and each program's `CreateInstance`
creates independent execution state while sharing compiled code. Hosts with many
script assets use this runtime/program/instance hierarchy. Static `Program::Compile`
remains a convenient isolated entry point. `Run`
executes top-level code once; `Call` checks an exported function's declared
signature before invoking the VM. Host values include scalars, nullable values,
vectors, typed native IDs, collections, structs, and function values. Structs and
vectors copy by value; collections retain shared identity. Retained closures are
bound to their creating instance and become invalid after its reset or destruction.
Host callbacks expose the caller instance separately from the receiver object.
Delayed events retain an instance ID and resolve it through `Runtime::FindInstance`
before invoking a callback. Runtime-owned values remain program-local even when
programs share a VM.
Include `NeoSource/NeoCoreHost.h` and supply `HostBindings` to expose methods and
properties with statically checked signatures. Object resolvers still validate
IDs at runtime. Saved program images include static contracts and debug metadata;
loading rebinds the host schema without recompiling source. Images must come from
a trusted matching compiler.
The underlying NeoScript API remains implementation infrastructure.
It is not a substitute for the NeoCore type-checking boundary.
Embedded editors use `NeoCoreDebugger.h` through `Runtime::GetDebugger()` for
breakpoints, stepping, source locations, call stacks and typed variable snapshots.
Debug controls configure the next stop; execution remains in the instance API.

## Documentation and tools

| Document or tool | Status |
| --- | --- |
| [Static language](docs/StaticTyping.md) | NeoCore rules, current scope, and remaining work |
| [Typed host bindings](docs/HostBindings.md) | C++ schema, object IDs, property calls, and lifetime/reentry contract |
| `NeoSource/NeoCore.h`, `neocore` | Static frontend entry points |
| [Script authoring](docs/ScriptAuthoring.md), [API](docs/API.md) | `.nc` syntax, checked library surface, and examples |
| [Embedding](docs/Embedding.md), [Unity](docs/Unity.md) | C++ hosting, typed C ABI, and Unity integration |
| [Unity package](Unity/NeoCore/README.md) | Managed values, schemas/callbacks, images, module and HTTP adapters |
| [VS Code extension](Tools/vscode-neocore/README.md) | `.nc` highlighting, completion, diagnostics, and DAP debugging |

The inherited [NeoScript engine demo](https://www.youtube.com/watch?v=Wi3EgWx38Vs)
shows the original engine integration, not a completed NeoCore port.

## Author and contact

- **Author:** JRoot
- **Email:** [hangulcode@gmail.com](mailto:hangulcode@gmail.com)

## License and provenance

NeoCore is derived from NeoScript and distributed under the [MIT license](LICENSE).
The original Copyright (c) 2018-2026 NeoScript contributors attribution is retained.

JSON parsing and serialization include **[yyjson 0.13.0](https://github.com/ibireme/yyjson)**,
Copyright (c) 2020 YaoYuan, under the **MIT license**. The unmodified source is in
[`ThirdParty/yyjson`](ThirdParty/yyjson); see its [license](ThirdParty/yyjson/LICENSE).
Include [`THIRD_PARTY_NOTICES.txt`](THIRD_PARTY_NOTICES.txt) with redistributed builds
that use the bundled JSON implementation. See [JSON API and build notes](docs/JSON.md).

Redistributed copies must retain the applicable copyright and license notices.

The core has no HTTP/TLS library dependency. C++ hosts provide `IHttpTransport`
through `CompileOptions::httpTransport`; Unity hosts supply the managed transport
adapter. The NeoCore `.nc` console connects [libcurl](https://curl.se/libcurl/)
for HTTP/HTTPS. Its native Windows build
downloads pinned libcurl 8.23.0 (SHA-256 verified) using a CMake release supporting
Visual Studio 2026, links it statically, and uses Schannel with the Windows
certificate store. Core-only builds do not fetch curl. See
[transport integration](docs/Embedding.md#http-and-https-transport). Include
`THIRD_PARTY_NOTICES.txt` when distributing the console.
The inherited backend uses bundled [PCRE2](ThirdParty/pcre2) for regular expressions;
retain its license notices when distributing it. Checked HTTP and regex APIs are
available to `.nc` scripts; see the [API guide](docs/API.md).
