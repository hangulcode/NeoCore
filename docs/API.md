# NeoCore API reference

This document describes the `.nc` frontend. The inherited VM may implement more
operations internally; those are not automatically NeoCore library APIs.
[NeoCoreBuiltins.h](../NeoSource/NeoCoreBuiltins.h) contains the checked library
signature table; collection and language intrinsics are checked by
[NeoCoreCompiler.cpp](../NeoSource/NeoCoreCompiler.cpp).

## Language intrinsics

`print`, `tostring`, `toint`, `tofloat`, `tosize`, `type`, `sleep` and `yield` are
checked language operations. Numeric conversions do not permit arbitrary object
or collection conversion. Use explicit typed declarations and `fn(...)->T`
function values. Conditions require bools.

## Strings

Strings support `len`, `sub(start, length)`, `codepoint(index)`, `find(text)`,
`upper`, `lower`, `trim`, `ltrim`, `rtrim`, `replace`, `replaceAll` and `split`.
`split(separator)` requires a nonempty separator and returns `list<string>`.

Regular expressions use `regex_match(pattern, ignoreCase?)`,
`regex_search(pattern, ignoreCase?)` and
`regex_replace(pattern, replacement, ignoreCase?)`. Search returns a nullable
`list<string>` containing the match/captures. PCRE2 enforces the backend's bounded
pattern, work and memory policies; errors remain runtime errors. This is PCRE2
syntax, not a promise of JavaScript RegExp compatibility. See
[regex.nc](../Samples/neocore/scripts/regex.nc) for checked examples.

## Mathematics and vectors

After `import math;`, the current scalar table exposes `sin`, `cos`, `sqrt`, `abs`,
`floor`, `ceil` and `pow`. Their scalar input/output type is `float`, with ordinary
int-to-float widening. Do not infer availability of other functions from a
NeoScript example.

`Vector2`, `Vector3`, `Vector4` and `Quaternion` are explicit value types.
Vector constructors accept either no arguments or their exact component count.
Vector components use `x,y,z,w`; quaternion components and constructors use
`w,x,y,z`. Scalar component reads produce binary64 values; writes narrow to the
defined float32 representation. Quaternion defaults are the identity rotation.
See [vectors.nc](../Samples/neocore/scripts/vectors.nc).

## Collections

`list<T>`, `array<T>`, `array32<T>`, `map<K,V>` and `set<T>` have fixed element types.
Their supported operations include size/membership queries, typed mutation,
removal, copies and iteration. Lists provide append/insert/pop/remove/remove_swap,
resize/reserve and sorting where supported by their comparator contract.
Maps provide `contains`, `get(key, fallback)`, `remove`, `clear` and typed keys/
values. Sets provide membership changes and union/intersection/difference.
The compiler checks operation names, argument counts and element types.

`array32<int>(count, fill)` and `array32<float>(count, fill)` use four-byte elements;
size/fill arguments are optional. Other element types are rejected. Scalar reads
remain int64/binary64. Int32 overflow is a runtime error before the store; float32
storage rounds through the shared narrowing rule. Compound assignment and prefix
expressions return the stored value, while postfix expressions return the old value.

Collections share identity. Struct and vector values stored in them retain value
semantics. Size/indices are validated before narrowing to storage indices;
allocation can still fail. A reserve request is not a guarantee of available RAM.
Examples: [list.nc](../Samples/neocore/scripts/list.nc),
[map.nc](../Samples/neocore/scripts/map.nc),
[contailer.nc](../Samples/neocore/scripts/contailer.nc).

## UTC, durations and elapsed time

After `import system;`:

| API | Result |
| --- | --- |
| `time()` / `time_ms()` | Unix seconds / milliseconds as int64 |
| `utc_date(format, seconds)` | UTC formatted string |
| `utc_time_ms(year, month, day, hour, minute, second, millisecond)` | UTC Unix milliseconds |
| `utc_parts(milliseconds)` | `map<string,int>` components |
| `utc_format_ms(milliseconds)` / `utc_parse_ms(text)` | UTC text round trip |
| `time_span(days, hours, minutes, seconds, milliseconds)` | `TimeSpan` |
| `time_span_ms(value)` / `time_span_seconds(value)` | `TimeSpan` |
| `stopwatch()` | `Stopwatch` |
| `clock()` | Existing clock value as float |

`TimeSpan` exposes `total_milliseconds`, `total_seconds`, `parts`, `format`, `add`,
`subtract` and `compare`. `Stopwatch` exposes `elapsed`, `elapsed_ms`, `elapsed_us`,
`elapsed_seconds`, `start`, `stop`, `reset`, `restart` and `is_running`.
See [time.nc](../Samples/neocore/scripts/time.nc) for complete units and usage.

## HTTP and asynchronous requests

`system.async_create()` creates an `AsyncRequest`. It exposes
`get(timeoutMs, url, callback)`, `post(timeoutMs, url, body, callback)`,
`add_header(name, value)`, `wait()` and `close()`.
The callback type is `fn(bool, string)->void`. The host installs the transport;
the core contains no HTTP implementation. The native console optionally links
libcurl for HTTPS, and the managed host can install its HTTP transport.
See [HTTP sample coverage](SampleMigration.md#http-and-https).

## Host APIs, JSON, images and tools

- [Embedding.md](Embedding.md): C++ program/instance ownership, calls and images.
- [HostBindings.md](HostBindings.md): static host schemas, object IDs and adapters.
- [JSON.md](JSON.md): explicit target types at the host JSON boundary.
- [Unity.md](Unity.md): typed C ABI and managed Unity integration.
- [NeoCoreC.h](../Unity/NeoCore/NeoCoreC.h): exact C ABI declarations and ownership contracts.
- [VS Code](../Tools/vscode-neocore/README.md): `.nc` language service and debugger.

The complete accepted language contract remains in [StaticTyping.md](StaticTyping.md).
