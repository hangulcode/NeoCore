# NeoCore API reference

This document describes the `.nc` frontend. The inherited VM may implement more
operations internally; those are not automatically NeoCore library APIs.
[NeoCoreApiTable.inc](../NeoSource/NeoCoreApiTable.inc) is the single declaration
table for every built-in API: one line per API, written as a signature in the
language's own syntax (`bool try_get(K key, out V value)`). The compiler checks
argument counts, types and `out` arguments from that table, the tooling lists it,
and the runtime body table is generated from the same lines. A method or function
that is not in the table (or in the host schema) is a compile-time error.
Language operations and constructors are checked by
[NeoCoreCompiler.cpp](../NeoSource/NeoCoreCompiler.cpp).

## Language intrinsics

`print`, `tostring`, `toint`, `tofloat`, `sleep` and `yield` are checked language
operations. Collection and string sizes use the `len()` method. There is no runtime
type query; types are known at compile time. Numeric conversions do not permit
arbitrary object or collection conversion. Use explicit typed declarations and `fn(...)->T`
function values. Conditions require bools.

## Strings

Strings support `len`, `sub(start, length)`, `codepoint(index)`, `find(text)`,
`upper`, `lower`, `trim`, `ltrim`, `rtrim`, `replace`, `replaceAll` and `split`.
`split(separator)` requires a nonempty separator and returns `list<string>`.

Regular expressions use `regex_match(pattern, ignoreCase?)`,
`regex_search(pattern, ignoreCase?)` and
`regex_replace(pattern, replacement, ignoreCase?)`. Search returns a
`list<string>` containing the match/captures, or null when nothing matched. PCRE2 enforces the backend's bounded
pattern, work and memory policies; errors remain runtime errors. This is PCRE2
syntax, not a promise of JavaScript RegExp compatibility. See
[regex.nc](../Samples/neocore/scripts/regex.nc) for checked examples.

## Mathematics and vectors

After `import math;`, the following names have checked signatures. Scalar numeric
inputs and results are `float` unless another type is shown; `int` arguments widen
normally. For example, `math.Clamp(4, 0, 3)` returns `float`, so assigning its result
to an `int` requires an explicit conversion. Names are case-sensitive.

| Functions | Arguments and result |
| --- | --- |
| `abs`, `acos`, `asin`, `atan`, `ceil`, `floor`, `round`, `sin`, `cos`, `tan`, `log`, `log10`, `exp`, `deg`, `rad`, `sqrt`, `trunc`, `fract`, `sinh`, `cosh`, `tanh`, `Sign`, `Clamp01`, `SmoothStep01`, `WrapAngle` | `(float) -> float` |
| `pow`, `atan2`, `hypot`, `fmod`, `Min`, `Max`, `Repeat`, `PingPong`, `DeltaAngle`, `RandRange` | `(float, float) -> float` |
| `Clamp`, `Lerp`, `LerpAngle`, `MoveToward`, `MoveTowardAngle`, `InverseLerp`, `SmoothStep` | `(float, float, float) -> float` |
| `Remap` | `(value, inMin, inMax, outMin, outMax) -> float` |
| `Distance2` | `(x0, y0, x1, y1) -> float` |
| `Dot3`, `Distance3`, `DistanceSquared3` | `(Vector3, Vector3) -> float` |
| `Length3` | `(Vector3) -> float` |
| `Normalize3`, `Cross3` | `(Vector3, Vector3) -> Vector3` |
| `Lerp3` | `(Vector3, Vector3, float) -> Vector3` |
| `RotateVectorByQuat` | `(Quaternion, Vector3) -> Vector3` |
| `quat_from_basis` | `(Vector3 right, Vector3 up, Vector3 forward) -> Quaternion` |
| `quat_slerp` | `(Quaternion, Quaternion, float) -> Quaternion` |
| `srand` / `rand` | `(int seed) -> void` / `() -> int` |
| `Rand01` | `() -> float` |
| `Hash32` | `(int value) -> int` |
| `tostr` | `(float value) -> string` |
| `fixed` | `(float value, int decimals) -> string` |
| `ColorRGB` | `(float r, float g, float b) -> int` |
| `ColorARGB` | `(float a, float r, float g, float b) -> int` |

The established argument order and behavior are retained. `atan2` takes `(y, x)`;
angle operations use radians, `deg` converts radians to degrees, and `rad` does the
reverse. `Clamp` takes `(value, min, max)`, `Lerp` takes `(a, b, t)`, `InverseLerp`
takes `(a, b, value)`, and `SmoothStep` takes `(edge0, edge1, value)`. `Lerp` and
`InverseLerp` do not clamp their results; `quat_slerp` clamps `t` to `[0,1]`.
`Normalize3(value, fallback)` returns the fallback for a near-zero input;
`quat_from_basis` requires nonzero basis vectors. Vector helpers retain float32
component calculations and value semantics. `fmod(x, 0)` returns zero.

`srand` seeds the current execution worker; its seed is clamped to the signed
32-bit range. `rand` returns an integer from 0 through 32767, `Rand01` includes both
0 and 1, and `RandRange(min, max)` can return either endpoint. `Hash32` hashes the
low 32 bits and returns the signed 32-bit result widened to `int`.

Color channels are normalized values from 0 to 1, not integer bytes. They clamp
to that interval, treat NaN as zero, and truncate after multiplying by 255. The
packed `0xAABBGGRR` result is signed 32-bit widened to `int`; `ColorRGB` uses alpha
255. `tostr` preserves 17 significant digits for binary64 round trips and is not
interchangeable with the display-oriented `tostring` conversion.

`math.fixed(value, decimals)` formats the actual binary64 value with exactly
`decimals` fractional digits, including trailing zeros, and never uses exponent
notation. `decimals` must be in `[0, 9]`; out-of-range values, including constants,
raise a runtime error. The compiler checks argument types and count. Integer
arguments widen to binary64; vector components widen from their stored float32 value.
Rounding is to nearest, with exact ties going to the even final digit:
`fixed(0.25, 1)` is `"0.2"`, `fixed(2.5, 0)` is `"2"`, and `fixed(3.5, 0)` is `"4"`.
Because binary64 `0.15` is slightly below the midpoint, `fixed(0.15, 1)` is `"0.1"`.
The decimal separator is always `.`, independent of the locale and thread rounding
mode. Results that round to zero have no minus sign; `decimals == 0` omits the dot.
NaN and infinity retain the current `tostring` spelling. Conversion uses fixed-size
stack storage and allocates no temporary heap buffer; the runtime owns the final
string. This does not change `tostring` or `math.tostr`.

`Vector2`, `Vector3`, `Vector4` and `Quaternion` are explicit value types.
Vector constructors accept either no arguments or their exact component count.
The `math.Vector2`, `math.Vector3`, `math.Vector4` and `math.Quaternion` aliases
require the full component count and use the same typed construction instructions.
Vector components use `x,y,z,w`; quaternion components and constructors use
`w,x,y,z`. Scalar component reads produce binary64 values; writes narrow to the
defined float32 representation. Quaternion defaults are the identity rotation.
See [vectors.nc](../Samples/neocore/scripts/vectors.nc) and the checked library
examples in [math.nc](../Samples/neocore/scripts/math.nc).

## Collections

`list<T>`, `array<T>`, `array32<T>`, `map<K,V>` and `set<T>` have fixed element types.
Their supported operations include size/membership queries, typed mutation,
removal, copies and iteration. Lists provide append/insert/pop/remove/remove_swap,
resize/reserve and sorting where supported by their comparator contract.
Maps provide `contains(key)`, `try_get(key, out value)`, `remove`, `clear` and typed keys/
values. Reading `map[key]` always fails if the key is absent; a present null value
remains valid for reference-valued maps. `map.get` and its fallback were removed.
Sets provide membership changes and union/intersection/difference.
Set `contains`, `add` and `remove` return bool: membership,
new insertion and successful removal respectively. Duplicate insertion and removal
of an absent element return false. String null insertion fails; null membership
and removal queries return false. A null set receiver fails for all three methods.
Map keys and set elements must be `int` or `string`; `array` and `array32` keep
their fixed element rules, and `list<T>` accepts any element type.
The compiler checks operation names, argument counts and element types from the
built-in API table. These methods are ordinary API calls, not dedicated VM
instructions; only element access (`m[key]`, `a[i]`) and construction have instructions.

`map.try_get(key, out value)` performs one lookup and returns `bool`. On success it
assigns the stored value to `value`; on a missing key it returns `false` and leaves
`value` unchanged. A present null value still returns `true`. The receiver and key
are evaluated once, from left to right. A null map is a runtime error; a null string
query key is a miss. Structs and vectors are copied by value; collections keep their
shared identity.

```cpp
int value = 0;
if (counts.try_get("alpha", out value)) print(value);
```

### `out` arguments

`out` marks an argument that an API writes back to. It is a property of a registered
API parameter (`out V value` in the declaration table, or a host method registered
with parameter modes), so any built-in or host API can declare one. The argument must be `out` followed by an already-declared writable
local, parameter or script global whose type is exactly the parameter type; fields,
collection elements and host properties are not accepted. The call copies the
variable's current value in and copies the result back afterwards, so an API that
does not write the argument leaves the variable unchanged. No reference or alias is
created.

**User-defined functions, function types, lambdas and their calls cannot use `out`;
these are compile-time errors**, as is `out` on a parameter that is not declared
`out`, or a missing `out` where one is declared. `out` is a contextual keyword: it
remains a legal variable name but cannot name a type.

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
| `date(format, seconds)` | Device-local formatted string, including DST at that timestamp |
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

`date` and `utc_date` take Unix seconds, not milliseconds. `date` follows the
host's configured local timezone; it does not change process locale or timezone.
Local timestamp range depends on the platform CRT (Windows: 1970 through 3000;
POSIX: representable `time_t` within years 1900 through 9999). Unsupported
timestamps and invalid formats raise runtime errors. Both formatters limit output
to 79 UTF-8 bytes. Use `utc_date` for portable UTC formatting, including pre-epoch
timestamps on Windows.

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
- [JSON.md](JSON.md): typed script JSON conversion and host return bindings.
- [Unity.md](Unity.md): typed C ABI and managed Unity integration.
- [NeoCoreC.h](../Unity/NeoCore/NeoCoreC.h): exact C ABI declarations and ownership contracts.
- [VS Code](../Tools/vscode-neocore/README.md): `.nc` language service and debugger.

The complete accepted language contract remains in [StaticTyping.md](StaticTyping.md).
