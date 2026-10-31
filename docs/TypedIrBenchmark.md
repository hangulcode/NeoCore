# Typed IR VM benchmark snapshot — 2026-10-31

This records the eight-case benchmark for the NeoCore build at commit
`fd0e22913986876d97dd801ab0d037bb4ccd8e86` (`fd0e229`), the first build that
emits numeric expressions, scalar writes, calls and returns from typed IR with
the generic arithmetic instructions compiled out. **The NeoCore sum of case
medians is 75.1% higher than the archived NeoScript baseline and 25.4% higher
than Lua in this session.** These are measured results for this build, not a
conclusion about static typing in general, and they do not include a paired
generic-op control.

## Method

- Windows x64 session from 2026-10-31 10:19:46 to 10:21:58 UTC
  (19:19:46 to 19:21:58 in Korea). No other builds or media playback ran.
- One serial warmup round followed by seven measured rounds, rotating the
  executable order every round. No executables ran concurrently.
- Each benchmark script reports its best of five internal repetitions. The
  table takes the median of the seven process-level results, excluding the warmup.
- All eight checksums matched across **all 32 process executions**, including warmup.
- NeoCore ran without `--debug`. Script timers exclude process startup and compilation.
- The session record is `Build/nc-switch-benchmark/measurements.json`; per-process
  output files and the measurement script are in the same ignored build directory.

## Results

Times are milliseconds; lower is faster. Three decimal places retain the recorded
microsecond units and do not imply that every timer has microsecond accuracy.

| Case | NeoCore | NeoScript | Lua | C++ | Matching checksum |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 340.999 | 177.999 | 192.000 | 12.602 | 12497500 |
| float_math | 257.000 | 169.000 | 304.999 | 57.906 | 21666 |
| func_call | 153.999 | 125.999 | 156.000 | 4.295 | 12502500 |
| fib_recursive | 86.999 | 85.999 | 88.999 | 6.859 | 2178309 |
| array_rw | 60.999 | 41.000 | 46.999 | 2.489 | 25493856 |
| map_str | 267.999 | 36.000 | 40.999 | 62.385 | 90000 |
| string_ops | 59.999 | 49.999 | 142.000 | 13.502 | 438890 |
| particles | 51.999 | 44.999 | 49.000 | 3.409 | 10461222 |
| Sum of case medians | 1279.993 | 730.995 | 1020.996 | 163.447 | — |

The sum summarizes these eight workloads; it is not a process duration or a
general language-performance score. The seven measured rounds are tight: every
NeoCore case varies by less than 4% between its minimum and maximum, so the
differences below are properties of this build rather than session noise.

Relative to the NeoScript baseline, the NeoCore medians are:

| Case | NeoCore / NeoScript |
| --- | ---: |
| loop_sum | 1.92× |
| float_math | 1.52× |
| func_call | 1.22× |
| fib_recursive | 1.01× |
| array_rw | 1.49× |
| map_str | 7.44× |
| string_ops | 1.20× |
| particles | 1.16× |

## What this comparison includes

- The benchmark sources are the current `Samples/bench/bench.nc`, `bench.lua`
  and `bench.cpp`. The legacy `bench.ns` is no longer in the tree; the measured copy
  was restored from the commit before its removal and runs on the archived
  NeoScript console build from `ef70881`. Hashes are listed below.
- `bench.nc` keeps the original algorithms and iteration counts with explicit
  types, `map.get(key, 0)` and `tostring`, as described in
  [the first snapshot](FirstNcBenchmark.md).
- The `switch` statement added in `fd0e229` is not used by the benchmark; this
  session measures the typed IR build as a whole, not that statement.
- NeoCore uses the CMake Release configuration (`/O2 /Ob2 /DNDEBUG`, MSVC,
  `NEOSCRIPT_LEGACY_ARITHMETIC=0`). NeoScript is the archived Visual Studio
  console build. Lua is the prebuilt 5.5 Win64 binary. C++ is `bench.cpp` built
  with `cl /O2 /std:c++17`. Build configuration and code layout differ between
  these executables and can affect the comparison.

## What this comparison cannot isolate

- There is **no paired generic-op control** in this session. The historical
  [typed numeric VM comparison](TypedVmBenchmark.md) compiled both paths into one
  host executable; this build excludes the generic arithmetic instructions, so a
  controlled typed/generic comparison requires rebuilding with the legacy
  arithmetic enabled.
- The [first snapshot](FirstNcBenchmark.md) measured a different executable in a
  different session, so its NeoCore column is not a baseline for this table. As an
  observation only: that session recorded 176.000 ms for `loop_sum` and
  173.000 ms for `float_math` against the same NeoScript binary, which measured
  175–178 ms and 166–171 ms in both sessions. The NeoScript, Lua and C++ columns
  agree closely between the two sessions while the NeoCore column does not.
- `map_str` remains dominated by the `map.get` fallback path rather than by
  numeric instruction selection.

Per-case instruction counts and dispatch costs were not collected. Identifying
which typed IR paths cost the most requires a paired measurement with matching
checksums in one session, ideally with a bytecode dump of the inner loops, before
any change is claimed as an improvement or a regression cause.

## Artifact identity

SHA-256 values from the saved session record:

| Executable | SHA-256 |
| --- | --- |
| NeoCore (`Build/neocore/bin/Release/neocore.exe`) | `af36e150311f4d4055cb015f437e8af08bf92b2bb73e9be47d6cd1cd96bcc907` |
| NeoScript (`ef70881` console) | `3d3eaab5a380343191363a072e0832c73b818319273adb95daa18a661682d1ad` |
| Lua 5.5 | `0d20768e1b2b438fba72475b9da71a8d7d0738254ce7cdadc24f6a42fdb70775` |
| C++ (`bench_cpp.exe`) | `c39646891de34636c9e1a6398bb7b01f5c5ba8a15f70b87fdf6a4b7d4dcd0869` |

| Source | SHA-256 |
| --- | --- |
| Samples/bench/bench.nc | `aa3b9d60e89607cda69f8a23dab21d233f61dede48abc0b683d1e0b6b28782ca` |
| bench.ns (restored legacy copy) | `05736bd2b89022671ad75c65e564bfbafaf179422aaf786f6bc9ae961ac71c5a` |
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
| loop_sum | 343.000 | 340.999 | 341.999 | 340.000 | 338.000 | 338.000 | 341.999 | 343.000 |
| float_math | 255.999 | 258.000 | 256.000 | 257.000 | 256.000 | 250.999 | 259.999 | 262.000 |
| func_call | 157.000 | 155.000 | 157.000 | 156.000 | 153.999 | 153.999 | 153.999 | 153.999 |
| fib_recursive | 88.999 | 86.999 | 87.999 | 88.000 | 86.000 | 86.999 | 86.999 | 88.999 |
| array_rw | 61.000 | 61.999 | 60.999 | 60.999 | 59.999 | 60.000 | 61.999 | 62.999 |
| map_str | 262.000 | 263.999 | 270.000 | 267.999 | 262.999 | 262.999 | 267.999 | 271.000 |
| string_ops | 59.999 | 59.999 | 60.999 | 60.999 | 58.999 | 59.999 | 59.999 | 62.000 |
| particles | 51.999 | 51.999 | 52.999 | 51.999 | 50.999 | 51.999 | 52.999 | 51.999 |

### NeoScript

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 177.000 | 176.000 | 180.000 | 179.000 | 178.999 | 176.000 | 177.999 | 177.000 |
| float_math | 170.999 | 171.000 | 169.999 | 169.999 | 165.999 | 166.000 | 169.000 | 168.999 |
| func_call | 125.999 | 125.999 | 127.999 | 125.999 | 125.999 | 125.000 | 126.000 | 127.000 |
| fib_recursive | 85.999 | 85.999 | 86.999 | 85.999 | 86.999 | 84.000 | 85.999 | 85.999 |
| array_rw | 41.999 | 41.999 | 40.999 | 41.999 | 41.000 | 40.999 | 41.999 | 40.999 |
| map_str | 36.999 | 36.000 | 36.000 | 36.999 | 36.000 | 35.000 | 36.999 | 36.999 |
| string_ops | 49.000 | 49.999 | 49.999 | 49.999 | 48.999 | 48.000 | 48.999 | 49.999 |
| particles | 44.999 | 45.000 | 48.999 | 44.999 | 44.000 | 44.000 | 44.000 | 46.000 |

### Lua

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 190.999 | 193.000 | 192.000 | 192.000 | 191.999 | 190.000 | 190.999 | 193.000 |
| float_math | 321.000 | 302.999 | 319.999 | 329.000 | 302.000 | 304.999 | 308.000 | 303.999 |
| func_call | 157.000 | 159.000 | 157.999 | 157.000 | 153.999 | 155.000 | 155.999 | 156.000 |
| fib_recursive | 88.999 | 89.999 | 88.999 | 88.999 | 86.999 | 86.999 | 88.999 | 88.000 |
| array_rw | 46.999 | 46.000 | 46.999 | 46.999 | 46.000 | 46.999 | 47.000 | 47.000 |
| map_str | 41.000 | 39.999 | 41.000 | 43.999 | 37.999 | 40.999 | 40.999 | 36.000 |
| string_ops | 143.000 | 143.999 | 144.000 | 142.000 | 141.999 | 141.999 | 145.000 | 142.000 |
| particles | 48.999 | 49.000 | 49.999 | 48.999 | 48.999 | 49.999 | 49.000 | 49.999 |

### C++

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 12.652 | 12.644 | 12.683 | 12.401 | 12.597 | 12.602 | 12.478 | 12.640 |
| float_math | 58.255 | 57.843 | 57.895 | 58.442 | 58.040 | 57.900 | 58.171 | 57.906 |
| func_call | 4.284 | 4.279 | 4.373 | 4.214 | 4.376 | 4.295 | 4.535 | 4.287 |
| fib_recursive | 6.894 | 6.850 | 7.002 | 6.723 | 6.993 | 6.914 | 6.854 | 6.859 |
| array_rw | 2.459 | 2.557 | 2.515 | 2.410 | 2.489 | 2.567 | 2.470 | 2.432 |
| map_str | 61.732 | 60.866 | 62.192 | 64.453 | 63.373 | 62.385 | 60.688 | 64.244 |
| string_ops | 13.714 | 13.589 | 13.434 | 13.502 | 13.448 | 13.553 | 13.584 | 13.348 |
| particles | 3.425 | 3.443 | 3.351 | 3.431 | 3.409 | 3.278 | 3.410 | 3.331 |

</details>
