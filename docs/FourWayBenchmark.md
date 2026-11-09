# Four-way benchmark snapshot (2026-11-09)

This records the eight-case benchmark for the NeoCore tree that adds the accumulating
container read (`x += c[k];` as one instruction) and the pointer-returning string-key
map lookup on top of commit `f89532b`, measured in one session against the console of
commit `a001b41` (the build behind the previous readme table), the NeoScript console,
Lua 5.5 and the C++ reference. All eight checksums matched in every process of every
round. The summary table in [ReadMe.md](../ReadMe.md#performance-test-results) is
derived from this record, and [Benchmarking.md](Benchmarking.md) describes the method.

## Method

- Session 2026-11-09 13:50:28-13:52:09 UTC. One warmup round followed by five measured rounds;
  each round runs the five executables serially in an order rotated by one position.
  Runner: `Tools/bench/measure.py --rounds 5 --label readme20261109`.
- Each process reports the best of five in-process repetitions per case; the tables
  show the median of those per-round values and the per-round range.
- NeoCore consoles are CMake Release builds without IPO and run without debug metadata
  (`neocore --file bench.nc`). NeoScript runs its own `bench.ns`; Lua runs `bench.lua`;
  the C++ executable is the same `bench_cpp.exe` as in the earlier snapshots.
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
| NeoCore | `Build/neocore/bin/Release/neocore.exe` (working tree) | 0 | `8c9cc8fbddf76136` |
| NeoCore `a001b41` | worktree build of commit `a001b41` | 16 | `0f2d9cd7ac9fa310` |
| NeoScript | `Samples/console/x64/Release/console.exe` (NeoScript repository) | | `531b2a942dae97e9` |
| Lua | `lua55.exe` | | `0d20768e1b2b438f` |
| C++ | `Build/nc-3way-benchmark-5/bench_cpp.exe` | | `c39646891de34636` |

"Dispatch offset" is the position, within a 64-byte line, of the first instruction of
the dispatch sequence in the benchmark's `RunInternal` instantiation
(`Tools/bench/dispatch_offset.py`). Offsets 0 and 16 have measured alike; 32 and 48
slow some cases by up to 25%.

| Source | SHA-256 (prefix) |
| --- | --- |
| `bench.nc` | `5ff95969fdcb4dc5` |
| `bench.ns` | `76d29c92dd8e5e5e` |
| `bench.lua` | `7fdc9d39636cacad` |

## Medians (ms)

| Case | NeoCore | NeoCore `a001b41` | NeoScript | Lua | C++ | Checksum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 147 | 147 | 176 | 195 | 12.6 | 12497500 |
| float_math | 151 | 151 | 180 | 320 | 58.5 | 21666 |
| func_call | 113 | 112 | 133 | 159 | 4.3 | 12502500 |
| fib_recursive | 79 | 78 | 87 | 90 | 7.0 | 2178309 |
| array_rw | 39 | 42 | 42 | 47 | 2.5 | 25493856 |
| map_str | 32 | 41 | 36 | 41 | 62.6 | 90000 |
| string_ops | 49 | 42 | 50 | 155 | 13.7 | 438890 |
| particles | 42 | 40 | 48 | 50 | 3.5 | 10461222 |
| Sum of case medians | 652 | 653 | 752 | 1057 | 164.6 | |

## Per-round range (ms)

| Case | NeoCore | NeoCore `a001b41` | NeoScript | Lua | C++ |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 146-149 | 147-149 | 176-177 | 192-196 | 12.5-12.7 |
| float_math | 149-153 | 151-153 | 179-182 | 306-327 | 58.2-59.2 |
| func_call | 111-115 | 111-113 | 133-135 | 156-160 | 4.3-4.4 |
| fib_recursive | 78-80 | 78-79 | 86-88 | 89-90 | 6.9-7.0 |
| array_rw | 38-39 | 41-42 | 42-42 | 47-47 | 2.5-2.6 |
| map_str | 32-33 | 41-41 | 36-38 | 40-42 | 61.0-63.3 |
| string_ops | 48-50 | 41-43 | 49-51 | 155-156 | 13.6-13.9 |
| particles | 41-42 | 39-41 | 47-50 | 49-51 | 3.4-3.5 |

## Same-position comparison

Earlier in the same evening, the pre-change tree (commit `f89532b`) and the measured
tree were built with 0, 16, 32 and 48 bytes of `nop` at the top of `RunInternal`, which
shifts the whole dispatch loop without changing any logic, and the eight builds ran
alternately for three rounds (medians in ms):

| Dispatch offset | `f89532b` sum | Tree sum | `f89532b` `map_str` | Tree `map_str` | `f89532b` `array_rw` | Tree `array_rw` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 638 | 629 | 40 | 31 | 41 | 38 |
| 16 | 638 | 622 | 40 | 31 | 40 | 37 |
| 32 | 649 | 632 | 41 | 30 | 41 | 37 |
| 48 | 644 | 634 | 44 | 31 | 41 | 37 |

## Observations

- `map_str` and `array_rw` are faster at every dispatch offset with separated ranges;
  the other six cases stay within one round's spread. The accumulating read removes
  one dispatch and one temporary per `x += c[k];` statement, which the NeoScript
  compiler had always fused into one instruction.
- Against the `a001b41` console the sum is 0.998x. `map_str` (32 against 41 ms) and
  `array_rw` (39 against 42 ms) are faster with separated ranges, while `string_ops`
  (49 against 42 ms) and `particles` (42 against 40 ms) are slower, so the sum does not
  move in this session. The same-position runs above put `string_ops` 2-3 ms behind the
  pre-change tree at offsets 0 and 16 with touching or overlapping ranges and equal at
  32 and 48; that difference is not attributed to the change, and the two consoles here
  sit at different offsets (0 and 16).
- Against NeoScript the tree is faster in seven cases with separated ranges and within
  one round's spread in `string_ops`; the sum of case medians is 0.87x NeoScript and
  0.62x Lua.

## What this comparison cannot isolate

- Code placement: shifting only the dispatch loop has moved single cases by up to
  about 25%, so differences between NeoCore builds are attributed to source changes
  only when the ranges separate at equal dispatch offsets.
- Results from other sessions are not comparable with these numbers: between the
  previous record and this one the unchanged NeoScript, Lua and C++ binaries all moved
  by about 3%. Earlier snapshots of this file are in the repository history.
- The C++ reference is a ceiling, not a peer; its `map_str` case uses
  `std::unordered_map<std::string, int>` and is slower than the scripts, which hash
  interned keys once.
- Lua and NeoScript are separate binaries with their own build settings.
