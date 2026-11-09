# Four-way benchmark snapshot (2026-11-10)

This records the eight-case benchmark for the NeoCore tree that pairs frequent adjacent
instructions into one dispatch and performs range-loop entry tests in place, on top of
commit `a871cb4` (which already carries the in-place string concatenation and the cached
value-stack base), measured in one session against the NeoScript console, Lua 5.5 and
the C++ reference. All eight checksums matched in every process of every round. The
summary table in [ReadMe.md](../ReadMe.md#performance-test-results) is derived from this
record, and [Benchmarking.md](Benchmarking.md) describes the method.

## Method

- Session 2026-11-09 15:31:29-15:32:49 UTC. One warmup round followed by five measured rounds;
  each round runs the four executables serially in an order rotated by one position.
  Runner: `Tools/bench/measure.py --rounds 5 --label readme20261110`.
- Each process reports the best of five in-process repetitions per case; the tables
  show the median of those per-round values and the per-round range.
- The NeoCore console is a CMake Release build without IPO and runs without debug
  metadata (`neocore --file bench.nc`). NeoScript runs its own `bench.ns`; Lua runs
  `bench.lua`; the C++ executable is the same `bench_cpp.exe` as in the earlier snapshots.
- No other build or test ran during the session.

## Environment

```text
CPU       : Intel Core i7-12700F (12C/20T)      RAM : 64 GB
OS        : Windows 11 Pro 64-bit (build 26200)
NeoCore   : CMake Release, MSVC 19.51 /O2 /Ob2 /MT, static CRT, no IPO
NeoScript : console.exe, Release x64, whole-program optimization off
Lua       : Lua 5.5.0 (official Win64 binary)
C++       : MSVC 19.51 /O2 /std:c++17 (x64)
```

## Executables and sources

| Name | File | Dispatch offset | SHA-256 (prefix) |
| --- | --- | ---: | --- |
| NeoCore | `Build/neocore/bin/Release/neocore.exe` (working tree) | 48 | `2102a7ac06c2c285` |
| NeoScript | `Samples/console/x64/Release/console.exe` (NeoScript repository) | | `531b2a942dae97e9` |
| Lua | `lua55.exe` | | `0d20768e1b2b438f` |
| C++ | `Build/nc-3way-benchmark-5/bench_cpp.exe` | | `c39646891de34636` |

"Dispatch offset" is the position, within a 64-byte line, of the first instruction of
the dispatch sequence in the benchmark's `RunInternal` instantiation
(`Tools/bench/dispatch_offset.py`). This build sits at offset 48; in the same-position
table below the tree measures 611-625 ms across the four offsets, so the placement
costs this snapshot about 2% of the sum (mostly `particles`).

| Source | SHA-256 (prefix) |
| --- | --- |
| `bench.nc` | `5ff95969fdcb4dc5` |
| `bench.ns` | `76d29c92dd8e5e5e` |
| `bench.lua` | `7fdc9d39636cacad` |

## Medians (ms)

| Case | NeoCore | NeoScript | Lua | C++ | Checksum |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 139 | 175 | 192 | 12.6 | 12497500 |
| float_math | 151 | 177 | 310 | 57.9 | 21666 |
| func_call | 119 | 132 | 157 | 4.3 | 12502500 |
| fib_recursive | 80 | 86 | 89 | 6.9 | 2178309 |
| array_rw | 38 | 42 | 46 | 2.4 | 25493856 |
| map_str | 32 | 36 | 41 | 62.0 | 90000 |
| string_ops | 22 | 50 | 154 | 13.8 | 438890 |
| particles | 45 | 47 | 49 | 3.5 | 10461222 |
| Sum of case medians | 626 | 745 | 1038 | 163.3 | |

## Per-round range (ms)

| Case | NeoCore | NeoScript | Lua | C++ |
| --- | ---: | ---: | ---: | ---: |
| loop_sum | 139-140 | 175-177 | 190-194 | 12.4-12.7 |
| float_math | 150-152 | 174-180 | 305-317 | 57.4-58.8 |
| func_call | 117-122 | 131-133 | 154-158 | 4.2-4.3 |
| fib_recursive | 79-87 | 84-88 | 87-91 | 6.8-6.9 |
| array_rw | 38-39 | 42-43 | 46-47 | 2.4-2.5 |
| map_str | 32-33 | 36-38 | 36-42 | 60.8-62.8 |
| string_ops | 22-22 | 49-51 | 152-156 | 13.5-13.8 |
| particles | 45-46 | 47-49 | 49-50 | 3.4-3.5 |

## Same-position comparison

In the same session, the pre-change tree (commit `a871cb4`) and the measured tree were
built with 0, 16, 32 and 48 bytes of `nop` at the top of `RunInternal`, which shifts the
whole dispatch loop without changing any logic, and the eight builds ran alternately
for three rounds (medians in ms):

| Dispatch offset | `a871cb4` sum | Tree sum | `a871cb4` `loop_sum` | Tree `loop_sum` | `a871cb4` `func_call` | Tree `func_call` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 645 | 611 | 150 | 137 | 122 | 111 |
| 16 | 625 | 614 | 152 | 139 | 112 | 112 |
| 32 | 634 | 618 | 159 | 140 | 111 | 112 |
| 48 | 630 | 625 | 151 | 145 | 113 | 112 |

## Observations

- `loop_sum` is faster at every dispatch offset with separated ranges: its statement
  `sum += i + x` now runs as one paired dispatch (`I64_ADD` followed by `I64_ADD2`).
  `func_call` separates only at offset 0, where the pre-change build was slow; the
  other six cases stay within one round's spread. The benchmark suite stages few call
  arguments through copies, so the copy and immediate pairs barely appear in it.
- The pairs were chosen from dynamic adjacent-opcode counts. In a game project's
  headless regression run (about 20 billion instructions) local payload copies were
  14% of all instructions and the paired shapes about 16%; its wall-clock time went
  from 72.6-72.8 s to 68.8-69.4 s with the paired build at equal dispatch offset.
- Against NeoScript the tree is faster in seven cases with separated ranges and within
  one round's spread in `fib_recursive`; the sum of case medians is 0.84x NeoScript and
  0.60x Lua.

## What this comparison cannot isolate

- Code placement: shifting only the dispatch loop has moved single cases by up to
  about 25%, so differences between NeoCore builds are attributed to source changes
  only when the ranges separate at equal dispatch offsets.
- Results from other sessions are not comparable with these numbers: between the
  previous record and this one the unchanged NeoScript, Lua and C++ binaries all moved
  by about 1-2%. Earlier snapshots of this file are in the repository history.
- The C++ reference is a ceiling, not a peer; its `map_str` case uses
  `std::unordered_map<std::string, int>` and is slower than the scripts, which hash
  interned keys once.
- Lua and NeoScript are separate binaries with their own build settings.
