# Runtime diagnostics and profiling

## Boundary regressions

`neocore_boundary_regression` runs with and without debug metadata, both directly
compiled and saved/reloaded. It combines nested struct/value copies, shared
collection identity, host reentry, suspended return ownership, instance isolation,
program/instance destruction, cancellation, reset, deleted host IDs and recovery
after runtime errors. Integer addition wraps; division by zero, null access and
out-of-range list reads fail. Existing host, host-out, instance, struct and owned
value suites cover the individual contracts in more detail.

## Runtime errors

`Result.error.stackTrace` owns an innermost-first snapshot captured at the first
failure, before native callbacks restore frames or execution unwinds. Each entry
contains the function name, source name, line, column, function ID and instruction
index. Script frames across synchronous host reentry are preserved. Generated
property helper frames with hidden debug locations are omitted. Module names and
lines are translated back to their source files, including after image reload.
The formatted `error.message` also includes these frames and prints `[IP(index)]`
for every frame, even when debug metadata is absent.
The C++ `Error` layout has changed; rebuild embedding applications with the updated
headers and library together. The C ABI's existing error-message accessor is unchanged.

Compile with `debug=true` (CLI `--debug`) for source locations and private function
names. Without metadata, unavailable locations are zero/empty and some names are
`function#ID`; do not interpret these as source lines. Runtime columns are currently
unavailable (zero); compiler diagnostics retain their existing token columns.
The snapshot contains at most 64 frames; deep stacks retain their inner and outer
ends and set `stackTruncated`. OOM reporting remains best effort.

The original error remains first-wins across a failed host callback. A later
independent call starts a fresh error snapshot. Returned snapshots remain valid
after another call, reset, or destruction of the originating instance.

## Function and host profiling

Profiling is off by default. Enable it on an idle `Instance` or the default
instance exposed by `Program`:

```cpp
NeoCore::Error error;
instance->SetProfiling(true, error);
NeoCore::Result result = instance->Call("Update");
NeoCore::ProfileSnapshot profile = instance->ReadProfile();
instance->SetProfiling(false, error); // freezes counters
instance->ResetProfile(error);       // clears counters, keeps enabled/disabled
```

`SetProfiling` and `ResetProfile` reject executing/suspended instances. Read a
snapshot between calls or while suspended; it contains completed accounting
intervals, does not advance clocks, and is not a live cross-thread API. The runtime's
existing one-thread rule applies. `Instance::Reset` replaces the VM worker and thus
discards profiling data and disables it. Cancel preserves accumulated counters.

Each interpreter entry selects one of three loop bodies:

| Loop | Selected when | Runtime controls |
| --- | --- | --- |
| Fast | No active debugger, profiling or execution limit | No per-op debugger/limit checks or direct-call/return profiling checks |
| Controlled | Debugger inactive; profiling or an execution limit enabled | Profiling and limits are independent runtime flags |
| Debug | Breakpoints, stepping or a pause request activate the debugger | Debug checks; profiling and limits remain independently optional |

Profiling and execution-limit combinations share the Controlled or Debug body;
they do not generate additional template instantiations or enable one another.
A time-sliced run with profiling disabled pays the shared branch but does not
collect timings. A profiling-only run does not check breakpoints or enforce a
limit. Retained disabled collectors remain frozen. Flags are selected again on
resume and host reentry, preserving the existing nested-execution budget rules.
Collector functions keep their internal enabled checks because inconsistent host
profiling state can disable collection during execution. Shared host,
function-value and coroutine boundaries respect the runtime setting. Bytecode
and frame layout are unchanged.

Each entry records:

- `calls`: script frame entries or registered host boundary attempts, including
  failed calls. Resuming a suspended function does not increment it.
- `selfNanoseconds`: active elapsed time excluding child script/registered host
  calls. Built-in API work remains in the calling script's time.
- `inclusiveNanoseconds`: active elapsed time including child calls. Overlapping
  recursive invocations of the same entry count once, including mutual recursion
  and host reentry. Every invocation still increments `calls`. Each entry's
  inclusive time is at most `activeNanoseconds`; inclusive times of different
  entries overlap and must not be summed as total elapsed time.
- Host methods/properties use `Type.Member`, `Type.Property [get]` or `[set]`.
  Their self time includes binding conversion, resolution and callback work;
  same-instance script reentry is counted as a child, not host self time.

`activeNanoseconds` is the sum of self times. Time between Run/Resume slices,
cooperative sleep and debugger pauses is excluded. Execution in another instance
is separate; a synchronous call into another instance remains part of the caller's
host boundary time. Generated script helper functions can have their own entries.
Snapshots sort by self time descending, then calls descending and name. Self share
is `100 * selfNanoseconds / activeNanoseconds` (zero for an empty snapshot).

The collector instruments frame/context transitions and host boundaries, not every
opcode. Ordinary calls/returns update only the changed profiling frames and use a
function-ID-indexed entry table. Full stack reconstruction is reserved for execution
start/resume and coroutine switches. Self time accrues only to the active top frame;
inclusive intervals close when an entry's active recursion depth reaches zero.
Run slices close accounting intervals without counting resumed frames as new calls.
It uses a monotonic wall clock, so OS scheduling delays during active calls
are included. Collection overhead is included too: these numbers locate expensive
calls and are **not** an uninstrumented benchmark. Closing a host profiling scope
does not allocate; an inconsistent profiling stack disables collection and marks
`complete=false`.

CLI example:

```sh
neocore --debug --profile --file example.nc
```

The table includes calls, self/inclusive milliseconds, self percent and source
location, and is printed on runtime failure too. `--profile` alone preserves
non-debug compilation; add `--debug` for private names and locations. No timing
comparison benchmark is required to run the functional regression suites.

```sh
ctest --test-dir Build/neocore -C Release -R "neocore_(boundary|diagnostics|profile)_regression" --output-on-failure
```

## Verification (2026-11-15)

- Windows x64 Release: full CMake build and all 89 CTest entries passed, including
  debugger protocol, CLI, host/schema and C ABI integration regressions.
- New native suites: boundary 112 checks, runtime diagnostics 36 checks, profiling
  156 checks; the additional CLI regression checks table ordering, counts, shares,
  failure output and rejected command combinations.
- Android ARM64 NDK Clang: changed core translation units, console and new native
  tests passed C++11 syntax checks with C++14/C++17 extensions treated as errors.
- No comparative timing benchmark or engine/editor UI integration was performed.
