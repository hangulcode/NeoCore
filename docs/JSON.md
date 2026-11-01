# Typed JSON conversion

NeoCore JSON decoding requires an explicit target type. Encoding uses the static
type of the source expression. Neither operation introduces heterogeneous values
or a dynamic `system.load` path into `.nc` source.

## Script API

Import `system` before calling either function:

```nc
import system;

struct SaveData
{
    int version;
    string name;
    list<int> inventory;
};

SaveData data = SaveData(1, "player", [7, 9]);
string text = system.jsonEncode(data, true);
SaveData restored = system.jsonDecode<SaveData>(text);
map<string, list<int>> groups =
    system.jsonDecode<map<string, list<int>>>("{\"coins\":[2,3,5]}");
```

- `system.jsonEncode(value, sortKeys = false)` returns a string. The compiler
  resolves the complete input type, including all struct fields and collection
  element types. `sortKeys` is a bool; true sorts JSON object keys.
- `system.jsonDecode<T>(text)` returns exactly `T`. Its string argument is checked
  at compile time; the JSON contents are checked at runtime. `T` can be a nested
  collection, a declared struct or another supported JSON type.
  The target cannot be omitted or supplied as a runtime string.
- Malformed JSON, a mismatched field or element, an invalid numeric value, and
  excessive nesting cause a script runtime error. Decoding does not return a
  partly constructed value or silently substitute an empty object.
- `system.jsonDecodeOr<T>(text, fallback)` returns `fallback` instead of failing and
  records the reason; `system.jsonError()` returns that reason, or an empty string
  after a successful decode. The reason starts with the JSON path of the problem,
  such as `$.gold: missing field`, `$.inventory.items[1]: expected int` or
  `$.legacyScore: unknown field`; parser failures report the byte position.
- `system.jsonDecodeLenient<T>(text)` and `system.jsonDecodeLenientOr<T>(text, fallback)`
  fill a missing struct field with its declared default (see
  [struct field defaults](StaticTyping.md#struct-values)) or the type default, and
  ignore unknown fields. Type mismatches still fail. Reading only a version header
  from an old document is `system.jsonDecodeLenient<Header>(text)` with a struct that
  declares just that field.

```nc
Slot slot = system.jsonDecodeOr<Slot>(text, Slot());
if (system.jsonError() != "")
{
    Log("load failed: " .. system.jsonError());
    return;
}
```

`jsonDecode<T>` is a built-in typed operation, not general user-defined generic
function support. The [console sample](../Samples/neocore/scripts/json.nc) shows
struct, vector, nested collection and null reference round trips.

## How decoding works

### The four decode functions

| Function | Missing struct field | Unknown struct field | Type mismatch or malformed text | On failure |
| --- | --- | --- | --- | --- |
| `jsonDecode<T>(text)` | error | error | error | script runtime error |
| `jsonDecodeOr<T>(text, fallback)` | error | error | error | returns `fallback`, sets `jsonError()` |
| `jsonDecodeLenient<T>(text)` | default value | ignored | error | script runtime error |
| `jsonDecodeLenientOr<T>(text, fallback)` | default value | ignored | error | returns `fallback`, sets `jsonError()` |

All four share one decoder. The frontend checks the target type `T`, the string
argument and, for the `Or` forms, that `fallback` converts to `T`; a list or map
literal written as the fallback (`[]`, `{}`) is typed by `T`. The call is lowered to
the same internal native with a mode value, so adding a mode does not add bytecode
instructions or a second decoder.

### Failure as a value

The `Or` forms never produce a partially filled value. Decoding builds the whole
result before anything is returned, so a failure anywhere in the document discards
everything that was built and returns the `fallback` argument unchanged. The
fallback is evaluated before the call like any other argument, even when decoding
succeeds; keep it cheap (`Slot()` or a shared empty collection) or compute it once.

`system.jsonError()` returns the reason recorded by the most recent `jsonDecodeOr`
or `jsonDecodeLenientOr` call in the same instance; a successful call clears it to
an empty string. The value is per instance, so two instances sharing a program do
not see each other's errors. The error-throwing forms also record the reason
before raising the runtime error, which is useful when the host inspects the
instance after a failed call.

### Error paths

Every decode failure starts with the JSON path of the offending value:

| Path form | Meaning |
| --- | --- |
| `$` | the whole document |
| `$.name` | struct field or object entry `name` |
| `$[3]` | array element at index 3 |
| `$.inventory.items[1]` | nested combination |

The part after the colon names the problem: `missing field`, `unknown field`,
`expected int`, `expected an array of 3 numbers for Vector3`, `null is not allowed
for int`, `vector component exceeds finite float32 range`, and so on. Malformed
text is reported by the parser before conversion starts, with the byte position of
the failure (`$.version: unexpected end of data at byte 16`). A struct with several
problems reports the first one in declaration order: missing fields first, then the
first unknown key.

### Strict and lenient struct decoding

For a struct target the decoder walks the declared fields in order:

1. The JSON value must be an object; otherwise `expected an object for struct T`.
2. For each declared field, look up the key. If it is present, decode it with the
   field's type and the path `parent.field`. If it is absent, strict mode fails with
   `path.field: missing field`; lenient mode substitutes the field's declared default
   (`int target = -1;`) or, when none is declared, the type default: `0`, `0.0`,
   `false`, a zero vector, the identity quaternion, or `null` for strings,
   collections, function values and host objects. Nested structs recurse, so a
   missing inner object becomes a struct built entirely from its own defaults.
3. After the fields, strict mode compares the object size with the field count and,
   when they differ, reports the first key that is not a declared field as
   `path.key: unknown field`. Lenient mode skips this step, which is what lets an
   old document carry fields that the struct no longer declares, and lets a small
   struct such as `struct Header { int version = 0; }` read only the version.

Lenient mode changes only these two rules. A present field with the wrong type, a
non-string object key, a vector with the wrong component count, an out-of-range
compact value or a `null` for a value type fails in both modes, because silently
substituting a default for a value that is present but wrong would hide corruption.
Lists, arrays, sets and maps decode identically in both modes; leniency applies
inside their struct elements and values.

### Where field defaults come from

A declared default is evaluated once when the struct is checked and stored as a
value in the type table next to the field's name and type. That value feeds three
consumers: the compiler-generated default helper used by `Slot s;` and `Slot()`,
the host-boundary default used by lenient decoding and by hosts that build default
struct values, and the saved image, whose type table carries the defaults so a
program loaded from an image decodes exactly like one compiled from source.
Because the stored value is the evaluated result, a default such as
`int mode = MODE_NORMAL * 2 + 1` or a module constant imported as `shape.LIMIT`
costs nothing at runtime and never depends on evaluation order.

### Host entry points

The C++ and C ABI entry points (`Program::ParseJson`, `HostCall::ReturnJson`,
`NcParseJson`) keep strict decoding; their callers receive the same path-prefixed
messages through `Error`. Lenient decoding and the recorded error are script-level
features of `system`.

## Host API

C++ hosts use `Program::ParseJson(typeName, json, error, strictIntegers)` and
`Program::ToJson(value, error, sortKeys)`. The C ABI exposes `NcParseJson` and
`NcToJson`; the managed Program wrapper provides corresponding operations.
The target type must exist in the compiled program's type registry.

A synchronous resource binding can return JSON through
`HostCall::ReturnJson(text, strictIntegers = true)`. Its target is the binding's
declared return type. It creates the result in the actual calling instance's
program, including calls during global initialization. It does not parse through
the program's default instance. Return its bool result from the binding callback:

```cpp
bindings.Bind(loadDocument, [](NeoCore::HostCall& call) {
    return call.ReturnJson(ReadDocument(call.String(0)));
});
```

Here `loadDocument` must already have a schema such as
`SaveData Resource.LoadDocument(string path)`, with `SaveData` registered as a host
struct. `ReadDocument` is supplied by the host. A conversion failure reports a host
call error. This helper does not turn one `LoadJson` method into a function that
returns arbitrary types based on the resource path; each binding still has a
fixed return contract.

## Representation and validation

| NeoCore type | JSON representation |
| --- | --- |
| int / float / bool / string | Corresponding scalar |
| string / collection (reference types) | null or the representation |
| list / array / compact array / set | Array of checked elements |
| map<string,T> | Object whose values have type T |
| struct | Object with the declared fields; strict decoding requires exactly those fields, lenient decoding fills missing fields and ignores extras |
| Vector2 / Vector3 / Vector4 / Quaternion | Array with exactly 2 / 3 / 4 / 4 numeric components |

Quaternion component order is `w,x,y,z`. Vector decoding checks float32 narrowing
and rejects nonfinite components. Integer values can widen to float; a fractional
JSON number does not silently become an int. Strict integer decoding rejects
values outside the script integer range. Map keys must be strings. Strict struct
input must contain every declared field and no extra field; the error names the
first missing or unknown field.

Functions, native object IDs, coroutine handles and builtin service handles are
not JSON data. Script calls reject unsupported types recursively during compilation,
including an empty collection of functions or a struct containing a non-string-key
map. Nonfinite scalar/vector output is rejected. Nesting is bounded;
cyclic collections cannot be serialized. Set decoding applies set uniqueness.
Copying or encoding a collection does not change its declared element type.

Always check `Error` or the returned C/managed result. A failed conversion is not
an empty successful object. Runtime-owned values remain tied to their program's
registry and memory domain; pass data between independent programs through JSON
or an explicit host copy, not by moving a live value handle.

For an old heterogeneous save object, declare a struct with the required fields
and use typed lists/maps for repeated data. JSON field names remain the declared
struct field names; the VM's positional struct storage is not the file format.
In strict decoding reference-typed fields must still be present in an object and
can contain JSON null; value-typed fields reject JSON null. Lenient decoding treats
an absent field as its default.
JSON exchange creates independent data and does not preserve live shared-state
identity between programs.
