# Runtime contracts after static checking

NeoCore checks signatures, operand types and conversions before executing bytecode.
Internal execution trusts those proofs. Debug C++ assertions diagnose compiler or
lowering defects; Release builds do not evaluate them. The console `--debug` option
adds script source information and does not enable C++ assertions in a Release binary.

## Rules for changes

- Replace a check with an assertion only when every supported caller supplies the
  same proven contract. Do not add a per-call flag to select a trusted path.
- Arity and non-nullable operand types belong to compile-time checking. Read the
  proven payload directly, preserving explicit numeric widening and narrowing.
- Nullable types still admit null at runtime. Assert the allowed representation
  set, then report an ordinary runtime error for a forbidden null value.
- Preserve checks on indices, sizes, divisors, integer overflow, compact integer
  storage, calendar ranges, callback/instance lifetime and execution state.
- Preserve validation of external C++/C ABI/Unity input, native return values, JSON
  data, images and object IDs. Shared internal/external entry points retain their
  boundary checks until the entry paths have separate proven contracts.
- Distinguish validation from representation selection. A type tag can still be
  needed for reference release, copy-on-write, nullable equality, numeric conversion
  or a generic instruction supporting several container layouts.
- Assertions must have no side effects. Do not put reads that produce output,
  reference-count changes, allocations or state transitions inside an assertion.

## Reviewed paths

| Path | Treatment |
| --- | --- |
| `NeoLib.cpp` string, collection, math, async and coroutine methods | Fixed arity and proven operand types use assertions. Nullable arguments and state/range failures remain runtime checks. |
| Builtin numeric reads | Float signatures consume binary64 payloads after compiler widening. Integer indices retain saturated int64-to-index narrowing. Vector storage retains float32 conversion. |
| Builtin vector reads | Fixed-dimension copies use the declared component count; they do not choose the copy size from a runtime tag. |
| `NeoTime.cpp` | Arity and scalar types use assertions. Null spans, date parsing, calendar limits and arithmetic overflow remain checked. |
| Hidden script-to-host wrapper | Its generated argument count is an assertion. Object resolution, snapshots, host failures and external values remain checked. |
| VM conditional jumps and async-wait resumption | Proven bool operands use assertions and direct bool payload reads. |
| VM scalar-native calls | Numeric membership is asserted. Integer/float representation selection remains part of the generic scalar instruction. |
| VM vector/array indices and property names | Proven index/name types use assertions. Actual index bounds and nullable receivers remain checked. |
| Typed map/set/array receivers | Assertions describe the declared kind; the executable branch checks null. Missing keys and compact-array range errors remain ordinary errors. |
| Foreach bookkeeping | Internal mutation-version slot types are asserted. Initialization sentinels and collection mutation detection remain executable. |
| Shared VM function/closure calls | Argument count, ownership, lifetime and stack limits remain checked because external invocation paths share these routines. |
| Generic comparisons, collection dispatch and variable storage | Representation selection, null semantics and reference cleanup remain executable. |
| JSON, regular expressions, image loading and host value boundaries | Data validation and resource limits remain executable. |

This does not complete the static instruction migration. Some shared instructions
still select a representation at runtime; removing that selection requires a
separately specified typed instruction or binding contract.

## Verification

Test invalid arity and types as compilation errors. Run accepted builtin signatures
and nullable/value-boundary failures in both Release and assertion-enabled Debug
builds. Keep source locations, observable side effects, reference ownership and
failure behavior intact. Run compiler, VM, host, image, coroutine and collection
regressions, plus non-Windows C++11 compilation. Timed benchmarks require the user's
permission and are separate from correctness verification.
