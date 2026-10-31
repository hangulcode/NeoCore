# Three-way VM benchmark snapshot — 2026-10-31

This records the eight-case benchmark for the NeoCore build at commit
`91d6d58ad98211b74036b973930f18508810a6bc` (`91d6d58`) against Lua 5.5 and C++
in one session. This build follows the removal of the dynamic arithmetic
opcodes, the generic logical and increment instructions and the legacy
arithmetic compile flag, plus the module struct visibility, set element and
function-value fixes made the same day. **The NeoCore sum of case medians is
14.3% above Lua in this session; NeoCore is faster than Lua in five of the
eight cases and slower in three.** These are measured results for this build,
not a conclusion about static typing in general. The archived NeoScript console
was not part of this session.

## Method

- Windows x64 session from 2026-10-31 14:48:09 to 14:49:47 UTC
  (23:48:09 to 23:49:47 in Korea). No other builds or media playback ran.
- One serial warmup round followed by seven measured rounds, rotating the
  executable order every round. No executables ran concurrently.
- Each benchmark script reports its best of five internal repetitions. The
  table takes the median of the seven process-level results, excluding the warmup.
- All eight checksums matched across **all 24 process executions**, including warmup.
- NeoCore ran without `--debug`. Script timers exclude process startup and compilation.
- The session record is `Build/nc-3way-benchmark/measurements.json`; per-process
  output files and the measurement script are in the same ignored build directory.

## Results

Times are milliseconds; lower is faster. Three decimal places retain the recorded
microsecond units and do not imply that every timer has microsecond accuracy.

| Case | NeoCore | Lua | C++ | NeoCore / Lua | Matching checksum |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 343.000 | 192.000 | 12.683 | 1.79× | 12497500 |
| float_math | 199.999 | 309.999 | 58.468 | 0.65× | 21666 |
| func_call | 143.000 | 154.999 | 4.367 | 0.92× | 12502500 |
| fib_recursive | 80.999 | 87.000 | 7.005 | 0.93× | 2178309 |
| array_rw | 60.999 | 45.999 | 2.498 | 1.33× | 25493856 |
| map_str | 237.999 | 38.999 | 62.109 | 6.10× | 90000 |
| string_ops | 57.000 | 142.999 | 13.424 | 0.40× | 438890 |
| particles | 44.999 | 49.999 | 3.426 | 0.90× | 10461222 |
| Sum of case medians | 1167.995 | 1021.994 | 163.980 | 1.14× | — |

The sum summarizes these eight workloads; it is not a process duration or a
general language-performance score. Seven of the eight NeoCore cases vary by
less than 9% between their minimum and maximum across the measured rounds.
`particles` is the exception: rounds 4 and 7 recorded 68.999 and 73.999 ms
against 43.000–45.999 ms in the other five rounds, so its median is robust but
its spread is not, and that case should be re-measured before any claim about it.

The three slow cases concentrate in two places. `loop_sum` and `array_rw`
are plain integer loops and array element reads and writes, where NeoCore spends
1.3–1.8× Lua's time. `map_str` is dominated by the `map.get(key, 0)` fallback
path, as in the earlier snapshots, and remains the largest gap at 6.1×.

## What this comparison includes

- The benchmark sources are the current `Samples/bench/bench.nc`, `bench.lua`
  and `bench.cpp`; their hashes are unchanged from the
  [typed IR snapshot](TypedIrBenchmark.md), so the workloads are identical.
- `bench.nc` keeps the original algorithms and iteration counts with explicit
  types, `map.get(key, 0)` and `tostring`, as described in
  [the first snapshot](FirstNcBenchmark.md).
- NeoCore uses the CMake Release configuration (`/O2 /Ob2 /DNDEBUG`, MSVC).
  This build no longer contains the dynamic arithmetic opcodes or the compile
  flag that used to exclude them. Lua is the prebuilt 5.5 Win64 binary. C++ is
  `bench.cpp` built with `cl /O2 /std:c++17`; the executable is the same file as in
  the typed IR snapshot. Build configuration and code layout differ between these
  executables and can affect the comparison.

## What this comparison cannot isolate

- The archived NeoScript console did not run in this session, so this report has
  no NeoScript column and makes no claim about the inherited dynamic VM.
- The [typed IR snapshot](TypedIrBenchmark.md) measured commit `fd0e229` in a
  different session. Separate sessions are not proof of a change. As an
  observation only: the Lua and C++ columns agree closely between the two
  sessions (Lua sums of 1020.996 and 1021.994 ms), while the NeoCore sum moved
  from 1279.993 to 1167.995 ms, with `float_math` (257.000 to 199.999 ms) and
  `map_str` (267.999 to 237.999 ms) accounting for most of the difference and
  `loop_sum` and `array_rw` unchanged. Attributing that movement to any of the
  commits between the two builds requires a paired run of both executables in
  one session.
- There is still no paired generic-op control; the generic arithmetic path no
  longer exists in the source, so the historical
  [typed numeric VM comparison](TypedVmBenchmark.md) remains the only such
  measurement.

Per-case instruction counts and dispatch costs were not collected. The next
useful step for `loop_sum` and `array_rw` is a bytecode dump of their inner loops
alongside a paired measurement in one session, before any change is claimed as an
improvement or a regression cause.

## Artifact identity

SHA-256 values from the saved session record:

| Executable | SHA-256 |
| --- | --- |
| NeoCore (`Build/neocore/bin/Release/neocore.exe`) | `0fba51b2a8c7df5872d1c39146511c8a22a17d872bcd54c0be2bc13d6627c2a4` |
| Lua 5.5 | `0d20768e1b2b438fba72475b9da71a8d7d0738254ce7cdadc24f6a42fdb70775` |
| C++ (`bench_cpp.exe`) | `c39646891de34636c9e1a6398bb7b01f5c5ba8a15f70b87fdf6a4b7d4dcd0869` |

| Source | SHA-256 |
| --- | --- |
| Samples/bench/bench.nc | `aa3b9d60e89607cda69f8a23dab21d233f61dede48abc0b683d1e0b6b28782ca` |
| Samples/bench/bench.lua | `7fdc9d39636cacad41cbd29db4f81db82126d0513ca9f5e1d872d7308983486b` |
| Samples/bench/bench.cpp | `fbdb520916b45861146f570407036267c4fc5f02e3c812177ce44b633acb841b` |

These hashes identify the measured snapshots. Files at the linked source paths can
change during subsequent work.

<details>
<summary>All recorded process-level times (milliseconds)</summary>

Each cell is that process's best-of-five result. The warmup column is retained for
audit and excluded from the published medians.

### NeoCore

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 340.999 | 351.000 | 335.000 | 339.000 | 338.000 | 343.000 | 352.000 | 348.000 |
| float_math | 197.000 | 208.000 | 194.999 | 198.999 | 194.999 | 203.999 | 204.000 | 199.999 |
| func_call | 141.000 | 149.000 | 137.999 | 143.000 | 140.000 | 141.999 | 145.000 | 145.000 |
| fib_recursive | 79.000 | 85.999 | 80.999 | 80.999 | 78.999 | 80.000 | 79.999 | 84.000 |
| array_rw | 60.999 | 62.999 | 61.999 | 60.999 | 60.999 | 59.999 | 60.999 | 63.000 |
| map_str | 235.000 | 235.999 | 234.000 | 237.999 | 237.999 | 238.000 | 235.999 | 248.000 |
| string_ops | 57.000 | 59.000 | 56.000 | 57.000 | 56.000 | 56.999 | 57.000 | 60.999 |
| particles | 44.999 | 45.999 | 43.999 | 44.000 | 68.999 | 43.000 | 44.999 | 73.999 |

### Lua

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 187.999 | 192.999 | 195.999 | 190.999 | 192.000 | 187.999 | 189.000 | 199.000 |
| float_math | 298.000 | 309.999 | 298.999 | 319.000 | 315.000 | 298.000 | 304.999 | 316.999 |
| func_call | 153.000 | 156.000 | 153.999 | 157.000 | 154.999 | 153.999 | 153.999 | 155.999 |
| fib_recursive | 86.999 | 88.000 | 86.999 | 92.000 | 87.000 | 87.000 | 84.999 | 85.999 |
| array_rw | 45.999 | 45.999 | 44.999 | 45.999 | 47.000 | 44.999 | 46.000 | 46.000 |
| map_str | 38.999 | 40.000 | 37.999 | 37.999 | 38.999 | 39.000 | 38.999 | 38.000 |
| string_ops | 144.999 | 139.999 | 142.999 | 141.000 | 144.999 | 152.000 | 141.000 | 144.999 |
| particles | 48.000 | 50.000 | 47.000 | 48.000 | 51.999 | 49.999 | 49.999 | 49.999 |

### C++

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 12.623 | 12.870 | 12.315 | 12.160 | 13.139 | 12.452 | 12.870 | 12.683 |
| float_math | 60.123 | 59.951 | 56.480 | 57.247 | 60.585 | 57.091 | 58.468 | 58.946 |
| func_call | 4.483 | 4.476 | 4.275 | 4.297 | 4.471 | 4.276 | 4.367 | 4.466 |
| fib_recursive | 7.209 | 7.164 | 6.857 | 6.857 | 7.160 | 6.846 | 7.005 | 7.153 |
| array_rw | 2.613 | 2.574 | 2.433 | 2.435 | 2.612 | 2.452 | 2.498 | 2.530 |
| map_str | 61.127 | 62.711 | 60.108 | 62.109 | 62.647 | 61.221 | 61.638 | 62.759 |
| string_ops | 13.604 | 13.424 | 13.039 | 13.245 | 14.134 | 13.281 | 13.992 | 13.448 |
| particles | 3.414 | 3.426 | 3.294 | 3.304 | 3.596 | 3.388 | 3.502 | 3.508 |

</details>
