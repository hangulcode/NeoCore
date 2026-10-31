# Typed sample migration

Run these commands from the repository root. The typed runner is `neocore`;
the inherited `console` remains a separate `.ns` regression host.

```powershell
Build/neocore/bin/Release/neocore.exe --run delegate
Build/neocore/bin/Release/neocore.exe --run callback
Build/neocore/bin/Release/neocore.exe --debug --run module
```

`--run` supplies the sample's declared host bindings, drives suspended execution,
and checks host-observable results where needed. `--file` is the general `.nc`
runner and does not implicitly declare the sample's `Host` or `g_sData` objects.
`--check` compiles without executing the source. `--emit-backend` also resolves
imports but its output still needs the frontend's typed IR metadata.

## Console examples

All typed sources are in [Samples/neocore/scripts](../Samples/neocore/scripts).
The legacy misspelling `contailer` is retained as the sample name.

| Original example | Typed counterpart and behavior |
| --- | --- |
| hello | Explicit declarations, functions, loops, printing |
| string | UTF-8 length, substring, search, splitting, replacement |
| list | Homogeneous lists, mutations, removal, sorting |
| map | Nested typed maps and a typed comparison callback |
| contailer | Nested lists, sets, typed iteration and set algebra |
| callback | C++ calls exported `Sum`; script calls declared `Host.mul` |
| map_callback | Declared Vector3 `g_sData.Transform` getter and setter |
| 9_times | Exported typed multiplication-table function |
| slice_run | Instruction-budget suspension and result after repeated Resume |
| time_limit | Bounded exported call, sleep and result after Resume |
| divide_by_zero | Expected runtime error checked by the sample runner |
| delegate | Named function values, anonymous functions, reassignment and captures |
| coroutine | Signature-bound creation, resume, yield and sleep |
| module | Typed mod1/mod2/dump imports and separately checked source compilation |
| http | Deterministic injected GET/POST transport; no external network |
| regex | Typed match/search/replace, including nullable search results |
| time | UTC round trips, TimeSpan and Stopwatch |
| performance | Full workload preserved; compilation only in automatic tests |
| regression, literal_totype, cycle | Typed counterparts; original dynamic stress suites remain separate |

Heterogeneous collections are expressed as structs or nested homogeneous
collections. Recursive dynamic `dump` is replaced by typed string-map and integer-list formatters.
The module example's source-string execution goes through `Program::Compile` and
an exported signature; it does not expose the untyped `system.load`/`pcall` path.

The original `.ns` regression, literal-conversion, GC, debugger and compiler-error
fixtures are intentionally retained. Their dynamic-language rejection cases and
heterogeneous graphs are not silently accepted by the static frontend. Typed
counterparts do not imply a line-for-line conversion of that legacy stress corpus.

## HTTP and HTTPS

`CompileOptions::httpTransport` installs the existing cancellable transport
interface. The core does not link an HTTP implementation. `--run http` uses an
offline test transport and validates callback counts, data and headers.

The checked-in Visual Studio console project links the existing libcurl transport.
Its live HTTPS example is:

```powershell
Build/Windows/bin/x64/Release/neocore.exe --file Samples/neocore/scripts/https.nc
```

For CMake, install a compatible libcurl and configure
`-DNEOCORE_CONSOLE_HTTP=ON`. CMake leaves this optional dependency off by default;
without it the live HTTPS example reports an unconfigured transport. TLS checks
remain enabled. Live network availability is not an automated regression assertion.

## Modules and execution limits

`CompileOptions::moduleLoader` resolves module names to `.nc` source and a canonical
source name. The compiler checks the complete bundle, respects `public` visibility,
rejects cycles and preserves import-site initialization order. The console searches
the importing directory and `Lib` directories along its ancestor chain. Hosts can
provide a virtual or application-specific loader. Runtime structured error locations
are mapped back to the originating module when debug information is enabled.

`Call(name, arguments, ExecutionLimits)` applies a cooperative time-slice duration and/or instruction
budget to the exported function. Its preliminary top-level initialization retains
the existing unlimited execution policy. `Resume(deltaTime, instructionBudget)`
sets the next slice's instruction budget; zero means unlimited. Expiring a time
slice suspends execution rather than raising a deadline error. Cooperative sleep
uses wall-clock time. A suspended result must be resumed before another call begins.
These adapters keep the existing shared program, instance workers and context pool.

## Benchmarks, mini game and Unity

The eight-case `bench.nc`, `bench_dense.nc`, `bench_call.nc` and `mapbench.nc` are
typed counterparts of the inherited benchmarks. Their `check.nc` or `*_check.nc`
files exercise reduced inputs with checksums and no timing. Automatic tests compile
the full workloads but do not run them as performance measurements.

The [typed mini game](../Samples/neocore/minigame/README.md) keeps its shared-program
instance acceptance scenario and adds the original movement/replay demonstration.
The replay reuses the existing browser viewer format.

The [typed Unity package](../Unity/NeoCore/README.md) is separate from the legacy
NeoScript package. It includes a real `.nc` Basic sample, typed scalar C ABI,
C# ownership/status wrappers and an `.nc` importer. Vector calculations stay in
the script and exported scalar components update the Unity transform. C ABI and
managed P/Invoke checks can run without an Editor license; PlayMode/domain-reload
checks require a licensed Unity Editor.

## Validation snapshot (2026-10-31)

- Windows Release and Linux CTest: 92/92 passed on each platform.
- Windows Debug CTest: 91/91 passed, excluding the inherited
  `neoscript_host_argument_buffer_regression` STL-proxy allocation-count test.
- Checked-in Visual Studio Release x64 solution: library, console and mini game built;
  module, coroutine, HTTP and original-motion modes executed successfully.
- Native console HTTPS request succeeded with the existing libcurl transport.
- Strict Android NDK C++11 syntax checks passed for the changed core and console code.
- Typed Unity C smoke and actual managed P/Invoke smoke passed. Unity-facing C#
  compiled against the installed Editor API with no warnings or errors.
- Actual Unity PlayMode/domain-reload execution could not start because the installed
  Editors lacked a valid license. The prepared check remains available once licensed.

Full performance workloads were compiled only; reduced checksum suites were run.
No performance benchmark results are claimed by this migration.
