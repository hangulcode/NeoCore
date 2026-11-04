# Typed sample migration

The runner is `neocore`. This checkout contains the typed samples and tests;
the original `.ns` console and corpus are maintained in the
[NeoScript repository](https://github.com/hangulcode/NeoScript).
These examples use executable paths relative to the repository root:

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
menu. The native Visual Studio console project's F5 arguments enable `--debug`
and pass its script directory through `--sample-dir`, so sample runs include runtime
source locations. A sample can be selected by name
or number and another sample can be run without restarting the console. Invalid
input returns to the prompt; end-of-input exits normally.

The original sample numbers 0 through 20 are retained, including `0` for `hello`
and `1` for the manual `performance` workload. The appended entries are
`21 fibonacci`, `22 structs`, `23 vectors`, `24 particles`, `25 https`,
`26 math`, and `27 json`. The math sample covers scalar, color, random, vector,
and Quaternion functions. The JSON sample shows typed struct and collection
round trips; see [JSON](JSON.md) for schema validation rules.
Use `r` to repeat the last sample, `list` to display the menu again, and `q`, `quit`,
or `exit` to leave. Because `list` is a menu command, select the list sample by its
number (`6`) or run `neocore --run list`.

Use `--sample-dir <directory>` to select the directory containing the `.nc` sample
files. The option applies to the menu, `--run`, `--smoke`, `--run-all`, and
`--debug-smoke`, and may appear before or after the command. Relative paths use the
current working directory. An explicit path takes precedence over automatic
discovery; a missing sample produces a file-read error without falling back to a
different directory. `--file` and `--check` continue to use their own file paths.

Without `--sample-dir`, lookup searches ancestors of the current working directory
and executable directory. The executable embeds no source-checkout sample path.
For an out-of-tree build or copied executable, pass the path at runtime or deploy
the samples in a nearby `scripts` directory. Visual Studio launch settings pass
the source sample directory as an argument; they do not compile it into the binary.

```powershell
neocore --sample-dir "path/to/scripts"
neocore --sample-dir "path/to/scripts" --smoke
neocore --run hello --sample-dir "path/to/scripts"
```

| Command-line mode | Menu command | Behavior |
| --- | --- | --- |
| `--list` | `list` | Display sample numbers, names, and commands without running samples |
| `--run <name-or-number>` | Sample name or number | Run one typed sample and its host checks |
| `--run-all` or `--smoke` | `all` or `smoke` | Run the 26 offline functional samples; skip `performance` and live `https` |
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
workloads use `get(key, 0)` for reads that may miss. This preserves their
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

`--dap` runs the typed stdio debugger adapter: breakpoints, stepping, call stacks,
variables, evaluation and exception details use original `.nc` source locations.
`--lsp` supplies typed completion, hover, signature help and diagnostics, including
open module overlays. These services have separate protocol regressions and are
used by the [NeoCore VS Code extension](../Tools/vscode-neocore/README.md).

The following inherited modes are explicitly rejected with a diagnostic and a
nonzero exit status:

| Mode | Remaining work |
| --- | --- |
| `--globals` | Names alone cannot define static globals. Use `--host host.json` for a typed schema and optional `--host-impl fake.nc` implementations; see [HeadlessHost.md](HeadlessHost.md). |
| `--v2smoke` | The inherited dynamic VM/embedding regression command has no equivalent typed console suite. Typed CTest/API regressions cover their own contracts separately. |

Successfully running the smoke command does not certify those unsupported services.
The dynamic NeoScript console and tests are no longer distributed in this checkout.

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
| regex | Typed match/search/replace, including null search results |
| time | UTC round trips, TimeSpan and Stopwatch |
| performance | Full workload preserved; compilation only in automatic tests |
| regression, literal_totype, cycle | Typed compiler, literal-conversion and cycle checks |

Heterogeneous collections are expressed as structs or nested homogeneous
collections. Recursive dynamic `dump` is replaced by typed string-map and integer-list formatters.
The module example's source-string execution goes through `Program::Compile` and
an exported signature; it does not expose the untyped `system.load`/`pcall` path.

The original dynamic-language rejection cases and heterogeneous graphs are not
silently accepted by the static frontend. Typed counterparts do not imply a
line-for-line conversion of the original stress corpus. That corpus remains in
the NeoScript repository rather than maintaining a second dynamic build here.

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
Both modes register private callbacks with `this.AddEvent("OnUpdate", OnUpdate)`;
host schema event signatures are checked at compile time. The host retains each
callback with its registering instance ID and dispatches it through the shared
runtime. The replay reuses the existing browser viewer format.

The [Unity package](../Unity/NeoCore/README.md) includes `.nc` examples, typed C ABI,
C# ownership/status wrappers, static host schemas, an importer and instance-based
execution. Vectors, typed collections/structs, retained callbacks, modules, HTTP,
JSON and serialized programs use checked boundaries. C ABI and managed P/Invoke
checks can run without an Editor license; actual Editor/PlayMode/domain-reload
execution requires a licensed Unity Editor.

## Current migration verification (2026-10-31)

This snapshot covers the typed distribution after removing the duplicated NeoScript
console, sample/test corpus, Unity package and VS Code extension. The shared VM
implementation remains because NeoCore executes on it.

- Windows Release CTest: 65/65 passed.
- Windows Debug selected regressions: 11/11 passed; this is not a full Debug CTest run.
- Linux strict C++11 build and CTest: 63/63 passed.
- Typed DAP/LSP protocol regression: 62 checks, no failures; the VS Code extension
  regression also passed using the real NeoCore executable.
- Compile definitions and compact arrays: 78 functional checks passed, including
  debug/non-debug execution, source mapping, modules, image values and narrowing.
- Native Windows Release and Debug functional smoke: 24 offline samples passed in
  each configuration. The mini-game acceptance check passed.
- The local HTTP console regression passed all six cases using typed `.nc` scripts.
- Managed P/Invoke checks passed, and managed integration compiled against actual
  Unity Editor assemblies. This does not establish an Editor/PlayMode run.
- Actual Unity Editor/PlayMode execution was blocked by the unavailable license.
  iOS binaries were not built in this environment.

These are functional/build checks. No performance benchmark or live-network result
is claimed by this migration. Earlier snapshots below retain their original scope
and must not be used as current test totals.

## Console menu validation before legacy cleanup (2026-10-31)

This snapshot includes the former dynamic backend suite. Its total is historical;
run the current typed CTest configuration for the post-cleanup test list and results.

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
not run for that snapshot. The current supported modes are described above.

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
