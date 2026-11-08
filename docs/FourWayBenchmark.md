# Four-way benchmark snapshot (2026-11-08, immediates and local copies)

This records the eight-case benchmark for the NeoCore tree that adds load-time integer
immediate forms, local-operand address paths for element access, conversions and
comparisons, and dedicated local copy and immediate-store opcodes on top of commit
`0bd42bd`. It was measured in one session against `0bd42bd`, the NeoScript console,
Lua 5.5 and the C++ reference. All eight checksums matched in every process of every
round. The summary table in [ReadMe.md](../ReadMe.md#performance-test-results) is
derived from this record.

## Method

- Session 2026-11-08 19:46:23-19:48:06 KST. One warmup round followed by five measured
  rounds. Each round runs the five executables serially in a rotated order; the runner
  is `Build/nc-perf-20261107/measure.py` (session label `imm20261108`), which stores
  every process's output.
- Each process reports the best of five in-process repetitions per case; the tables
  show the median of those per-round values and the per-round range.
- NeoCore runs without debug metadata (`neocore --file bench.nc`). NeoScript runs its
  own `bench.ns`; Lua runs `bench.lua`; the C++ executable is the same `bench_cpp.exe`
  as in the earlier snapshots.
- No other build ran during the session.

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
| NeoCore | `Build/neocore/bin/Release/neocore.exe` (working tree) | 32 | `292cde04ca092140` |
| NeoCore `0bd42bd` | build of commit `0bd42bd` | 48 | `505458ff47439eb3` |
| NeoScript | `Samples/console/x64/Release/console.exe` (NeoScript repository) | | `531b2a942dae97e9` |
| Lua | `lua55.exe` | | `0d20768e1b2b438f` |
| C++ | `Build/nc-3way-benchmark-5/bench_cpp.exe` | | `c39646891de34636` |

"Dispatch offset" is the position, within a 64-byte line, of the first instruction of
the dispatch sequence in `RunInternal<false,false>` (the fourth `movzx eax,byte ptr
[rdi]; cmp al,<opcode count>` site in the executable).

| Source | SHA-256 (prefix) |
| --- | --- |
| `bench.nc` | `5ff95969fdcb4dc5` |
| `bench.ns` | `76d29c92dd8e5e5e` |
| `bench.lua` | `7fdc9d39636cacad` |
| `bench.cpp` | `fbdb520916b45861` |

## Medians (ms)

| Case | NeoCore | NeoCore `0bd42bd` | NeoScript | Lua | C++ | Checksum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 149 | 148 | 175 | 194 | 12.8 | 12497500 |
| float_math | 152 | 157 | 176 | 313 | 58.8 | 21666 |
| func_call | 112 | 117 | 133 | 158 | 4.3 | 12502500 |
| fib_recursive | 78 | 84 | 86 | 90 | 7.0 | 2178309 |
| array_rw | 42 | 43 | 44 | 47 | 2.5 | 25493856 |
| map_str | 44 | 50 | 37 | 40 | 62.9 | 90000 |
| string_ops | 47 | 47 | 50 | 146 | 13.8 | 438890 |
| particles | 45 | 47 | 48 | 49 | 3.4 | 10461222 |
| Sum of case medians | 669 | 693 | 749 | 1037 | 165.6 | |

## Per-round range (ms)

| Case | NeoCore | NeoCore `0bd42bd` | NeoScript | Lua | C++ |
| --- | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 144-154 | 145-150 | 173-182 | 193-196 | 12.2-13.1 |
| float_math | 150-156 | 156-158 | 175-186 | 303-321 | 57.7-59.3 |
| func_call | 109-116 | 114-120 | 131-137 | 153-162 | 4.3-4.5 |
| fib_recursive | 77-81 | 82-85 | 83-89 | 86-92 | 6.8-7.2 |
| array_rw | 41-43 | 41-43 | 41-44 | 45-49 | 2.5-2.6 |
| map_str | 43-45 | 49-51 | 36-38 | 36-42 | 62.2-64.5 |
| string_ops | 47-48 | 45-48 | 49-52 | 143-152 | 13.4-14.2 |
| particles | 44-47 | 46-50 | 46-49 | 49-51 | 3.4-4.2 |

## Same-position comparison

Earlier in the same evening, `0bd42bd` and the measured tree were built with 0, 16,
32 and 48 bytes of `nop` at the top of `RunInternal<false,false>`, which shifts the
whole dispatch loop without changing any logic, and the eight builds ran alternately
for three rounds (medians in ms):

| Dispatch offset | `0bd42bd` sum | Tree sum | `0bd42bd` `fib_recursive` | Tree `fib_recursive` | `0bd42bd` `func_call` | Tree `func_call` | `0bd42bd` `particles` | Tree `particles` |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 655 | 647 | 80 | 78 | 113 | 110 | 43 | 40 |
| 16 | 657 | 639 | 81 | 77 | 111 | 109 | 43 | 39 |
| 32 | 669 | 655 | 80 | 77 | 113 | 110 | 46 | 44 |
| 48 | 675 | 667 | 81 | 83 | 114 | 116 | 45 | 43 |

A loop that only rotates four local `int` variables (four payload copies and one
`JFOR` per iteration) took 47-48 versus 35-38 tenths of a nanosecond per iteration
without and with the local copy opcodes, at offsets 0 and 32. Local payload copies
are the most frequent instruction in a large game script compiled for inspection
(about 13% of its instructions), but the eight benchmark loops contain few of them.

## Observations

- Against `0bd42bd` the tree is faster at every dispatch offset; the sum falls by
  1-3%, mostly from `fib_recursive` and `func_call` (integer immediates in the
  argument and comparison arithmetic) and from `particles` and `float_math`
  (local-operand paths for list element access and conversions).
- The headline table puts the tree at offset 32 and `0bd42bd` at offset 48. Its
  `map_str` difference (44 against 50 ms) follows the offset rather than the source;
  the same-position runs moved `map_str` by at most 2 ms between the two sources.
- Against NeoScript the tree is faster in seven cases and slower in `map_str`; the
  sum of case medians is 0.89x NeoScript and 0.65x Lua.

## What this comparison cannot isolate

- Code placement: shifting only the dispatch loop has moved single cases by up to
  about 25%, so differences between NeoCore builds are attributed to source changes
  only when the ranges separate at equal dispatch offsets.
- The micro-architectural cause of the placement effect was not identified; hardware
  counters were not available in this session.
- Results from other sessions are not comparable with these numbers; earlier
  snapshots of this file are in the repository history.
- The C++ reference is a ceiling, not a peer; its `map_str` case uses
  `std::unordered_map<std::string, int>` and is slower than the scripts, which hash
  interned keys once.
- Lua and NeoScript are separate binaries with their own build settings.
