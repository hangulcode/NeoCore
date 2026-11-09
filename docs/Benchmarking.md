# Benchmarking NeoCore

How timed measurements of NeoCore are taken, what they can and cannot show, and how
the readme table is refreshed. Follow this page for every number that is reported or
compared; the rules exist because code placement and session conditions have produced
differences of 20-30% between builds that execute the same instructions.

## What is measured

- **Suite:** [`Samples/bench/bench.nc`](../Samples/bench/bench.nc) runs eight cases
  (`loop_sum`, `float_math`, `func_call`, `fib_recursive`, `array_rw`, `map_str`,
  `string_ops`, `particles`). Each case runs one warmup plus `REPS` (five) repetitions
  inside the process and prints the best one as `case|microseconds|checksum`.
- **References:** `bench.ns` in the NeoScript repository, [`bench.lua`](../Samples/bench/bench.lua)
  and [`bench.cpp`](../Samples/bench/bench.cpp) implement the same algorithms statement
  for statement with the same repetition rule and print the same rows. Build the C++
  reference with [`build_cpp.bat`](../Samples/bench/build_cpp.bat) (`cl /O2 /std:c++17`).
- **Checksums:** every case prints a checksum; all processes of a session must agree,
  and the runner stops on the first mismatch. [`check.nc`](../Samples/bench/check.nc)
  checks the eight algorithms on small inputs without timers (`neocore --bench-check`).
- Other scripts in `Samples/bench` (`bench_call.nc`, `bench_dense.nc`, `mapbench.nc` and
  their `_check` files) are focused workloads for call, dense numeric and map paths and
  follow the same output format; they are not part of the readme table.

## Before a timed run

1. Ask the user before starting. Video playback, other builds and background jobs must
   stop; the machine is otherwise idle for the whole session.
2. Build the console in Release with the same options for every build in the session.
   `NEOCORE_ENABLE_IPO` must be the same for all of them (off by default); IPO and non-IPO
   builds differ in either direction, so state which one was measured.
3. Run without `--debug`. Debug metadata changes the dispatch loop and is not the
   shipping configuration.
4. Do not run builds or tests while a session is in progress; run them before or after.

## Running the protocol

```sh
python Tools/bench/measure.py --rounds 5 --label <name> \
    NeoCore=Build/neocore/bin/Release/neocore.exe [Previous=<path to a worktree build>]
```

- The first `NAME=exe` is the build the ratios describe. More NeoCore builds (the parent
  commit, a variant) can be added; NeoScript, Lua and the C++ reference are included
  unless passed as `none`. Default paths are the NeoScript checkout next to this
  repository, `D:\util\lua-5.5_Win64_bin\lua55.exe` and `Samples/bench/bench_cpp.exe`;
  override them with the options or the `NEOCORE_BENCH_*` environment variables.
- One warmup round is followed by the measured rounds. In every round each executable
  runs once, in an order rotated by one position per round, so no program always
  follows the same predecessor.
- Per case, the value is the **median of the per-round results**; the per-round
  **min-max range** is kept beside it. A full five-round session with four executables
  takes about 80 seconds.
- Outputs go to `Build/bench-sessions/<label>/`: every process's stdout,
  `measurements.json` and `summary.md` with the readme-format table. The record also
  stores each executable's SHA-256 and each NeoCore build's dispatch offset.

## Reading the result

- **Ratios** are NeoCore's time divided by the other program's time, so `0.50x` means
  that program needed twice NeoCore's time and `11.5x` means it finished 11.5 times
  faster. The same rule applies to the sum row.
- **Sum of case medians** is the headline number. It is not process wall time: startup
  and compilation are excluded, and the three largest cases (`loop_sum`, `float_math`,
  `func_call`) make up about 60% of it, so a 25% change in `map_str` moves the sum by
  about 1.5%.
- **A difference is real only when the per-round ranges of the two builds separate.**
  Overlapping ranges are reported as "within one round's spread", whatever the medians say.
- **Sessions are not comparable with each other.** The references themselves moved by
  about 3% between two sessions on the same machine within a day. Compare builds only
  inside one session; compare sessions only through the references measured in both.
- The C++ reference is a ceiling, not a peer (its `map_str` uses
  `std::unordered_map<std::string, int>` and is slower than the scripts). Lua and
  NeoScript are separate binaries with their own build settings.

## Code placement

The dispatch sequence at the top of `RunInternal` is about 45 bytes. Where it starts
inside a 64-byte line changes some cases by up to 25% with byte-identical handler code:
`float_math`, `map_str` and `string_ops` have been sensitive, integer loops and calls
have not. Two builds of near-identical source can therefore differ by far more than a
real change.

- Report the offset with `python Tools/bench/dispatch_offset.py <neocore.exe>`; the last
  of the four sites is the one `neocore --file` runs. The runner stores it in the record.
- **Compare two NeoCore builds at equal offsets.** To move a build without changing its
  logic, insert `if (!TIMEOUT && !DEBUG) { __nop(); ... }` (k nops, `#include <intrin.h>`)
  right after the `_isInitialized` check at the top of `RunInternal` and rebuild the
  console; 16 nops move the offset by 16 bytes. Build the variants that land on the same
  offsets as the other build, time them alternately in one session, and remove the
  padding afterwards. Measure all four offsets (0, 16, 32, 48).
- A change that is faster at every offset is a real change. A change that is faster at
  one offset and slower at another is placement, not source, and must not be reported
  as an improvement.

## Quick checks during development

- `measure.py --quick NAME=exe NAME=exe` compares NeoCore builds only: three measured
  rounds after the warmup, and a copy of `bench.nc` with `REPS` set to three, written
  into the session directory. Checksums are unchanged. A pair of builds takes about
  40 seconds instead of two minutes.
- `--reps` and `--rounds` set the two counts separately; a `REPS` other than five
  disables the references, because their sources keep five.
- Quick runs decide whether to keep working on a change. The readme and its record use
  only the full protocol.
- Regressions and other checks may run concurrently with each other, but never while a
  timed session is running.

## Refreshing the readme table

1. Run the full protocol (five rounds) with the current tree, the previous readme
   commit's console (built in a worktree with the same options), NeoScript, Lua and C++.
2. Replace the table in `ReadMe.md` under "Performance test results" with the first
   NeoCore build's row values: integer milliseconds, one decimal for C++, ratios with
   two decimals. Update the bullets below it only with facts the session shows (which
   cases are faster, which ranges overlap).
3. Rewrite [`docs/FourWayBenchmark.md`](FourWayBenchmark.md) as the record of that
   session: method, environment, executables with hashes and dispatch offsets, medians,
   per-round ranges, same-offset comparison if one was made, observations, and what the
   comparison cannot isolate. Earlier records stay in the repository history.
4. State the dispatch offsets of the compared NeoCore builds. If they differ, say which
   cases that affects and add the same-offset comparison.
5. Commit the code change and the readme/record refresh separately; commit messages
   carry no performance numbers.
