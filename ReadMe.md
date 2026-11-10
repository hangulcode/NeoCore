# NeoCore — statically typed scripting

NeoCore is an independent scripting-language project derived from
[NeoScript](https://github.com/hangulcode/NeoScript). It keeps the NeoScript VM as
its execution backend while adding explicit variable types, C-style function
declarations, and compile-time checking. The static language is under active development.

**NeoCore source files use `.nc`.** Dynamic `.ns` samples and regression fixtures
remain in the separate NeoScript repository. NeoCore's editor protocols compile
through its static frontend; they do not accept dynamic NeoScript source.

**[Language design and implementation status](docs/StaticTyping.md)** ·
**[Typed host bindings](docs/HostBindings.md)** ·
**[Host schemas and headless execution](docs/HeadlessHost.md)** ·
**[Converted samples and run commands](docs/SampleMigration.md)** ·
**[Source repository](https://github.com/hangulcode/NeoCore)**

## Why NeoCore

NeoCore started from NeoScript to pursue higher execution performance and more
reliable scripts through static typing. Its compiler resolves types, function
signatures, member access, and applicable operations before execution, which moves
detectable errors to compile time and removes repeated runtime type checks and name
lookups. Automatic memory management remains: managed values and containers keep
reference counting and cycle collection, and hosts control the collection safe points.

- **Compile-time contracts:** explicit types, function signatures, value semantics,
  and host schemas make invalid operations diagnosable before a script runs.
  Unsupported features fail explicitly instead of silently becoming dynamic.
- **Efficient execution:** resolved types become dedicated VM instructions.
  Performance claims must come from paired measurements, not from static typing alone.
- **Safe host integration:** member names and signatures resolve during compilation;
  object IDs and capabilities are validated during execution. Engine objects remain
  owned by the host.
- **Defined behavior:** evaluation order, numeric rules, value/reference semantics,
  and useful error locations are preserved as the compiler and VM evolve.
- **Portable embedding:** a bytecode VM with a C++11-compatible core and a clear
  boundary between the scripting language and its host engine.

These are development goals; the current state is described below.

## Current state

The frontend checks scalar/vector declarations, structs, typed arrays, nested
lists/maps, sets, typed function values and closures, named functions, calls and
returns, including forward calls and recursion. It rejects `var`, `fun`, undeclared
types, and implicit dynamic fallback. Numeric, bool, vector and struct values are
never null and are initialized by default; strings, collections, function values
and host objects may be null and default to null. Using a null reference is a
runtime error.

There are no pointers or `T&` declarations, parameters, or returns. Structs and
vectors copy by value, including nested struct fields; functions return modified
values for the caller to assign. Vectors have float32 components and checked
component writes; Quaternion uses `w, x, y, z` order. Built-in imports expose
checked math, string/regex, collection, time, JSON and HTTP APIs; script modules,
typed foreach and signature-bound coroutines are supported. See the
[struct](Samples/neocore/scripts/structs.nc), [vector](Samples/neocore/scripts/vectors.nc)
and [particles](Samples/neocore/scripts/particles.nc) samples and the [JSON guide](docs/JSON.md).

Typed host schemas declare object types, structs, globals, methods, and read/write
properties before compilation. Calls go to a fixed binding without a per-member name
lookup, and hosts resolve object IDs when a member call begins. See the
[binding contract and current limits](docs/HostBindings.md).

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
**`int32` and `float32` are not scalar types that scripts can declare;** 32-bit
storage exists only in `array32<int>` / `array32<float>` and vector components.
Compact-array reads produce ordinary int64 / float64 values; writes check the int32
range or round to float32.

Checked expressions lower through a typed intermediate representation that keeps
resolved types and source locations. Numeric operations, scalar writes, script calls
and returns become dedicated typed instructions, values live in eight-byte untagged
slots whose storage the compiler decides, and the loader further specializes
instructions at load time. Declarations, control flow, host/builtin calls and some
aggregate operations still use ordered transitional paths, so the typed VM is not
finished; see the [status and roadmap](docs/StaticTyping.md) and
[typed storage](docs/TypedStorage.md).

The current bytecode image version is `0165`. Earlier images and the dynamic
NeoScript profile are rejected, so regenerate cached images and rebuild C++ hosts
against matching headers. The NeoCore C ABI is a separate API; NeoScript C#/C hosts
must migrate to its checked contracts.

## Build and run

Requirements: **CMake 3.21+, a C++11-compatible compiler, and a C99 compiler** for
the bundled PCRE2 source. MSVC's default C++14 mode does not raise the source
requirement; non-Windows checks use strict C++11.

```sh
cmake -S . -B Build/neocore -DNEOCORE_BUILD_TESTS=ON -DNEOCORE_BUILD_EXAMPLES=ON
cmake --build Build/neocore --config Release --parallel
ctest --test-dir Build/neocore -C Release --output-on-failure
```

| CMake option | Effect |
| --- | --- |
| `NEOCORE_BUILD_TESTS` | Typed regressions |
| `NEOCORE_BUILD_EXAMPLES` | Console and the typed game-host example |
| `NEOCORE_BUILD_UNITY_BRIDGE` | C ABI plugin and its checks |
| `NEOCORE_CONSOLE_HTTP` | libcurl HTTPS transport for the console |
| `NEOCORE_ENABLE_IPO` | Whole-program optimization; off by default because it relinks every test slowly. Compare timings only between builds with the same setting |
| `NEOCORE_STATIC_CRT` | Static MSVC runtime; on when NeoCore is the top-level project, and an explicit `CMAKE_MSVC_RUNTIME_LIBRARY` is kept |

C++ hosts link `NeoCore::Runtime`. For a multi-configuration CMake build:

```powershell
Build/neocore/bin/Release/neocore.exe --check Samples/neocore/scripts/fibonacci.nc
Build/neocore/bin/Release/neocore.exe --file Samples/neocore/scripts/fibonacci.nc
```

For Visual Studio, open **[NeoCore.sln](NeoCore.sln)** at the repository root and
select `Console` as the startup project; **[Windows/NeoCore.sln](Windows/NeoCore.sln)**
opens the library alone. F5, or `neocore` without arguments, opens the interactive
sample menu: select a sample by number or name, enter `smoke` to run all offline
functional samples, or `q` to quit. `neocore --sample-dir "path/to/scripts" --run hello`
chooses the sample directory explicitly. From a developer prompt:

```bat
MSBuild NeoCore.sln /p:Configuration=Release /p:Platform=x64 /m
Build\Windows\bin\x64\Release\neocore.exe --file Samples\neocore\scripts\fibonacci.nc
```

[`Windows/NeoCore.vcxproj`](Windows/NeoCore.vcxproj) builds the frontend and backend
into one static library. Include `NeoCore.h` and do not also link the inherited
`NeoScript` library. The console is
`Build/Windows/bin/<platform>/<configuration>/neocore.exe` (`Win32` for x86).

| Configuration | Library under `Build/Windows/lib` | CRT |
| --- | --- | --- |
| Release x64 | `NeoCore64.lib` | `/MT` |
| Debug x64 | `NeoCore64-d.lib` | `/MTd` |
| Release x86 | `NeoCore32.lib` | `/MT` |
| Debug x86 | `NeoCore32-d.lib` | `/MTd` |

Console modes: `--check <file>` compiles without running; `--file <file>` runs a
checked program; `--emit-backend <file>` prints the lowered statement skeleton for
diagnostics. Execution omits debug metadata unless `--debug` comes first, for example
`neocore --debug --file source.nc`; run timed benchmarks without it. The
[console feature matrix](docs/SampleMigration.md) lists smoke tests and remaining
tooling limits. The [mini game host](Samples/neocore/minigame/README.md) runs one
`.nc` program in three instances to check frame and event dispatch, `sleep`,
callback reentry, and object-ID validation, without a graphics engine.

[`Samples/bench/check.nc`](Samples/bench/check.nc) checks all eight benchmark
algorithms on small inputs without timers; [`Samples/bench/bench.nc`](Samples/bench/bench.nc)
holds the timed workloads. Earlier measurement snapshots stay under `docs/`
(`FirstNcBenchmark`, `TypedVmBenchmark`, `TypedIrBenchmark`, `ThreeWayBenchmark` 1–3);
each describes only the build it measured.

### Performance test results

Sources: [`Samples/bench/bench.nc`](Samples/bench/bench.nc) (NeoCore) ·
`bench.ns` (NeoScript, in its own repository) ·
[`bench.lua`](Samples/bench/bench.lua) · [`bench.cpp`](Samples/bench/bench.cpp) — the
four files implement the **same eight algorithms**, statement for statement, and
every run checks the same eight checksums.

```text
CPU       : Intel Core i7-12700F (12C/20T)      RAM : 64 GB
OS        : Windows 11 Pro 64-bit (build 26200)
NeoCore   : CMake Release, MSVC 19.51 /O2 /Ob2 /MT, static CRT, no IPO (`neocore.exe`)
NeoScript : console.exe, Release x64, whole-program optimization off
Lua       : Lua 5.5.0 (official Win64 binary)
C++       : MSVC 19.51 /O2 /std:c++17 (x64)
```

Lower is better. **ms** is the median of five rotated serial rounds after a warmup;
each process reports the best of five in-process repetitions. The `vs NeoCore`
columns give NeoCore's time divided by that language's time, so `0.49x` means the
language needed about twice NeoCore's time. Per-round ranges, executable hashes and
the dispatch-loop position are in [docs/FourWayBenchmark.md](docs/FourWayBenchmark.md);
the procedure and its rules are in [docs/Benchmarking.md](docs/Benchmarking.md).

| Benchmark | What it stresses | NeoCore (ms) | NeoScript (ms) | Lua (ms) | C++ (ms) | Lua vs NeoCore | C++ vs NeoCore |
| :-------- | :--------------- | -----------: | -------------: | -------: | -------: | -------------: | -------------: |
| `loop_sum` | integer loop, VM dispatch floor | **133** | 178 | 195 | 12.6 | 0.68x | 10.55x |
| `float_math` | float mul/add/sub chain | **148** | 180 | 311 | 58.5 | 0.48x | 2.53x |
| `func_call` | script function call overhead | **110** | 134 | 157 | 4.3 | 0.70x | 25.62x |
| `fib_recursive` | recursion, fib(32) | **79** | 86 | 89 | 7.0 | 0.89x | 11.29x |
| `array_rw` | sequential list write + read | **38** | 42 | 48 | 2.5 | 0.79x | 15.33x |
| `map_str` | string-key hash lookup | **32** | 37 | 40 | 62.5 | 0.80x | 0.51x |
| `string_ops` | string build + length | **22** | 51 | 145 | 13.7 | 0.15x | 1.61x |
| `particles` | game-style float + list sim | **39** | 47 | 49 | 3.4 | 0.80x | 11.33x |
| **total** | | **601** | 755 | 1034 | 164.6 | 0.58x | 3.65x |

- NeoCore is faster than NeoScript, its dynamically typed predecessor, in all eight
  cases with separated per-round ranges. The sum of case medians is 20% below
  NeoScript and 42% below Lua.
- `map_str` is the one case where C++ is slower: its reference uses
  `std::unordered_map<std::string, int>` and hashes on every lookup, while the
  scripts hash interned keys once.
- Code placement moves single cases between otherwise identical builds by up to
  about 25%, so differences are evidence of a change only when the per-round ranges
  separate. Numbers from other sessions or machines are not comparable.

## Embedding

`NeoSource/NeoCore.h` is the static entry point in namespace `NeoCore`. A `Runtime`
owns the shared backend VM and fixed host bindings; `Compile`/`LoadImage` create
programs, and each program's `CreateInstance` creates independent execution state
that shares compiled code. `Run` executes top-level code once; `Call` checks an
exported function's declared signature before invoking it. Host values include
scalars, null references, vectors, typed native IDs, collections, structs and
function values; structs and vectors copy by value, and collections keep shared
identity. Retained closures are bound to their creating instance and become invalid
after its reset or destruction. Host callbacks receive the caller instance separately
from the receiver object, and delayed events keep an instance ID that they resolve
through `Runtime::FindInstance` before calling back. Runtime-owned values stay
program-local even when programs share a VM. Supply `HostBindings` from `NeoSource/NeoCoreHost.h` to expose
statically checked methods and properties; saved images rebind the host schema
without recompiling and must come from a trusted matching compiler. Embedded editors
use `Runtime::GetDebugger()` for breakpoints, stepping, call stacks and typed
variable snapshots. Details: [Embedding](docs/Embedding.md),
[host bindings](docs/HostBindings.md).

## Documentation and tools

| Document or tool | Status |
| --- | --- |
| [Architecture](docs/Architecture.md) | Pipeline from source to execution, VM and API class relationship diagrams, source map; [Architecture.html](docs/Architecture.html) is the same content with page styling for local viewing |
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
adapter. The `.nc` console connects [libcurl](https://curl.se/libcurl/) for
HTTP/HTTPS: its native Windows build downloads pinned libcurl 8.23.0 (SHA-256
verified), links it statically, and uses Schannel with the Windows certificate store.
Core-only builds do not fetch curl. See
[transport integration](docs/Embedding.md#http-and-https-transport), and include
`THIRD_PARTY_NOTICES.txt` when distributing the console. The inherited backend uses
bundled [PCRE2](ThirdParty/pcre2) for regular expressions; retain its license notices
when distributing it.
