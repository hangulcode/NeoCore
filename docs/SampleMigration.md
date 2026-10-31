# Typed sample migration

The typed runner is `neocore`; the inherited `console` remains a separate `.ns`
regression host. These examples use executable paths relative to the repository
root:

```powershell
Build/neocore/bin/Release/neocore.exe --run delegate
Build/neocore/bin/Release/neocore.exe --run callback
Build/neocore/bin/Release/neocore.exe --debug --run module
```

`--run` supplies the sample's declared host bindings, drives suspended execution,
and checks host-observable results where needed. `--file` is the general `.nc`
runner and does not implicitly declare the sample's `Host` or `g_sData` objects.
Only `callback` registers `Host.mul`, and only `map_callback` registers the
`g_sData.Transform` getter and setter. Other samples compile without a host schema;
their assembly listings contain no sample host-binding globals.
`--check` compiles without executing the source. `--emit-backend` also resolves
imports but its output still needs the frontend's typed IR metadata.

## Console menu and command-line modes

Starting `neocore` without arguments, or with only `--debug`, opens the interactive
menu. The native Visual Studio console project's default F5 arguments are `--debug`,
so sample runs include runtime source locations. A sample can be selected by name
or number and another sample can be run without restarting the console. Invalid
input returns to the prompt; end-of-input exits normally.

The original sample numbers 0 through 20 are retained, including `0` for `hello`
and `1` for the manual `performance` workload. The appended entries are
`21 fibonacci`, `22 structs`, `23 vectors`, `24 particles`, and `25 https`.
Use `r` to repeat the last sample, `list` to display the menu again, and `q`, `quit`,
or `exit` to leave. Because `list` is a menu command, select the list sample by its
number (`6`) or run `neocore --run list`.

Sample lookup searches ancestors of the current working directory and executable
directory. CMake builds also retain a configured source-directory fallback, so an
out-of-tree build can find the checkout's samples. These paths do not bundle the
sample sources into the executable; a copied executable still needs access to them.

| Command-line mode | Menu command | Behavior |
| --- | --- | --- |
| `--list` | `list` | Display sample numbers, names, and commands without running samples |
| `--run <name-or-number>` | Sample name or number | Run one typed sample and its host checks |
| `--run-all` or `--smoke` | `all` or `smoke` | Run the 24 offline functional samples; skip `performance` and live `https` |
| `--debug-smoke` | `debug-smoke` | Run the functional samples with debug information and verify a runtime error's source file and line |
| `--compiler-error-regression` | `compiler-errors` | Compile 24 typed acceptance/rejection cases without executing their programs |
| `--bench-check` | `bench-check` | Check the nine console workloads with 54 reduced, untimed calls |
| `--bench` | `bench` | Run the original nine console workloads converted to typed source, with timing |
| `--pooltrim-check` | `pooltrim-check` | Check reduced memory-pool reclamation and per-call page limits without measuring execution time |
| `--pooltrim` | `pooltrim` | Run manual memory-pool allocation, reclamation, hold-time, and incremental-trimming diagnostics |

`--debug-smoke` checks functional behavior and source mapping. It does not test
breakpoints, stepping, debugger variable inspection, or the debugger protocol.
The typed compiler-error cases likewise do not replace or claim equivalence with
the inherited dynamic-language compiler-error corpus.

`--bench` and `--pooltrim` are manual modes, excluded from smoke runs. Pool trimming
uses the existing runtime pool API; it includes allocation loads, a hold-time wait,
and trimming timing. `--bench-check` exercises the same nine workload sources with
small inputs and checks their results without measuring performance. The typed map
workloads use `get(key, 0)` to produce nonnullable values. This preserves their
calculations and expected checksums, but their typed instructions differ from the
inherited dynamic bytecode.

`--pooltrim-check` uses smaller allocations and a short hold-time wait to check
forced, delayed, and incremental reclamation. It does not measure trimming time or
run the manual mode's idle-call timing loop. Both untimed check modes also accept
`--debug`.

The `--debug`, `--asm`, and `--stats` options may appear before or after a command.
For example:

```powershell
neocore --file Samples/neocore/scripts/fibonacci.nc --debug --asm --stats
neocore --debug --compiler-error-regression
neocore --bench-check
neocore --pooltrim-check
```

`--asm` prints the compiled bytecode diagnostics. `--stats` prints process-global
allocation and pool counters after the individual program and sample host are
destroyed; they are not per-instance counters, and retained pool capacity is not
the same as live script objects. `--file` paths remain relative to the current
working directory unless an absolute path is supplied.

The following inherited modes are explicitly rejected with a diagnostic and a
nonzero exit status:

| Mode | Remaining work |
| --- | --- |
| `--globals` | Names alone cannot define static globals. An embedding host must supply their types and bindings through `HostSchema` and `HostBindings`; a typed CLI declaration format is not implemented. |
| `--dap` | A typed debugger adapter and its breakpoint/stepping/inspection checks are not implemented. |
| `--lsp` | A language service for `.nc` static types, symbols, and diagnostics is not implemented. |
| `--v2smoke` | The inherited dynamic VM/embedding regression command has no equivalent typed console suite. Typed CTest/API regressions cover their own contracts separately. |

The inherited `.ns` console and its tests remain available separately. Successfully
running the typed smoke command does not certify those unsupported services or the
entire legacy regression corpus.

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

## Console menu validation (2026-10-31)

- Native Windows Debug x64 build: no warnings or errors; all 31 CLI scenarios passed.
- Windows Release CTest: 95/95 passed, including the new console regressions.
- Linux console build and four CLI/module/compiler-diagnostic CTests passed.
- Internal-symbol, host-binding and menu regressions also passed on Linux.
- Strict Android NDK C++11 syntax checks passed for the console and diagnostics.
- Functional smoke: 24 offline samples; typed compiler diagnostics: 24 cases.
- Nine benchmark workloads passed 54 reduced, untimed calls. Reduced pool-reclamation
  checks passed with and without debug information.
- Negative module/particle assertion tests verified that false results fail the
  sample command instead of being reported as successful.

Full performance benchmarks, timed pool diagnostics, and live HTTPS requests were
not run for this change. The unsupported modes listed above remain unsupported.

## Earlier sample migration validation (2026-10-31)

This snapshot predates the console menu and CLI-parity additions above; it does
not record validation of those additions.

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
