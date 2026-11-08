# Four-way benchmark snapshot (2026-11-08, local operands)

This records the eight-case benchmark for the NeoCore tree that extends loop-constant
hoisting to typed numeric instructions and reads all-local operands of the runtime
arithmetic and comparison-jump handlers through one frame base, on top of commit
`c74df0c`. It was measured in one session against `c74df0c`, the NeoScript console,
Lua 5.5 and the C++ reference. All eight checksums matched in every process of every
round. The summary table in [ReadMe.md](../ReadMe.md#performance-test-results) is
derived from this record.

## Method

- Session 2026-11-08 19:09:13-19:10:59 KST. One warmup round followed by five measured
  rounds. Each round runs the five executables serially in a rotated order; the runner
  is `Build/nc-perf-20261107/measure.py` (session label `local20261108`), which stores
  every process's output.
- Each process reports the best of five in-process repetitions per case; the tables
  show the median of those per-round values and the per-round range.
- NeoCore runs without debug metadata (`neocore --file bench.nc`). NeoScript runs its
  own `bench.ns`; Lua runs `bench.lua`; the C++ executable is the same `bench_cpp.exe`
  as in the earlier snapshots.
- No other build ran during the session. Every executable, the non-NeoCore ones
  included, ran about 3% slower than in the previous session on the same machine, so
  only same-session ratios are meaningful.

## Environment

```
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
| NeoCore | `Build/neocore/bin/Release/neocore.exe` (working tree) | 48 | `9b4c2fd31bac505a` |
| NeoCore `c74df0c` | worktree build of commit `c74df0c` | 32 | `b2ca135b416e150d` |
| NeoScript | `Samples/console/x64/Release/console.exe` (NeoScript repository) | | `531b2a942dae97e9` |
| Lua | `lua55.exe` | | `0d20768e1b2b438f` |
| C++ | `Build/nc-3way-benchmark-5/bench_cpp.exe` | | `c39646891de34636` |

"Dispatch offset" is the position, within a 64-byte line, of the first instruction of
the dispatch sequence in `RunInternal<false,false>` (the fourth `movzx eax,byte ptr
[rdi]; cmp al,0CDh` site in the executable).

| Source | SHA-256 (prefix) |
| --- | --- |
| `bench.nc` | `5ff95969fdcb4dc5` |
| `bench.ns` | `76d29c92dd8e5e5e` |
| `bench.lua` | `7fdc9d39636cacad` |
| `bench.cpp` | `fbdb520916b45861` |

## Medians (ms)

| Case | NeoCore | NeoCore `c74df0c` | NeoScript | Lua | C++ | Checksum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 147 | 174 | 178 | 195 | 12.8 | 12497500 |
| float_math | 155 | 184 | 180 | 314 | 58.3 | 21666 |
| func_call | 116 | 126 | 134 | 158 | 4.4 | 12502500 |
| fib_recursive | 83 | 83 | 87 | 89 | 7.0 | 2178309 |
| array_rw | 43 | 45 | 43 | 47 | 2.5 | 25493856 |
| map_str | 49 | 43 | 37 | 41 | 63.1 | 90000 |
| string_ops | 47 | 43 | 51 | 147 | 13.8 | 438890 |
| particles | 46 | 46 | 49 | 50 | 3.5 | 10461222 |
| Sum of case medians | 686 | 744 | 759 | 1041 | 165.4 | |

## Per-round range (ms)

| Case | NeoCore | NeoCore `c74df0c` | NeoScript | Lua | C++ |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 147-148 | 172-174 | 175-179 | 192-201 | 12.6-13.1 |
| float_math | 154-156 | 183-186 | 178-183 | 308-329 | 58.2-59.5 |
| func_call | 115-118 | 123-127 | 133-136 | 156-163 | 4.3-4.5 |
| fib_recursive | 83-84 | 83-84 | 86-88 | 89-91 | 7.0-7.7 |
| array_rw | 42-43 | 44-45 | 42-43 | 46-47 | 2.5-2.8 |
| map_str | 49-50 | 43-45 | 37-38 | 37-47 | 62.2-63.6 |
| string_ops | 46-48 | 43-44 | 51-53 | 146-150 | 13.7-14.2 |
| particles | 46-47 | 46-47 | 48-49 | 49-51 | 3.5-3.6 |

## Same-position comparison

Earlier in the same evening, `c74df0c` and the measured tree were rebuilt with 0, 16,
32 and 48 bytes of `nop` at the top of `RunInternal<false,false>`, which shifts the
whole dispatch loop without changing any logic, and the eight builds ran alternately
for three rounds (medians in ms):

| Dispatch offset | `c74df0c` sum | Tree sum | `c74df0c` `float_math` | Tree `float_math` | `c74df0c` `loop_sum` | Tree `loop_sum` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| +0 | 711 | 668 | 154 | 155 | 172 | 144 |
| +16 | 712 | 668 | 157 | 155 | 171 | 146 |
| +32 | 741 | 680 | 185 | 155 | 171 | 146 |
| +48 | 730 | 681 | 167 | 154 | 170 | 145 |

## Observations

- Against `c74df0c` the tree is faster at every dispatch offset: the sum falls by
  6-8%, `loop_sum` by about 15% and `func_call` by about 9%, with separated ranges.
- `float_math` of `c74df0c` depends on the dispatch offset (154-185 ms); the tree
  runs it at 154-155 ms at every offset. The F64 handlers of the two sources were
  byte-identical before this change, so the earlier swings came from code placement.
- `map_str` and `string_ops` still depend on the offset in both sources. At offset 48,
  where this build landed, `map_str` is about 49 ms for both; at offsets 0 and 16 the
  tree measured 39 ms. The table above therefore shows the tree at an unfavourable
  position for these two cases.
- Against NeoScript the tree is faster in six cases, equal in `array_rw` and slower in
  `map_str`; the sum of case medians is 0.90x NeoScript and 0.66x Lua.

## What this comparison cannot isolate

- Code placement: shifting only the dispatch loop moved single cases by up to about
  25%, so differences between NeoCore builds are attributed to source changes only
  when the ranges separate at equal dispatch offsets.
- The micro-architectural cause of the placement effect was not identified; hardware
  counters were not available in this session.
- Results from other sessions are not comparable with these numbers; earlier
  snapshots of this file are in the repository history.
- The C++ reference is a ceiling, not a peer; its `map_str` case uses
  `std::unordered_map<std::string, int>` and is slower than the scripts, which hash
  interned keys once.
- Lua and NeoScript are separate binaries with their own build settings.
