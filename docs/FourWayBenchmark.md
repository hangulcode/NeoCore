# Four-way benchmark snapshot (2026-11-08)

This records the eight-case benchmark for the NeoCore tree that introduced the
two-pointer value view (`ValueRef` holding a payload pointer and a type-byte
pointer) on top of commit `f0935b6`, measured in one session against two older
NeoCore builds, the NeoScript console, Lua 5.5 and the C++ reference. All eight
checksums matched in every process of every round. The summary table in
[ReadMe.md](../ReadMe.md#performance-test-results) is derived from this record.

## Method

- One warmup round followed by five measured rounds. Each round runs the six
  executables serially in a rotated order so that no executable always follows
  the same predecessor; the runner is `Build/nc-perf-20261107/measure.py`
  (session label `view16b`), which stores every process's output.
- Each process reports the best of five in-process repetitions per case; the
  tables show the median of those per-round values and the per-round range.
- NeoCore runs without debug metadata (`neocore --file bench.nc`). NeoScript runs
  its own `bench.ns`; Lua runs `bench.lua`; the C++ executable is the same
  `bench_cpp.exe` as in the earlier three-way snapshots.
- Nothing else ran on the machine during the session; video playback and other
  builds were stopped first.

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

| Name | File | SHA-256 (prefix) |
| --- | --- | --- |
| NeoCore | `Build/neocore/bin/Release/neocore.exe` (working tree) | `62c1b020565acab3` |
| NeoCore `f0935b6` | worktree build of commit `f0935b6` | `f2ee2aec29ad4144` |
| NeoCore `d490647` | `Build/nc-perf-20261107/prev-noipo-build/bin/Release/neocore.exe` | `0ba3c9ab83f723f3` |
| NeoScript | `Samples/console/x64/Release/console.exe` (NeoScript repository) | `531b2a942dae97e9` |
| Lua | `lua55.exe` | `0d20768e1b2b438f` |
| C++ | `Build/nc-3way-benchmark-5/bench_cpp.exe` | `c39646891de34636` |

| Source | SHA-256 (prefix) |
| --- | --- |
| `bench.nc` | `5ff95969fdcb4dc5` |
| `bench.ns` | `76d29c92dd8e5e5e` |
| `bench.lua` | `7fdc9d39636cacad` |
| `bench.cpp` | `fbdb520916b45861` |

`d490647` is the last tagged-value build before the untagged eight-byte storage
migration; `f0935b6` is the commit immediately before the measured tree.

## Medians (ms)

| Case | NeoCore | NeoCore `f0935b6` | NeoCore `d490647` | NeoScript | Lua | C++ | Checksum |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 171 | 170 | 187 | 175 | 189 | 12.6 | 12497500 |
| float_math | 151 | 152 | 255 | 176 | 300 | 57.0 | 21666 |
| func_call | 122 | 122 | 150 | 130 | 154 | 4.4 | 12502500 |
| fib_recursive | 84 | 82 | 93 | 83 | 87 | 6.9 | 2178309 |
| array_rw | 43 | 46 | 52 | 41 | 46 | 2.5 | 25493856 |
| map_str | 39 | 43 | 34 | 37 | 41 | 61.7 | 90000 |
| string_ops | 46 | 48 | 43 | 49 | 142 | 13.5 | 438890 |
| particles | 45 | 44 | 49 | 46 | 48 | 3.4 | 10461222 |
| Sum of case medians | 701 | 707 | 863 | 737 | 1007 | 162.0 | |

## Per-round range (ms)

| Case | NeoCore | NeoCore `f0935b6` | NeoCore `d490647` | NeoScript | Lua | C++ |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| loop_sum | 169-178 | 167-178 | 186-199 | 171-185 | 187-193 | 12.3-12.8 |
| float_math | 148-152 | 151-156 | 249-270 | 171-187 | 294-303 | 56.3-59.1 |
| func_call | 119-127 | 119-123 | 149-156 | 127-138 | 151-161 | 4.2-4.4 |
| fib_recursive | 82-89 | 79-85 | 93-97 | 83-89 | 85-92 | 6.8-7.0 |
| array_rw | 42-45 | 43-47 | 50-54 | 40-43 | 45-46 | 2.4-2.6 |
| map_str | 38-41 | 41-44 | 32-35 | 35-39 | 39-43 | 60.8-63.3 |
| string_ops | 44-47 | 47-50 | 41-45 | 48-51 | 140-146 | 13.3-13.9 |
| particles | 44-46 | 43-48 | 48-56 | 45-48 | 48-49 | 3.4-3.5 |

## Observations

- Against NeoScript the measured tree is faster in five cases and inside one
  round's spread in `fib_recursive`, `array_rw` and `map_str`; the sum of case
  medians is 0.95x NeoScript and 0.70x Lua.
- Against the tagged-value build `d490647` every case except `map_str` and
  `string_ops` is faster, and the sum is 0.81x.
- Against `f0935b6` the view change itself is within noise on the numeric cases
  and moves `map_str` (43 -> 39) and `string_ops` (48 -> 46), the cases that
  build boundary views; a separate view-heavy micro benchmark (foreach over
  `list<string>` and `list<struct>`, struct field reads, `map<string,string>`
  reads) measured 0.93x, 0.91x, 1.05x and 0.72x the `f0935b6` times per iteration.

## What this comparison cannot isolate

- Build layout: builds of near-identical source have differed by several percent
  in single cases during this work, so single-case differences below about 5%
  between NeoCore builds are not attributed to source changes.
- The C++ reference is a ceiling, not a peer; its `map_str` case uses
  `std::unordered_map<std::string, int>` and is slower than the scripts, which hash
  interned keys once.
- Lua and NeoScript are separate binaries with their own build settings.
