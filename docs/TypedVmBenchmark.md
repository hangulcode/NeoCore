# Typed numeric VM benchmark — 2026-10-31

The first typed arithmetic, conversion, and comparison implementation is **7.51%
slower than the current generic-op control** on the sum of eight case medians.
The largest clear regression is `float_math` (+22.51%). These are measured
implementation results, not a conclusion about static typing in general.

## Method and scope

- Windows x64 session: 2026-10-31 00:53:12–00:55:54 KST
  (2026-10-30T15:53:12.466048+00:00 to 2026-10-30T15:55:54.281120+00:00 UTC).
- One serial warmup round, followed by seven serial measured rounds with rotating
  executable order. Each script reports its best of five internal repetitions;
  the table uses the median of those seven process-level results.
- Forty processes completed: five warmups and thirty-five measured runs.
  All 320 case checksums matched the fixed expected results.
- NeoCore generic and typed modes use **the exact same benchmark host executable**
  and Release VM library. Only `CompileStaticSource(..., typedBackend)` changes.
  The host compiles the checked source and runs its global initialization without
  debug metadata; script timers exclude process startup and compilation.
- This compares the cumulative arithmetic/conversion/comparison path. It does not
  isolate the latest comparison change or the cost of enlarging the VM dispatch loop.
- The historical first NeoCore executable was not archived. The generic column is
  a current control, not the historical executable from [the first snapshot](FirstNcBenchmark.md).
- NeoScript is the archived VS console build from
  `ef70881a94bc1e0963ff7cd3b9ec058b9b12fa6e`; NeoCore libraries and the comparison
  host use CMake Release with MSVC 19.51. Cross-language differences include build
  configuration, host, and code layout. The comparison host is not the shipped CLI executable.
- The source files are unchanged from the first snapshot. Typed `map.get(key, 0)`,
  typed list initialization, and explicit `tostring` remain in both NeoCore modes.
  `array_rw` uses `list<int>`; `particles` uses four `list<float>` collections.
- Raw records and per-process output are under the ignored
  `Build/nc-typed-benchmark` directory. The original snapshot was preserved.

## Results

Times are milliseconds. Three decimal places retain recorded units, not a claim
of timer accuracy. Positive change means that typed operations took longer.

| Case | NC generic | NC typed | Change | NeoScript | Lua | C++ |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 176.000 | 188.999 | 7.39% | 177.000 | 190.999 | 12.832 |
| float_math | 190.999 | 234.000 | 22.51% | 169.999 | 313.000 | 57.994 |
| func_call | 121.999 | 132.000 | 8.20% | 127.000 | 156.000 | 4.286 |
| fib_recursive | 80.999 | 83.000 | 2.47% | 85.999 | 88.000 | 6.866 |
| array_rw | 46.999 | 48.000 | 2.13% | 41.000 | 46.999 | 2.463 |
| map_str | 223.999 | 222.999 | -0.45% | 36.999 | 40.999 | 61.730 |
| string_ops | 57.999 | 57.999 | 0.00% | 48.999 | 145.999 | 13.425 |
| particles | 46.999 | 49.999 | 6.38% | 44.999 | 49.999 | 3.370 |
| Sum of medians | 945.993 | 1016.996 | +7.51% | 731.995 | 1031.995 | 162.966 |

This sum is not process wall time or a general language-performance score.
The typed sum is 38.93% higher than the NeoScript sum and 1.45% lower than Lua's.
The explicit map fallback/helper is a major remaining workload difference:
both NeoCore modes take about 223–224 ms for `map_str`, versus 37 ms for NeoScript.

## Repeatability

| Case | Generic min–max | Typed min–max |
| --- | ---: | ---: |
| loop_sum | 174.000–177.999 | 184.999–194.000 |
| float_math | 189.000–193.999 | 228.999–238.999 |
| func_call | 119.000–128.000 | 129.000–138.999 |
| fib_recursive | 79.999–88.000 | 81.999–86.999 |
| array_rw | 45.999–47.000 | 46.999–48.000 |
| map_str | 213.000–234.000 | 216.000–229.000 |
| string_ops | 57.000–61.999 | 57.000–60.000 |
| particles | 45.999–49.999 | 48.999–51.999 |

`loop_sum`, `float_math`, and `func_call` were slower in the typed mode in every
paired round, and their seven-sample ranges do not overlap. These are clear
regressions in this session. The recursive Fibonacci medians are 80.999 versus
83.000 ms, but their ranges overlap; this is not evidence of a recursion speedup
or a clearly isolated regression. The map and string differences are too small
relative to variation to claim an improvement. Lua's `func_call` had a wide
154.999–226.999 ms range, despite its 156.000 ms median.

## Code-grounded follow-up

These are implementation facts and likely cost sources, not measured attribution
of a percentage to each change:

1. `PatchLocalOps` specializes generic ADD3/SUB3/MUL3 for all-local operands.
   The typed operations have no corresponding specialization and use
   `GetVarPtrF1/2/3`, which decode slot-location flags.
2. `HoistableOperandMask` does not include the typed instructions. Constants
   used by them therefore miss the existing loop-constant localization pass.
3. The mixed integer result multiplied by `0.5` in `FloatMath` emits
   `I64_TO_F64` followed by `F64_MUL`. The generic multiplication handles the
   mixed operands inside one instruction. The separate conversion adds one
   dispatch and a temporary value store/read on each of ten million inner iterations.
4. Typed numeric operations still use the shared destination cleanup/tag-store
   path, and compound assignments such as `+=` remain generic. Removing input
   type dispatch alone does not eliminate these costs.

The saved image contains 511 instructions in generic mode and 507 in typed mode.
This static count does not measure execution frequency: five typed conversion
sites include the conversion in the hot `FloatMath` loop. Executing different
handlers can also affect cache and layout behavior even in the same executable.

The next controlled experiments should restore local operand handling and
constant localization, then examine conversion/operation combining separately.
Do not infer the size of a potential gain without another paired measurement.
No optimization or rollback was applied as part of this measurement.

## Checksums

| Case | Expected result |
| --- | ---: |
| loop_sum | 12497500 |
| float_math | 21666 |
| func_call | 12502500 |
| fib_recursive | 2178309 |
| array_rw | 25493856 |
| map_str | 90000 |
| string_ops | 438890 |
| particles | 10461222 |

## Artifact identity

SHA-256 identifies the uncommitted measured snapshot. Both NeoCore modes use
the same executable hash.

| Artifact | SHA-256 |
| --- | --- |
| NeoCore comparison host | `489a0610aa0a1cbaa8847e3b9b4cce168c1fee3286039a02aa722aed8ce58a9f` |
| NeoScript | `3d3eaab5a380343191363a072e0832c73b818319273adb95daa18a661682d1ad` |
| Lua | `0d20768e1b2b438fba72475b9da71a8d7d0738254ce7cdadc24f6a42fdb70775` |
| Cpp | `dd6d54066f12e97bacd4cf44cf0ec7481b01a005da6dcd7e29bc51104ffd5317` |
| neoscript.lib | `2ebf2a2182df78bb3258ca985e05b631d3c6bf4df0506b4ab56864921b67a585` |
| neocore_frontend.lib | `93861c88e4d48afc9b915cfaf900cea6c1362f3704d3d58ca59f2aedc9943cba` |
| neocore_type_model.lib | `0087ca90e88aeecd0136a89c21d3db6cfc3d48c976c25045a932fa367f3fe3c9` |
| bench.nc | `3fd1b29392f63641cc6cceb265be25a3cac145d30618681bd8f88111176181c4` |
| bench.ns | `76d29c92dd8e5e5e94366e802b301e51087325be8248742f7a95bbe2fc674b1d` |
| bench.lua | `02ec5fd6d5a1016a31e0c5ae345232e2b6d21e904a7509f12f3ca086005e87ed` |
| bench.cpp | `d5575fe210bb9d06267810aca1ce1e331a7a427b9b703893927e5d7a44bac2ae` |

## 2026-11-08: fixed-storage copies and return specialization

This experiment retains compiler-time temporary-slot compaction, raw known-reference copies,
and separate payload-only/owning return paths. It also fixes nested synchronous sort callbacks
and Debug compiler stack exhaustion while rendering long string concatenations. The return
specialization is retained despite the unresolved performance regressions listed below.

Windows x64 Release, MSVC 19.51, `/O2`, IPO off, static CRT. The control is commit `75ef4d2`.
After the user stopped video playback, three immutable binaries ran in rotating order:
control, specialized returns before the void-path fix, and the final implementation. One
full warmup preceded seven measured rounds. Script `REPS` was 1; the table reports the
median of seven internal execution times, excluding compilation/startup. Workloads retain
the original iteration counts. All 16 case checksums matched in every process.

| Case | Control (ms) | Before void fix (ms) | Final (ms) | Final/control |
| --- | ---: | ---: | ---: | ---: |
| loop_sum | 175.000 | 176.000 | 176.000 | 1.006x |
| float_math | 154.000 | 168.999 | 167.999 | 1.091x |
| func_call | 126.000 | 127.000 | 125.000 | 0.992x |
| fib_recursive | 87.000 | 83.999 | 83.000 | 0.954x |
| array_rw | 43.999 | 45.000 | 43.999 | 1.000x |
| map_str | 40.000 | 50.000 | 50.000 | 1.250x |
| string_ops | 45.000 | 47.999 | 46.999 | 1.044x |
| particles | 44.999 | 47.000 | 47.000 | 1.044x |
| call_floor | 26.000 | 25.000 | 25.000 | 0.962x |
| call_void | 91.000 | 112.000 | 94.000 | 1.033x |
| call_ret0 | 98.000 | 100.999 | 98.000 | 1.000x |
| call_ret1 | 108.000 | 115.999 | 114.000 | 1.056x |
| call_ret3 | 132.000 | 139.000 | 136.000 | 1.030x |
| string_copy | 60.000 | 37.000 | 37.000 | 0.617x |
| list_copy | 90.000 | 39.000 | 37.000 | 0.411x |
| owned_call | 100.000 | 83.000 | 82.000 | 0.820x |

The eight-case suite sums to 715.998 ms for the control and 739.997 ms for the final implementation (+3.35%). The reference-copy microbenchmarks are separate workloads and are not included in that total.

The confirmed `call_void` regression came from treating the unused boxed return slot of
a void function as a reason to select the owning return path. The compiler and image
validator now permit a no-result return to specialize when all arguments, locals and
temporaries are payload-only and there are no captures. The slow path dropped from
112 ms to 94 ms; the control was 91 ms.

**Unresolved:** `float_math`, `map_str`, `string_ops`, `particles`, and some call cases
remain slower. Restoring the old return execution path in a diagnostic control recovered
most of the large regressions while keeping raw copies and frame compaction; that control
is not the retained implementation. In the final object, `RunInternal<false,false>` is
23,960 bytes versus 25,608 bytes in the control, and reserves 928 rather than 960 bytes of
C++ stack. The inspected floating-point operand-load/multiply instruction sequences are
unchanged, but their addresses move. This implicates changes around interpreter code
generation/layout; a specific cache or branch-prediction cause has not been isolated.
Do not describe these regressions as fixed or attribute them to extra runtime type checks.

**Follow-up (2026-11-08): code placement, not the return split.** The F64 handlers of
`c74df0c` and of the same tree with the single return handler restored are byte-identical;
only their addresses differ. Inserting 16, 32 or 48 bytes of `nop` at the top of
`RunInternal<false,false>` (and nowhere else) moved `float_math` between about 151 and
185 ms in both sources: fast when the 45-byte dispatch sequence starts at offset 0 or 16
of a 64-byte line, slow at 32 or 48. `c74df0c` happened to land at 32 and the restored
handler at 0. The split return handler is therefore kept. Hardware counters were not
available, so the micro-architectural mechanism is not identified. When comparing builds,
record the dispatch offset and compare at equal offsets.

Two items of the list above were then implemented. Loop-constant hoisting now covers
typed numeric instructions, and the runtime arithmetic and comparison-jump handlers read
operands through one frame base when every operand is local. Compared at each of the four
dispatch offsets against `c74df0c`, the eight-case sum fell by 6–8%, `loop_sum` by about
15% and `func_call` by about 9%, and `float_math` stayed at 154–155 ms at every offset.
`string_ops` moved by a few milliseconds in either direction with the offset. Keeping the
code pointer in a register instead of the worker member was also tried and measured
slower (extra register copies at the dispatch head), so it was not kept. The session
record is [FourWayBenchmark.md](FourWayBenchmark.md).

Validation: Windows Release and Debug CTest each passed 84/84, including protocol and
C ABI integration checks. Changed C++ files passed Android NDK Clang and Linux GCC C++11
syntax checks; GCC still reports the existing anonymous-struct pedantic warnings. Image
version 0165 requires cached scripts to be recompiled.
