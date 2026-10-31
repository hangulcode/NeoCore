# Typed JSON conversion

NeoCore JSON conversion requires an explicit target type. It does not introduce
heterogeneous values or a dynamic `system.load` path into `.nc` source.

C++ hosts use `Program::ParseJson(typeName, json, error, strictIntegers)` and
`Program::ToJson(value, error, sortKeys)`. The C ABI exposes `NcParseJson` and
`NcToJson`; the managed Program wrapper provides corresponding operations.
The target type must exist in the compiled program's type registry.

| NeoCore type | JSON representation |
| --- | --- |
| int / float / bool / string | Corresponding scalar |
| nullable T | null or the T representation |
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
not JSON data. Nonfinite scalar/vector output is rejected. Nesting is bounded;
cyclic collections cannot be serialized. Set decoding applies set uniqueness.
Copying or encoding a collection does not change its declared element type.

Always check `Error` or the returned C/managed result. A failed conversion is not
an empty successful object. Runtime-owned values remain tied to their program's
registry and memory domain; pass data between independent programs through JSON
or an explicit host copy, not by moving a live value handle.
