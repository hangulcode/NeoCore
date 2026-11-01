# Three-way VM benchmark snapshot — 2026-11-01

This records the eight-case benchmark for the NeoCore build at commit
`dca2d64a97e4dd32acf65ab65f0915457867d208` (`dca2d64`) against Lua 5.5 and C++
in one session, plus a paired NeoCore-only session that runs this build against a
rebuild of the [previous snapshot's commit](ThreeWayBenchmark.md) `91d6d58`.
This build follows the nullability-by-type change, struct field defaults with
lenient JSON decoding, unified constant evaluation, null array guards and null
collection key rejection. **The NeoCore sum of case medians is 26.6% above Lua
in this session; NeoCore is faster than Lua in two cases, within 4% in three and
slower in three.** **In the paired session the `dca2d64` build is 7.3% slower
than the `91d6d58` build in the sum of case medians, with `float_math` 21%
slower, while the emitted bytecode for `bench.nc` is identical.** These are
measured results for these builds, not a conclusion about static typing in general.

## Method

- Windows x64 session from 2026-11-01 02:39:35 to 02:41:18 UTC (11:39 to 11:41
  in Korea) for the three-way run, 02:43:28 to 02:45:13 UTC for the paired run and
  02:48:14 to 02:51:29 UTC for the five-build run. No media playback ran. Another
  agent's idle build-server processes were resident during the five-build run but
  no compiler ran.
- One serial warmup round followed by seven measured rounds (five for the
  five-build run), rotating the executable order every round. No executables ran
  concurrently.
- Each benchmark script reports its best of five internal repetitions. The tables
  take the median of the process-level results, excluding the warmup.
- All eight checksums matched across every process execution in all three
  sessions, including warmups: 24, 16 and 30 processes.
- NeoCore ran without `--debug`. Script timers exclude process startup and compilation.
- The working tree at measurement time carried another agent's uncommitted
  edits, so every NeoCore executable was built from a detached worktree of its
  commit with the CMake Release configuration and only the `neocore` target. The
  committed sources are therefore what the hashes below identify.
- The session records are `Build/nc-3way-benchmark-2/measurements.json`,
  `Build/nc-3way-benchmark-2/paired/measurements.json` and
  `Build/nc-3way-benchmark-2/bisect/measurements.json`; per-process output files
  and the measurement scripts are in the same ignored build directory.

## Results

Times are milliseconds; lower is faster. Three decimal places retain the recorded
microsecond units and do not imply that every timer has microsecond accuracy.

| Case | NeoCore | Lua | C++ | NeoCore / Lua | Matching checksum |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 343.000 | 193.000 | 12.694 | 1.78× | 12497500 |
| float_math | 266.000 | 309.000 | 58.689 | 0.86× | 21666 |
| func_call | 164.000 | 157.999 | 4.286 | 1.04× | 12502500 |
| fib_recursive | 91.999 | 90.999 | 6.910 | 1.01× | 2178309 |
| array_rw | 59.000 | 47.000 | 2.457 | 1.26× | 25493856 |
| map_str | 273.000 | 40.000 | 62.167 | 6.83× | 90000 |
| string_ops | 62.999 | 147.000 | 13.684 | 0.43× | 438890 |
| particles | 50.000 | 49.999 | 3.427 | 1.00× | 10461222 |
| Sum of case medians | 1309.998 | 1034.997 | 164.314 | 1.27× | — |

The sum summarizes these eight workloads; it is not a process duration or a
general language-performance score. Every NeoCore case varies by less than 4%
between its minimum and maximum across the seven measured rounds, so the medians
are stable within this session. `particles`, which was noisy in the previous
snapshot, stayed within 49.999–51.000 ms here.

The Lua and C++ columns agree closely with the previous snapshot (Lua sums of
1021.994 and 1034.997 ms, C++ sums of 163.980 and 164.314 ms), so the machine
state was comparable. The NeoCore column does not: `float_math`, `func_call`,
`fib_recursive`, `map_str` and `particles` are all slower than their 2026-10-31
values while `loop_sum` and `array_rw` are unchanged. Separate sessions are not
proof, so the next section pairs the two builds directly.

## Paired comparison with the previous snapshot's commit

Both executables ran in one session, alternating, from the same `bench.nc`.

| Case | NeoCore `91d6d58` | NeoCore `dca2d64` | `dca2d64` / `91d6d58` |
| --- | ---: | ---: | ---: |
| loop_sum | 348.000 | 344.000 | 0.99× |
| float_math | 223.999 | 271.000 | 1.21× |
| func_call | 145.000 | 165.999 | 1.14× |
| fib_recursive | 83.000 | 91.999 | 1.11× |
| array_rw | 62.000 | 59.999 | 0.97× |
| map_str | 258.000 | 271.000 | 1.05× |
| string_ops | 60.999 | 63.000 | 1.03× |
| particles | 47.000 | 51.000 | 1.09× |
| Sum of case medians | 1227.998 | 1317.997 | 1.07× |

The min–max ranges of the two builds do not overlap for `float_math`
(221.999–226.999 against 269.000–279.999), `func_call` (144.000–148.000 against
162.999–166.999), `fib_recursive`, `map_str`, `string_ops` or `particles`, so the
difference is a property of the executables, not round-to-round noise.

What the paired run can and cannot say:

- `neocore --asm --file bench.nc` produces byte-identical instruction listings
  from both builds; only the timing lines differ. The frontend and lowering
  changes between the commits did not change what the VM executes for this suite.
- The backend source changes between the commits are a null check in `ToSize`, a
  null-receiver error message, null guards at the start of the typed array
  handlers, null map-key checks in the force-inlined `CltInsert` paths, and error
  location bookkeeping (`m_iLastErrorSourceOPIndex`, helper-frame filtering) in
  `NeoVMWorker.cpp`. Only the map-key checks sit on a path that `bench.nc`
  executes in a hot loop (`map_str`), and none of them is on the `float_math`
  or `func_call` paths.
- A five-build session over the intermediate commits does not step monotonically.
  Sums of case medians in milliseconds, in commit order:

  | `91d6d58` | `ad7711d` | `364e959` | `75ffe9e` | `dca2d64` |
  | ---: | ---: | ---: | ---: | ---: |
  | 1224.994 | 1175.996 | 1260.997 | 1187.994 | 1316.997 |

  `float_math` alone reads 221.999, 197.999, 237.000, 220.999 and 270.999 ms
  across the same five builds, although none of those commits touches the float
  arithmetic handlers. `364e959` is slower than both of its neighbours and
  `75ffe9e` is faster than both of its neighbours. That pattern is what code
  layout changes in the dispatch loop look like, and it is the reason the working
  rules say build layout can change timings.

The paired result is therefore a confirmed slowdown of the `dca2d64` executable
relative to a rebuilt `91d6d58` executable on this suite, with the largest effect
in `float_math`. It is not evidence that any one commit's logic is slower. The
rebuilt `91d6d58` executable itself differs from the one recorded on 2026-10-31
(different hash, `float_math` 223.999 against 199.999 ms across the two sessions),
which is the same caveat seen from the other side. Attributing the step from
`75ffe9e` to `dca2d64` requires a layout-controlled experiment, such as the
`dca2d64` tree with the VM-side edits of that commit reverted, measured in a pair
against the unmodified build.

## What this comparison includes

- The benchmark sources are the current `Samples/bench/bench.nc`, `bench.lua`
  and `bench.cpp`; their hashes are unchanged from the
  [previous three-way snapshot](ThreeWayBenchmark.md), so the workloads are identical.
- NeoCore uses the CMake Release configuration (`/O2 /Ob2 /DNDEBUG`, MSVC),
  built from a detached worktree of each commit. Lua is the same prebuilt 5.5
  Win64 binary as in the previous snapshot. The C++ executable is the same file as
  in the previous snapshot. Build configuration and code layout differ between
  these executables and can affect the comparison.

## What this comparison cannot isolate

- No NeoScript column: the archived dynamic console did not run.
- No generic-op control: the generic arithmetic path no longer exists in the
  source, so the historical
  [typed numeric VM comparison](TypedVmBenchmark.md) remains the only such
  measurement.
- No instruction counts or dispatch profiles were collected. The next useful step
  for the `float_math` and `func_call` slowdown is the layout-controlled pair
  described above, together with a per-handler profile of the dispatch loop,
  before any change is claimed as the cause or as a fix.

## Artifact identity

SHA-256 values from the saved session records:

| Executable | SHA-256 |
| --- | --- |
| NeoCore `dca2d64` (three-way and paired runs) | `6409a1943fb5b1497f45643644d08272c2720b54ab705001eb9905cf2b58f3fe` |
| NeoCore `91d6d58` (rebuilt for the paired run) | `27b181f8f61a8ff3ac11e61446a07a03a0026ce660839056420e670740c1ebcb` |
| NeoCore `ad7711d` (five-build run) | `94e075decbb65f6c892a074957e95c660f66a03bf0a6cbadf08c1d14b75b1cb7` |
| NeoCore `364e959` (five-build run) | `8b7e7eead88c46ef437023277bf05b5810a85f623a3dfff755008509fa865f21` |
| NeoCore `75ffe9e` (five-build run) | `58cefc4cbe4a64924f39a97731ae6be34c9121ef77ec08b9e3765520bf69f87c` |
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

### Three-way session: NeoCore `dca2d64`

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 343.000 | 345.000 | 339.000 | 343.000 | 343.000 | 344.000 | 350.000 | 338.999 |
| float_math | 269.000 | 266.000 | 266.000 | 266.000 | 273.000 | 265.999 | 263.999 | 267.999 |
| func_call | 167.000 | 164.000 | 165.999 | 165.000 | 164.000 | 166.999 | 161.000 | 161.999 |
| fib_recursive | 92.999 | 91.999 | 91.000 | 92.000 | 92.999 | 92.999 | 91.999 | 91.999 |
| array_rw | 59.999 | 59.000 | 58.999 | 59.999 | 59.000 | 59.999 | 58.999 | 59.000 |
| map_str | 268.999 | 274.000 | 269.000 | 273.000 | 274.000 | 273.000 | 269.000 | 270.999 |
| string_ops | 61.999 | 62.000 | 62.000 | 62.000 | 62.999 | 62.999 | 63.000 | 62.999 |
| particles | 50.999 | 49.999 | 50.000 | 49.999 | 49.999 | 50.999 | 51.000 | 50.999 |

### Three-way session: Lua

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 194.999 | 195.999 | 194.000 | 193.000 | 191.999 | 194.999 | 193.000 | 193.000 |
| float_math | 311.000 | 322.000 | 308.000 | 309.000 | 322.999 | 310.999 | 308.999 | 308.999 |
| func_call | 160.000 | 161.000 | 157.000 | 157.000 | 157.999 | 159.000 | 157.999 | 157.999 |
| fib_recursive | 90.999 | 91.000 | 89.999 | 90.999 | 91.000 | 91.999 | 88.999 | 88.999 |
| array_rw | 48.000 | 46.999 | 47.000 | 46.999 | 46.999 | 47.000 | 47.999 | 47.999 |
| map_str | 43.000 | 42.000 | 40.000 | 38.999 | 41.999 | 38.999 | 41.999 | 40.000 |
| string_ops | 147.000 | 147.000 | 144.999 | 145.999 | 144.999 | 147.999 | 147.000 | 147.000 |
| particles | 50.000 | 49.999 | 48.999 | 49.999 | 49.999 | 49.999 | 49.999 | 49.999 |

### Three-way session: C++

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 12.613 | 12.694 | 12.477 | 12.781 | 12.905 | 12.843 | 12.597 | 12.619 |
| float_math | 58.939 | 59.028 | 58.689 | 58.351 | 59.705 | 58.114 | 59.479 | 58.168 |
| func_call | 4.282 | 4.286 | 4.204 | 4.289 | 4.370 | 4.458 | 4.197 | 4.283 |
| fib_recursive | 6.871 | 6.869 | 6.837 | 6.982 | 6.999 | 7.153 | 6.910 | 6.831 |
| array_rw | 2.432 | 2.449 | 2.433 | 2.500 | 2.499 | 2.560 | 2.457 | 2.378 |
| map_str | 62.456 | 62.167 | 62.650 | 61.733 | 62.327 | 62.943 | 60.713 | 61.857 |
| string_ops | 13.869 | 13.852 | 13.848 | 13.725 | 13.623 | 13.684 | 13.412 | 13.521 |
| particles | 3.528 | 3.434 | 3.436 | 3.400 | 3.482 | 3.418 | 3.423 | 3.427 |

### Paired session: NeoCore `91d6d58`

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 358.000 | 351.000 | 345.000 | 348.000 | 347.999 | 351.000 | 349.000 | 345.999 |
| float_math | 225.000 | 221.999 | 222.999 | 223.999 | 222.999 | 225.000 | 226.999 | 223.999 |
| func_call | 169.999 | 144.000 | 147.000 | 144.000 | 145.000 | 148.000 | 147.000 | 145.000 |
| fib_recursive | 83.999 | 80.999 | 83.000 | 81.999 | 83.000 | 83.000 | 84.999 | 80.999 |
| array_rw | 70.999 | 61.999 | 62.000 | 61.999 | 62.000 | 64.000 | 62.000 | 62.999 |
| map_str | 259.000 | 256.000 | 258.000 | 259.999 | 256.999 | 258.999 | 258.000 | 255.999 |
| string_ops | 62.000 | 60.999 | 60.999 | 61.999 | 61.999 | 60.999 | 61.999 | 60.999 |
| particles | 49.999 | 48.000 | 46.999 | 49.999 | 46.999 | 47.000 | 48.000 | 46.999 |

### Paired session: NeoCore `dca2d64`

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 349.999 | 353.000 | 341.000 | 343.000 | 344.000 | 340.999 | 344.000 | 344.000 |
| float_math | 274.999 | 279.999 | 269.000 | 271.000 | 271.000 | 272.999 | 270.000 | 269.000 |
| func_call | 164.000 | 166.999 | 162.999 | 165.999 | 163.000 | 166.000 | 163.000 | 165.999 |
| fib_recursive | 94.000 | 94.000 | 91.999 | 91.999 | 91.999 | 92.999 | 91.999 | 92.999 |
| array_rw | 60.999 | 60.999 | 59.999 | 59.999 | 58.999 | 58.999 | 58.999 | 59.999 |
| map_str | 283.000 | 278.999 | 271.000 | 274.999 | 270.999 | 275.999 | 270.999 | 270.999 |
| string_ops | 65.000 | 64.000 | 63.000 | 62.999 | 62.999 | 64.999 | 64.000 | 62.999 |
| particles | 51.999 | 51.000 | 49.999 | 50.000 | 49.999 | 52.000 | 51.000 | 51.000 |

### Five-build session: medians and min–max

| Case | `91d6d58` | `ad7711d` | `364e959` | `75ffe9e` | `dca2d64` |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 347.000 | 347.999 | 351.000 | 334.999 | 343.000 |
| float_math | 221.999 | 197.999 | 237.000 | 220.999 | 270.999 |
| func_call | 145.000 | 145.000 | 155.000 | 149.000 | 165.000 |
| fib_recursive | 82.999 | 82.999 | 84.999 | 92.999 | 92.000 |
| array_rw | 62.999 | 62.999 | 62.999 | 59.999 | 59.000 |
| map_str | 256.999 | 234.000 | 251.999 | 228.999 | 273.000 |
| string_ops | 60.999 | 59.000 | 64.000 | 56.000 | 62.999 |
| particles | 46.999 | 46.000 | 54.000 | 44.999 | 50.999 |
| Sum of case medians | 1224.994 | 1175.996 | 1260.997 | 1187.994 | 1316.997 |

| Case | `91d6d58` | `ad7711d` | `364e959` | `75ffe9e` | `dca2d64` |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 345.000–349.999 | 345.000–355.000 | 348.000–361.999 | 334.000–346.000 | 339.999–364.000 |
| float_math | 221.000–229.999 | 197.000–203.999 | 234.999–250.000 | 218.999–228.000 | 270.000–278.999 |
| func_call | 144.000–149.000 | 144.000–157.999 | 153.000–157.999 | 146.999–154.999 | 162.999–167.000 |
| fib_recursive | 80.999–112.000 | 82.999–108.999 | 84.000–91.000 | 92.000–93.999 | 91.999–94.999 |
| array_rw | 61.999–80.999 | 62.999–63.999 | 62.999–64.999 | 59.000–59.999 | 59.000–60.000 |
| map_str | 254.999–355.000 | 232.000–243.999 | 250.000–259.000 | 226.999–238.999 | 270.999–277.999 |
| string_ops | 59.999–67.000 | 58.999–60.999 | 62.999–64.999 | 56.000–57.999 | 61.999–64.000 |
| particles | 46.999–51.999 | 44.999–47.999 | 53.999–57.000 | 43.999–46.000 | 49.000–51.999 |

The `91d6d58` and `ad7711d` columns of the five-build run include one outlier
round each (`fib_recursive` 112.000 and 108.999 ms, `map_str` 355.000 ms), so
their medians are robust but their ranges are not. The five-build run used five
measured rounds rather than seven.

</details>
