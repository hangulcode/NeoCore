# First NeoCore .nc benchmark snapshot

This records the first complete eight-case benchmark of NeoCore's checked-source
frontend. **It measures source lowering into the inherited tagged VM, not a VM with
typed instructions.** Preserve this snapshot as the baseline for the typed-VM migration.

The session ran on Windows from 2026-10-30 15:03:00 to 15:04:56 UTC
(2026-10-31 00:03:00 to 00:04:56 in Korea). The NeoScript comparison binary came
from commit `ef70881a94bc1e0963ff7cd3b9ec058b9b12fa6e` (`ef70881`).
NeoCore was the then-current working-tree build; the hashes below identify the
measured artifacts without claiming that uncommitted code had a release revision.

## Method

- One serial warmup round ran all four executables.
- Seven measured rounds followed, rotating executable order each round. No
  executables were benchmarked concurrently.
- Each benchmark script reported its best of five internal repetitions. The table
  takes the median of those seven process-level results, excluding the warmup.
- All eight checksums matched across **all 32 process executions**, including warmup.
- NeoCore ran without `--debug`. Script timers exclude process startup and compilation.
- The local session record is `Build/nc-first-benchmark/measurements.json`; per-process
  output files are in the same ignored build directory. This document retains its
  result table, checksums, individual observations, and artifact hashes.

## Results

Times are milliseconds; lower is faster. Three decimal places retain the recorded
microsecond units and do not imply that every timer has microsecond accuracy.

| Case | NeoCore | NeoScript | Lua | C++ | Matching checksum |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 176.000 | 174.999 | 190.000 | 12.604 | 12497500 |
| float_math | 173.000 | 167.999 | 307.000 | 57.585 | 21666 |
| func_call | 117.000 | 125.000 | 154.999 | 4.226 | 12502500 |
| fib_recursive | 78.999 | 84.999 | 88.000 | 6.732 | 2178309 |
| array_rw | 47.000 | 40.999 | 46.000 | 2.459 | 25493856 |
| map_str | 211.999 | 36.000 | 38.999 | 60.778 | 90000 |
| string_ops | 57.999 | 48.000 | 141.999 | 13.319 | 438890 |
| particles | 56.999 | 44.000 | 48.000 | 3.355 | 10461222 |
| Sum of case medians | 918.996 | 721.996 | 1014.997 | 161.058 | — |

The sum is a summary of these eight workloads, not a measured end-to-end process
duration or a general language-performance score. The largest difference from the
NeoScript baseline is `map_str`: 211.999 ms versus 36.000 ms. Function calls and
recursive Fibonacci are faster in this session, but this comparison alone does
not isolate the cause of those differences.

## What this comparison includes

The [.nc benchmark](../Samples/bench/bench.nc) retains the original algorithms,
iteration counts, and expected checksums. Static typing required these adaptations:

- Map reads use `m.get(key, 0)` so missing-key handling is explicit. Every measured
  key is present, so the fallback does not change the checksum. This first frontend
  lowers `get` through a generated helper, adding a call around the lookup.
- Typed `list.resize` initializes newly added slots to the element's default value.
  Its helper work is included in the measured execution.
- Numeric string concatenation uses explicit `tostring` calls.
- Type checks, conversions, and static declarations still lead to the existing
  tagged values and generic VM instructions. This is not the final static execution
  representation, and these numbers do not predict its performance.

NeoCore was built with **CMake Release**; NeoScript used the archived original
**Visual Studio console Release** build. The configurations and binary layouts
were not normalized into a single compiler/linker setup. Optimization flags,
link-time optimization, runtime linkage, and code layout can affect the comparison.
Do not attribute every difference to the frontend or to static typing without a
controlled build variant.

The [small-input correctness program](../Samples/bench/check.nc) checks these same
algorithms without timers. Future typed-VM comparisons must retain those checks and
the full benchmark checksums. Re-run baseline and changed binaries in the same
serial session; ask the user before starting a new timed session.

## Artifact identity

SHA-256 values from the saved session record:

| Executable | SHA-256 |
| --- | --- |
| NeoCore | `aa347837f55239caad2918db66f96faac64fca24c9453e7a38ec28bcb8648486` |
| NeoScript | `3d3eaab5a380343191363a072e0832c73b818319273adb95daa18a661682d1ad` |
| Lua | `0d20768e1b2b438fba72475b9da71a8d7d0738254ce7cdadc24f6a42fdb70775` |
| C++ | `dd6d54066f12e97bacd4cf44cf0ec7481b01a005da6dcd7e29bc51104ffd5317` |

| Source under `Samples/bench` | SHA-256 |
| --- | --- |
| bench.nc | `3fd1b29392f63641cc6cceb265be25a3cac145d30618681bd8f88111176181c4` |
| bench.ns | `76d29c92dd8e5e5e94366e802b301e51087325be8248742f7a95bbe2fc674b1d` |
| bench.lua | `02ec5fd6d5a1016a31e0c5ae345232e2b6d21e904a7509f12f3ca086005e87ed` |
| bench.cpp | `d5575fe210bb9d06267810aca1ce1e331a7a427b9b703893927e5d7a44bac2ae` |

These hashes identify the measured snapshots. Files at the linked source paths can
change during subsequent work.

<details>
<summary>All recorded process-level times (milliseconds)</summary>

Each cell is that process's best-of-five result. The warmup column is retained for
audit and excluded from the published medians.

### NeoCore

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 175.000 | 177.000 | 176.000 | 178.000 | 177.000 | 175.000 | 176.000 | 175.000 |
| float_math | 173.999 | 170.999 | 173.000 | 173.999 | 175.000 | 173.999 | 173.000 | 172.999 |
| func_call | 114.999 | 115.999 | 117.000 | 117.999 | 116.000 | 117.999 | 115.999 | 117.000 |
| fib_recursive | 79.999 | 78.999 | 77.999 | 79.000 | 76.999 | 78.999 | 77.999 | 80.000 |
| array_rw | 47.000 | 46.999 | 46.999 | 45.999 | 47.000 | 47.000 | 47.000 | 47.000 |
| map_str | 207.999 | 209.999 | 211.999 | 215.000 | 213.000 | 209.999 | 211.000 | 212.000 |
| string_ops | 56.999 | 57.999 | 57.999 | 58.000 | 56.999 | 56.999 | 57.999 | 59.000 |
| particles | 56.999 | 54.999 | 54.999 | 57.999 | 56.999 | 56.999 | 56.999 | 56.000 |

### NeoScript

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 180.000 | 174.000 | 174.999 | 174.000 | 173.000 | 176.000 | 175.000 | 175.000 |
| float_math | 167.000 | 164.000 | 168.999 | 167.000 | 167.999 | 167.000 | 167.999 | 170.000 |
| func_call | 123.999 | 125.000 | 125.000 | 125.000 | 123.999 | 126.000 | 125.999 | 125.999 |
| fib_recursive | 84.000 | 83.999 | 83.999 | 84.999 | 84.999 | 84.999 | 84.999 | 84.999 |
| array_rw | 41.000 | 41.999 | 40.999 | 40.999 | 40.999 | 41.999 | 40.999 | 40.999 |
| map_str | 36.000 | 36.999 | 36.000 | 36.000 | 36.000 | 36.999 | 36.000 | 35.999 |
| string_ops | 48.000 | 48.000 | 48.000 | 48.000 | 48.999 | 48.999 | 48.000 | 48.000 |
| particles | 44.000 | 44.000 | 43.000 | 44.000 | 44.000 | 44.000 | 44.000 | 46.000 |

### Lua

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 189.000 | 188.000 | 187.999 | 193.000 | 190.000 | 190.000 | 189.000 | 190.000 |
| float_math | 300.000 | 312.999 | 301.999 | 303.999 | 304.000 | 307.000 | 320.000 | 321.000 |
| func_call | 152.999 | 152.000 | 153.999 | 154.999 | 157.999 | 156.000 | 154.999 | 153.999 |
| fib_recursive | 87.000 | 88.000 | 86.999 | 87.999 | 88.000 | 90.999 | 88.000 | 86.999 |
| array_rw | 45.999 | 44.999 | 45.999 | 46.999 | 46.000 | 48.000 | 45.999 | 46.000 |
| map_str | 38.999 | 35.000 | 38.000 | 41.999 | 38.999 | 40.000 | 38.999 | 40.000 |
| string_ops | 139.999 | 141.999 | 141.000 | 141.999 | 141.000 | 140.000 | 141.999 | 142.999 |
| particles | 49.000 | 48.000 | 48.999 | 49.000 | 48.000 | 48.000 | 48.999 | 48.000 |

### C++

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 12.577 | 12.868 | 12.369 | 12.856 | 12.604 | 12.310 | 12.472 | 12.635 |
| float_math | 57.116 | 57.054 | 57.483 | 57.759 | 57.837 | 58.533 | 57.585 | 57.036 |
| func_call | 4.154 | 4.134 | 4.381 | 4.226 | 4.380 | 4.189 | 4.244 | 4.148 |
| fib_recursive | 6.585 | 6.715 | 7.035 | 6.732 | 6.896 | 6.726 | 6.810 | 6.584 |
| array_rw | 2.373 | 3.624 | 2.617 | 2.373 | 2.523 | 2.459 | 2.431 | 2.353 |
| map_str | 59.469 | 61.438 | 60.778 | 60.424 | 59.632 | 62.607 | 62.341 | 59.776 |
| string_ops | 13.548 | 13.290 | 13.181 | 13.378 | 13.471 | 13.236 | 13.319 | 13.585 |
| particles | 3.425 | 3.361 | 3.292 | 3.383 | 3.298 | 3.355 | 3.296 | 3.456 |

</details>
