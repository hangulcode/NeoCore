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

`jsonDecode<T>` is a built-in typed operation, not general user-defined generic
function support. The [console sample](../Samples/neocore/scripts/json.nc) shows
struct, vector, nested collection and null reference round trips.

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
| struct | Object with exactly the declared fields |
| Vector2 / Vector3 / Vector4 / Quaternion | Array with exactly 2 / 3 / 4 / 4 numeric components |

Quaternion component order is `w,x,y,z`. Vector decoding checks float32 narrowing
and rejects nonfinite components. Integer values can widen to float; a fractional
JSON number does not silently become an int. Strict integer decoding rejects
values outside the script integer range. Map keys must be strings. Struct input
must contain every declared field and no extra field.

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
Reference-typed fields must still be present in an object and can contain JSON null;
value-typed fields reject JSON null.
JSON exchange creates independent data and does not preserve live shared-state
identity between programs.
