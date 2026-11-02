# Three-way VM benchmark snapshot — 2026-11-01 (typed map lookups)

This records the eight-case benchmark for the NeoCore build at commit
`cb522bb5c7e71a967e3ad5d99c3fa42b3316220c` (`cb522bb`) against Lua 5.5 and C++
in one session, plus a paired NeoCore-only session that runs this build against
the `dca2d64` build measured in the [second snapshot](ThreeWayBenchmark2.md).
`cb522bb` replaces the generated script helpers behind `map[key]`, `map.get`
and `map.contains` with dedicated VM operations (`MAP_I64_READ`,
`MAP_STRING_READ`, `MAP_I64_CONTAINS`, `MAP_STRING_CONTAINS`) and removes
`map.get`. **The NeoCore sum of case medians is 1.9% above Lua in this session;
NeoCore is faster than Lua in five of the eight cases and slower in `loop_sum`,
`array_rw` and `map_str`.** **In the paired session `map_str` takes 0.31× the
time of the `dca2d64` build (259.000 to 78.999 ms) and the sum of case medians
0.82×.** These are measured results for these builds, not a conclusion about
static typing in general.

## Method

- Windows x64. The three-way session reported below ran from 2026-11-01 04:29:53
  to 04:31:26 UTC (13:29 to 13:31 in Korea). A first three-way session ran from
  04:25:33 to 04:27:10 UTC while another agent's compiler was active; its medians
  agree with the reported session to within a few percent but its final round
  carries outliers, so it is kept in the appendix for audit and not used for the
  headline numbers. The paired session ran from 04:27:12 to 04:28:52 UTC.
- One serial warmup round followed by seven measured rounds, rotating the
  executable order every round. No executables ran concurrently.
- Each benchmark script reports its best of five internal repetitions. The tables
  take the median of the seven process-level results, excluding the warmup.
- All eight checksums matched across every process execution in all three
  sessions, including warmups: 24, 24 and 16 processes.
- NeoCore ran without `--debug`. Script timers exclude process startup and compilation.
- The NeoCore executable was built from a detached worktree of `cb522bb` with the
  CMake Release configuration and only the `neocore` target, so the committed
  sources are what the hashes below identify. The `dca2d64` executable is the one
  recorded in the second snapshot.
- The session records are `Build/nc-3way-benchmark-3/rerun/measurements.json`
  (reported three-way session), `Build/nc-3way-benchmark-3/measurements.json`
  (first three-way session) and `Build/nc-3way-benchmark-3/paired/measurements.json`;
  per-process output files and the measurement scripts are in the same ignored
  build directory.

## Results

Times are milliseconds; lower is faster. Three decimal places retain the recorded
microsecond units and do not imply that every timer has microsecond accuracy.

| Case | NeoCore | Lua | C++ | NeoCore / Lua | Matching checksum |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 336.000 | 190.999 | 12.745 | 1.76× | 12497500 |
| float_math | 230.999 | 307.999 | 58.669 | 0.75× | 21666 |
| func_call | 147.999 | 156.000 | 4.392 | 0.95× | 12502500 |
| fib_recursive | 86.999 | 88.999 | 6.900 | 0.98× | 2178309 |
| array_rw | 60.999 | 47.000 | 2.471 | 1.30× | 25493856 |
| map_str | 76.000 | 41.000 | 62.363 | 1.85× | 90000 |
| string_ops | 64.000 | 147.999 | 13.757 | 0.43× | 438890 |
| particles | 46.999 | 49.999 | 3.511 | 0.94× | 10461222 |
| Sum of case medians | 1049.995 | 1029.995 | 164.808 | 1.02× | — |

The sum summarizes these eight workloads; it is not a process duration or a
general language-performance score. Every NeoCore case varies by less than 7%
between its minimum and maximum across the seven measured rounds, and `map_str`
by less than 4% (75.000–77.999 ms).

`map_str` moved from the largest gap in both earlier snapshots (6.10× and 6.83×
Lua) to 1.85× Lua. The remaining gap is the hash lookup plus the typed add and
move around it against Lua's single table-get instruction; the generated helper
call that used to dominate the case is gone. `loop_sum` and `array_rw` are
unchanged from the earlier snapshots and are now the two cases furthest from Lua.

## Paired comparison with the previous snapshot's build

Both executables ran in one session, alternating, from the same current
`bench.nc`. The `dca2d64` build compiles the new `m["key"]` form through its
`$neocore_mapread` helper, so this pair measures the lowering change on
identical source.

| Case | NeoCore `dca2d64` | NeoCore `cb522bb` | `cb522bb` / `dca2d64` |
| --- | ---: | ---: | ---: |
| loop_sum | 353.999 | 348.000 | 0.98× |
| float_math | 272.000 | 238.999 | 0.88× |
| func_call | 165.999 | 153.999 | 0.93× |
| fib_recursive | 94.000 | 89.000 | 0.95× |
| array_rw | 58.999 | 62.999 | 1.07× |
| map_str | 259.000 | 78.999 | 0.31× |
| string_ops | 64.999 | 64.000 | 0.98× |
| particles | 51.999 | 49.000 | 0.94× |
| Sum of case medians | 1320.995 | 1084.996 | 0.82× |

The `map_str` ranges do not overlap (251.000–269.000 against 76.999–81.999 ms),
so the change is a property of the executables, not round-to-round noise. The
instruction listing confirms the mechanism: the inner loop of `MapStr` in
`cb522bb` is four `MAP_STRING_READ` / `I64_ADD` / `MOV` triples, while in
`dca2d64` each lookup was three argument moves, a `CALL` into the helper and the
helper's `READ`, `CLR`, `JNE` and `RET`.

The other cases also moved, in both directions, by 2–12% although their lowering
and handlers did not change between the two commits. The same pattern appeared in
the [second snapshot's](ThreeWayBenchmark2.md) five-build session; it is build
layout, and it is why only the `map_str` difference is attributed to this commit.
The `float_math` value of `cb522bb` (238.999 ms) is close to the `91d6d58` and
`75ffe9e` builds of that session (221.999–223.999 and 220.999 ms) rather than to
`dca2d64` (270.999–272.000 ms), which is consistent with layout rather than with
a logic change in the arithmetic handlers.

A micro-benchmark run outside the sessions, ten million iterations of one
operation each, shows the per-lookup cost directly: with the `dca2d64` build a
`map<string,int>["key"]` read took 268.999 ms against 56.999 ms for the bare loop
and 68.999 ms for a `list[0]` read; with the `cb522bb` build the same map read took
84.000 ms against 60.000 and 70.000 ms. The `map<int,int>` read moved from 249.000
to 82.000 ms and `map.contains` from 313.000 to 100.000 ms. These single-process
numbers are not part of the recorded sessions and are quoted only for scale.

## Follow-up run on commit `7dcc5a1`

Commit `7dcc5a1` (set membership and mutation lowered to typed VM operations; no
benchmark source changed) was measured the same way after the sessions above:
a three-way session from 04:55:23 to 04:57:00 UTC and a paired session against
the `cb522bb` executable from 04:57:01 to 04:58:32 UTC, both with seven measured
rounds and all checksums matching (24 and 16 processes). Records:
`Build/nc-3way-benchmark-4/measurements.json` and
`Build/nc-3way-benchmark-4/paired/measurements.json`. Executable SHA-256:
`1b745e1dd4c27592d537baf99429023da0bbc53252482084105a9360a5bcc14c`.

| Case | NeoCore `7dcc5a1` | Lua | C++ | NeoCore / Lua | Paired `7dcc5a1` / `cb522bb` |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 352.000 | 198.000 | 12.960 | 1.78× | 1.01× |
| float_math | 261.000 | 320.999 | 59.901 | 0.81× | 1.10× |
| func_call | 153.999 | 161.000 | 4.384 | 0.96× | 1.01× |
| fib_recursive | 90.999 | 92.000 | 7.021 | 0.99× | 0.97× |
| array_rw | 64.999 | 48.999 | 2.570 | 1.33× | 1.03× |
| map_str | 76.000 | 43.000 | 62.334 | 1.77× | 0.96× |
| string_ops | 67.000 | 152.000 | 13.916 | 0.44× | 1.03× |
| particles | 48.999 | 51.000 | 3.513 | 0.96× | 0.98× |
| Sum of case medians | 1114.996 | 1066.998 | 166.599 | 1.04× | 1.02× |

The Lua and C++ columns are 3.6% and 1.1% above the reported `cb522bb` session,
so the machine was somewhat slower during this session and the NeoCore column
should be read against its own Lua column: 4.5% above Lua, with `map_str` at
1.77×. In the paired session only `float_math` separates (239.999–243.999 against
262.000–266.999 ms); the commit does not touch arithmetic handlers, so this is
the same layout movement seen between earlier builds, not a change attributable
to the set operations. `map_str` is unchanged within noise (0.96×).

## Follow-up run: direct-destination assignment lowering (working tree on `2b096ca`)

After the map and set changes, the backend was changed so that an assignment or
increment that forms a whole statement stores into a plain variable directly
instead of computing into a temporary and copying it (`IRNode::discardResult`;
see [StaticTyping.md](StaticTyping.md#implementation-boundary)). `sum += i + bias;`
in `LoopSum` went from four instructions per iteration (`I64_ADD`, `I64_ADD`,
`MOV`, `JFOR`) to three (`I64_ADD`, `I64_ADD`, `JFOR`), and `FuncCall`, `ArrayRW`,
`FloatMath` and the `Particles` checksum loop each lost one `MOV` per iteration.
The benchmark sources are unchanged. The measured NeoCore executable was built from
the uncommitted working tree on top of commit `2b096ca`; its SHA-256 is
`dacb84e0f0a6a140b3a158c8db8035c55b2105e71031e5c0f053f2915d96db2a`. The baseline
is a clean detached-worktree build of `2b096ca`, SHA-256
`cefd0a363aa82900bf6bb421f1d9ab6dd5189bdbb822f0800e1d70cccbdceee2`. Records:
`Build/nc-3way-benchmark-5/paired2/measurements.json` (paired, 06:43:20 to
06:44:41 UTC) and `Build/nc-3way-benchmark-5/measurements.json` (three-way,
06:44:43 to 06:46:09 UTC), seven measured rounds each, all checksums matching
(16 and 24 processes).

| Case | NeoCore `2b096ca` | NeoCore working tree | Ratio | NeoCore working tree / Lua |
| --- | ---: | ---: | ---: | ---: |
| loop_sum | 344.000 | 205.999 | 0.60× | 1.06× |
| float_math | 230.999 | 253.000 | 1.10× | 0.82× |
| func_call | 149.999 | 134.000 | 0.89× | 0.85× |
| fib_recursive | 85.999 | 90.999 | 1.06× | 1.01× |
| array_rw | 60.999 | 50.999 | 0.84× | 1.06× |
| map_str | 73.999 | 65.999 | 0.89× | 1.67× |
| string_ops | 65.999 | 62.999 | 0.95× | 0.43× |
| particles | 51.000 | 56.999 | 1.12× | 1.10× |
| Sum of case medians | 1062.994 | 920.994 | 0.87× | 0.89× |

Three-way session medians for the working-tree build: NeoCore 913.994 ms, Lua
1029.994 ms, C++ 163.783 ms; the last column above uses that session's Lua
medians (loop_sum 192.999, float_math 308.999, func_call 157.999, fib_recursive
88.999, array_rw 47.000, map_str 38.999, string_ops 145.000, particles 49.999).
For the first time the NeoCore sum is below the Lua sum, and `loop_sum` is within
7% of Lua.

What the pair does and does not show:

- `loop_sum` (0.60×), `array_rw` (0.84×), `func_call` and `map_str` (0.89×) have
  non-overlapping ranges and a bytecode change in their hot loops, so the lowering
  change is the cause.
- `float_math` (1.10×) and `particles` (1.12×) also have non-overlapping ranges, but
  the `Particles` inner loop is byte-identical between the two builds and the
  `FloatMath` inner loop only lost a `MOV`. A separate micro-benchmark whose hot
  loops are byte-identical in both executables (declaration-only float and integer
  bodies) runs 15–18% slower on the working-tree executable, so this pair's float
  and particle differences are the executables' code layout, not the lowering. A
  layout-controlled measurement would run the new bytecode on the baseline VM
  binary; the CLI has no image-run mode, so that was not done.
- The `Particles` inner loop in `2b096ca` itself carries two more `MOV`
  instructions than in `7dcc5a1` (the collection element write path now copies
  the value operand before `CLT_MOV`), which is where the earlier `particles`
  movement from 47 to 51 ms came from. That is independent of this change.

## Build configuration unification (commit `96b5672`, working tree CMake change)

Every snapshot above measured the CMake Release build, which compiled with
`/O2 /Ob2 /MD` and linked without link-time code generation. The native `.vcxproj`
Release build of the same sources, and the archived NeoScript console used as the
dynamic baseline, compile with `/GL` and link with `/LTCG`; the native NeoCore build
also uses the static CRT (`/MT`). The CMake project now enables both through
`NEOCORE_ENABLE_IPO` and `NEOCORE_STATIC_CRT` (see the ReadMe), so the three builds
share their optimization and runtime settings. The bytecode of `bench.nc` is identical
in all three executables; only the VM binary differs.

Paired sessions on the same commit, seven measured rounds each, all checksums matching:

| Case | CMake `/O2 /MD` | Native `/GL /LTCG /MT` | CMake unified | Unified / old CMake | Unified / native |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 200.999 | 187.999 | 187.000 | 0.93× | 0.99× |
| float_math | 266.000 | 229.999 | 223.000 | 0.84× | 0.96× |
| func_call | 144.000 | 135.000 | 137.999 | 0.96× | 1.01× |
| fib_recursive | 89.999 | 90.999 | 86.999 | 0.97× | 0.95× |
| array_rw | 51.000 | 49.999 | 48.999 | 0.96× | 0.98× |
| map_str | 62.999 | 59.000 | 60.999 | 0.97× | 1.02× |
| string_ops | 65.000 | 60.000 | 67.000 | 1.03× | 1.12× |
| particles | 51.999 | 51.000 | 46.000 | 0.88× | 0.90× |
| Sum of case medians | 931.996 | 863.996 | 857.996 | 0.92× | 0.99× |

The old-CMake and native columns come from the pair each was measured in (12:50:52 to 12:52:05 UTC and 12:49:41 to 12:50:51 UTC);
the unified column shows the values from the old-CMake pair, and the last ratio uses the
unified values from the native pair. An earlier pair of the old CMake build against the
native build (12:25:05 to 12:26:19 UTC, `Build/nc-3way-benchmark-9/paired/measurements.json`) gave a sum ratio of 0.93×.

Three-way session with the unified CMake build (12:52:05 to 12:53:29 UTC):

| Case | NeoCore | Lua | C++ | NeoCore / Lua |
| --- | ---: | ---: | ---: | ---: |
| loop_sum | 187.999 | 191.999 | 12.577 | 0.98× |
| float_math | 223.000 | 308.000 | 58.357 | 0.72× |
| func_call | 137.000 | 157.000 | 4.238 | 0.87× |
| fib_recursive | 86.999 | 89.999 | 6.851 | 0.97× |
| array_rw | 49.000 | 46.999 | 2.452 | 1.04× |
| map_str | 60.000 | 40.999 | 62.660 | 1.46× |
| string_ops | 66.999 | 145.000 | 13.533 | 0.46× |
| particles | 46.000 | 48.999 | 3.434 | 0.94× |
| Sum of case medians | 856.997 | 1028.995 | 164.102 | 0.83× |

What this does and does not show:

- The unified build is within 1% of the native build in the sum, so the two build
  systems now measure the same VM. `string_ops` (1.12×) and `particles` (0.90×) still
  separate between them; the native project additionally uses `/EHa`, `/GR-` and `/Gy`,
  which the CMake build does not adopt, and the rest is code layout.
- Relative to the earlier snapshots, roughly 8% of the NeoCore sum was build
  configuration rather than VM or lowering behaviour. Their NeoCore columns are
  therefore pessimistic by about that much; the Lua and C++ columns are unaffected.
- Against the archived NeoScript console (also `/GL /LTCG`, `/MD`, no PGO), `loop_sum`
  on the native NeoCore build measured 192–195 ms against 182–183 ms in alternated
  single runs, while per-instruction micro-benchmarks with identical loop shapes
  (`opmicro.nc`/`opmicro.ns` in the session scratch directory) ran faster on NeoCore for
  independent and dependent typed adds and for comparisons. The remaining `loop_sum`
  difference is NeoScript's two-operand in-place `ADD2_L` for `sum += t`, which writes
  no type tag, against NeoCore's three-operand `I64_ADD` with destination equal to the
  left operand.

Executable SHA-256: unified CMake `745d78df99443920c5772b40344dec17686b7860a5725172f70aae4dd5a86b78`, native `2433f73fea2c2935beadaf35aa582dadd3a918fbebb0011148e439d466642859`, old CMake `0fa351f3cbe1a9413cb1da8bb48ea1c7663f744a0a83d0a3844fad191376a61b`.
Records: `Build/nc-3way-benchmark-10/pair_o2_vs_final`, `pair_native_vs_final` and
`threeway_cmake_final` (each `measurements.json`).

## What this comparison includes

- `Samples/bench/bench.nc` changed in `cb522bb`: the four `m.get("key", 0)`
  calls became `m["key"]`, because `map.get` no longer exists. The algorithm,
  key set and iteration counts are unchanged and the checksum still matches Lua
  and C++. `bench.lua` and `bench.cpp` are unchanged from the earlier snapshots.
- NeoCore uses the CMake Release configuration (`/O2 /Ob2 /DNDEBUG`, MSVC). Lua is
  the same prebuilt 5.5 Win64 binary and the C++ executable is the same file as in
  the earlier snapshots. Build configuration and code layout differ between these
  executables and can affect the comparison.

## What this comparison cannot isolate

- No NeoScript column: the archived dynamic console did not run.
- No generic-op control: the generic arithmetic path no longer exists in the
  source, so the historical
  [typed numeric VM comparison](TypedVmBenchmark.md) remains the only such
  measurement.
- No instruction counts or dispatch profiles were collected. `loop_sum` and
  `array_rw` are now the cases to examine next: a bytecode dump of their inner
  loops alongside a paired measurement in one session, before any change is
  claimed as an improvement.

## Artifact identity

SHA-256 values from the saved session records:

| Executable | SHA-256 |
| --- | --- |
| NeoCore `cb522bb` | `b8bda0b32963fd21606193e1f8a47c1587b08178e165625e3105d0a2d165afaa` |
| NeoCore `dca2d64` (paired run) | `6409a1943fb5b1497f45643644d08272c2720b54ab705001eb9905cf2b58f3fe` |
| Lua 5.5 | `0d20768e1b2b438fba72475b9da71a8d7d0738254ce7cdadc24f6a42fdb70775` |
| C++ (`bench_cpp.exe`) | `c39646891de34636c9e1a6398bb7b01f5c5ba8a15f70b87fdf6a4b7d4dcd0869` |

| Source | SHA-256 |
| --- | --- |
| Samples/bench/bench.nc | `5ff95969fdcb4dc5cd0625c839c775611e0fe6dbb89390cab41fb04b38b6dae6` |
| Samples/bench/bench.lua | `7fdc9d39636cacad41cbd29db4f81db82126d0513ca9f5e1d872d7308983486b` |
| Samples/bench/bench.cpp | `fbdb520916b45861146f570407036267c4fc5f02e3c812177ce44b633acb841b` |

These hashes identify the measured snapshots. Files at the linked source paths can
change during subsequent work.

<details>
<summary>All recorded process-level times (milliseconds)</summary>

Each cell is that process's best-of-five result. The warmup column is retained for
audit and excluded from the published medians.

### Reported three-way session: NeoCore `cb522bb`

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 344.000 | 346.000 | 336.999 | 336.000 | 336.000 | 336.000 | 335.999 | 336.999 |
| float_math | 232.999 | 238.000 | 234.000 | 229.000 | 229.000 | 230.999 | 230.999 | 229.000 |
| func_call | 149.000 | 156.000 | 149.000 | 147.999 | 147.000 | 153.000 | 146.999 | 146.999 |
| fib_recursive | 87.000 | 91.000 | 86.999 | 85.999 | 86.000 | 86.999 | 86.999 | 89.999 |
| array_rw | 61.999 | 60.999 | 60.999 | 62.000 | 60.000 | 59.999 | 60.999 | 60.000 |
| map_str | 75.999 | 77.999 | 76.999 | 75.999 | 76.000 | 76.000 | 76.000 | 75.000 |
| string_ops | 64.999 | 64.000 | 64.000 | 63.000 | 64.000 | 64.999 | 63.000 | 62.999 |
| particles | 48.000 | 49.000 | 46.000 | 46.999 | 46.999 | 46.999 | 46.999 | 45.999 |

### Reported three-way session: Lua

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 197.000 | 199.999 | 190.000 | 190.000 | 189.999 | 191.000 | 190.999 | 200.999 |
| float_math | 310.000 | 309.000 | 316.999 | 304.000 | 302.999 | 307.999 | 308.000 | 303.999 |
| func_call | 159.999 | 161.000 | 163.999 | 156.000 | 154.999 | 156.000 | 161.999 | 155.000 |
| fib_recursive | 92.000 | 90.999 | 90.999 | 87.000 | 88.000 | 88.999 | 88.999 | 88.000 |
| array_rw | 48.000 | 47.000 | 47.000 | 45.999 | 45.999 | 47.999 | 48.000 | 45.999 |
| map_str | 43.999 | 41.999 | 45.999 | 40.000 | 41.000 | 41.000 | 42.000 | 40.000 |
| string_ops | 152.000 | 150.000 | 152.999 | 141.000 | 142.999 | 147.999 | 154.999 | 144.000 |
| particles | 51.000 | 51.000 | 49.999 | 48.000 | 48.000 | 51.999 | 51.000 | 49.000 |

### Reported three-way session: C++

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 13.089 | 13.074 | 13.109 | 12.556 | 12.745 | 12.549 | 13.175 | 12.279 |
| float_math | 60.623 | 60.373 | 59.282 | 57.391 | 57.392 | 58.669 | 60.848 | 57.217 |
| func_call | 4.467 | 4.403 | 4.470 | 4.200 | 4.392 | 4.116 | 4.514 | 4.127 |
| fib_recursive | 7.179 | 7.161 | 7.019 | 6.688 | 6.888 | 6.628 | 7.194 | 6.900 |
| array_rw | 2.620 | 2.571 | 2.573 | 2.404 | 2.443 | 2.471 | 2.557 | 2.453 |
| map_str | 62.746 | 63.416 | 61.747 | 61.890 | 62.412 | 62.363 | 63.600 | 60.694 |
| string_ops | 14.089 | 14.106 | 13.805 | 13.470 | 13.198 | 13.757 | 14.148 | 13.605 |
| particles | 3.589 | 3.511 | 3.532 | 3.365 | 3.299 | 3.520 | 3.595 | 3.427 |

### Paired session: NeoCore `dca2d64`

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 349.000 | 347.999 | 347.000 | 353.999 | 360.000 | 359.999 | 359.000 | 349.000 |
| float_math | 271.999 | 272.000 | 272.000 | 271.999 | 288.999 | 278.000 | 279.000 | 267.999 |
| func_call | 165.000 | 165.000 | 162.999 | 165.999 | 193.999 | 168.000 | 169.999 | 162.999 |
| fib_recursive | 91.999 | 94.000 | 91.999 | 92.999 | 96.000 | 94.999 | 94.999 | 93.000 |
| array_rw | 59.000 | 58.999 | 58.000 | 57.999 | 60.999 | 60.999 | 60.999 | 58.999 |
| map_str | 259.000 | 259.000 | 251.000 | 258.000 | 269.000 | 264.000 | 261.000 | 253.000 |
| string_ops | 62.999 | 64.999 | 64.000 | 64.000 | 70.999 | 64.999 | 67.000 | 64.000 |
| particles | 51.999 | 51.000 | 51.999 | 51.999 | 52.999 | 51.999 | 52.999 | 52.000 |

### Paired session: NeoCore `cb522bb`

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 346.000 | 345.999 | 346.000 | 344.000 | 359.000 | 364.000 | 351.000 | 348.000 |
| float_math | 238.999 | 233.000 | 234.999 | 233.000 | 244.999 | 246.999 | 242.999 | 238.999 |
| func_call | 150.999 | 149.999 | 151.000 | 149.000 | 160.000 | 154.999 | 154.999 | 153.999 |
| fib_recursive | 88.000 | 87.000 | 86.000 | 86.999 | 89.000 | 89.999 | 90.000 | 89.000 |
| array_rw | 62.999 | 60.999 | 61.999 | 60.999 | 63.000 | 62.999 | 62.999 | 62.999 |
| map_str | 78.000 | 78.999 | 76.999 | 78.999 | 78.999 | 80.999 | 80.000 | 81.999 |
| string_ops | 65.000 | 64.000 | 62.999 | 62.999 | 68.999 | 65.999 | 64.999 | 64.000 |
| particles | 47.999 | 48.000 | 46.999 | 49.000 | 49.999 | 49.999 | 49.000 | 47.000 |

### First three-way session (another agent's compiler active; audit only)

Medians: NeoCore 1072.996 ms, Lua 1054.996 ms, C++ 167.991 ms; `map_str`
78.999 / 40.999 / 62.654 ms. Round 7 carries outliers in all three columns
(for example NeoCore `fib_recursive` 136.999 ms, Lua `particles` 78.999 ms).

#### NeoCore `cb522bb`

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 349.000 | 347.000 | 349.999 | 343.000 | 348.000 | 347.000 | 352.000 | 364.000 |
| float_math | 235.000 | 234.000 | 233.000 | 233.000 | 233.000 | 237.000 | 258.999 | 238.999 |
| func_call | 152.999 | 152.000 | 148.000 | 147.000 | 148.000 | 149.999 | 155.000 | 179.999 |
| fib_recursive | 87.000 | 88.000 | 86.999 | 85.999 | 86.999 | 88.000 | 89.000 | 136.999 |
| array_rw | 60.999 | 60.999 | 59.999 | 61.999 | 60.999 | 62.000 | 61.999 | 91.999 |
| map_str | 76.999 | 78.999 | 76.999 | 77.999 | 80.000 | 77.999 | 78.999 | 96.000 |
| string_ops | 64.999 | 62.999 | 62.999 | 63.000 | 64.000 | 65.000 | 62.999 | 68.999 |
| particles | 48.999 | 48.999 | 46.000 | 51.999 | 48.999 | 48.999 | 49.999 | 49.999 |

#### Lua

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 196.999 | 196.000 | 190.999 | 197.999 | 199.999 | 197.000 | 198.999 | 200.000 |
| float_math | 314.999 | 311.999 | 305.999 | 316.999 | 314.000 | 314.999 | 313.000 | 320.000 |
| func_call | 160.000 | 159.000 | 157.999 | 161.999 | 157.999 | 160.000 | 158.999 | 162.999 |
| fib_recursive | 91.000 | 92.000 | 88.999 | 92.000 | 90.000 | 90.999 | 92.000 | 103.999 |
| array_rw | 47.999 | 48.000 | 45.999 | 47.999 | 49.000 | 48.999 | 53.000 | 52.000 |
| map_str | 44.999 | 37.999 | 36.999 | 40.999 | 43.999 | 36.999 | 43.000 | 59.999 |
| string_ops | 150.999 | 150.999 | 147.000 | 151.000 | 147.999 | 147.999 | 171.000 | 186.000 |
| particles | 50.000 | 52.000 | 49.999 | 51.000 | 51.000 | 51.000 | 51.999 | 78.999 |

#### C++

| Case | Warmup | Round 1 | Round 2 | Round 3 | Round 4 | Round 5 | Round 6 | Round 7 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 12.807 | 13.104 | 13.102 | 13.146 | 12.828 | 12.745 | 13.158 | 13.411 |
| float_math | 59.226 | 60.542 | 58.998 | 59.668 | 59.386 | 60.587 | 60.614 | 60.689 |
| func_call | 4.371 | 4.466 | 4.424 | 4.477 | 4.387 | 4.377 | 4.470 | 4.492 |
| fib_recursive | 7.159 | 7.161 | 7.152 | 7.184 | 7.154 | 7.026 | 7.164 | 7.164 |
| array_rw | 2.546 | 2.557 | 2.625 | 2.604 | 2.572 | 2.493 | 2.824 | 2.796 |
| map_str | 61.512 | 61.312 | 61.406 | 62.654 | 63.841 | 60.504 | 63.455 | 64.113 |
| string_ops | 14.111 | 13.815 | 13.850 | 14.071 | 13.850 | 13.880 | 14.107 | 14.120 |
| particles | 3.510 | 3.496 | 3.513 | 3.590 | 3.580 | 3.576 | 3.601 | 3.585 |

</details>
