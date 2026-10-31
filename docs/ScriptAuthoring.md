# Writing NeoCore scripts

NeoCore source uses `.nc` and explicit static types. The compiler rejects unknown
names, incompatible assignments, invalid calls, and unsupported syntax before VM
execution. See [StaticTyping.md](StaticTyping.md) for the detailed language rules
and [API.md](API.md) for the current library surface.

## Declarations and functions

```cpp
import math;

int count = 0;
float scale = 1.5;
string label = "particles";
bool enabled = true;

int Add(int a, int b) { return a + b; }
void Update(float dt) { count += 1; }
export int ReadCount() { return count; }
```

`int` is signed 64-bit; `float` is binary64. Vector components use float32.
`var`, `fun`, pointers, `T&`, and user-declared `int32`/`float32` scalars are not
part of NeoCore. Functions always declare their result type, including `void`.
Variables must be declared before use; functions can call later functions and
recurse. Numeric, bool and vector variables have defined initial values.
Nonnullable strings, collections, functions and native objects need initializers.

`public` exposes a file declaration to another module. `export` exposes supported
functions or variables to the host. Neither keyword implies the other.
`import`, `const`, `public` and `export` are file-scope constructs.

## Values and collections

```cpp
struct Position { float x; float y; };
Position point = Position(1.0, 2.0);
Position copy = point;
copy.x = 3.0;

list<int> values = [1, 2, 3];
array<float> samples = array<float>(64);
map<string, int> scores = {"alpha": 10};
int score = scores.get("missing", 0);

for (int i in 0, values.len()) values[i] += 1;
foreach (int value in values) print(value);
```

Structs and vectors copy by value. Collections share storage: assigning a list
or map does not deep-copy it. Element/key/value types are fixed; heterogeneous
collections do not gain an implicit dynamic fallback. Map reads that can miss
are nullable; use a typed fallback or supported null handling. Removal is explicit.
An ordinary `array<float>` stores binary64 and `array<int>` stores int64.
Compact storage remains a separate array type; it does not introduce public
32-bit scalar declarations.

Conditions require bools. Strings concatenate with `..`; use `tostring` for
explicit conversion. Numeric conversions, division and shift counts keep their
documented runtime boundary behavior even after static checking.

## Functions as values and execution control

```cpp
fn(int)->int twice = fn(int value)->int { return value * 2; };
int result = twice(4);
```

Function types include every parameter and the return type. Closures capture
values. Retained host callbacks use the same checked signatures; instance-owned
closures cannot execute in another instance or after their owner is reset.

`sleep(milliseconds)` and `yield;` use the existing VM execution machinery.
Hosts resume suspended instances through `Instance::Resume`. Script coroutines
use signature-bound `coroutine<fn(...)->void>` handles after `import coroutine;`.
See [the converted coroutine sample](../Samples/neocore/scripts/coroutine.nc).

## Modules and host objects

A module import loads another `.nc` file and checks it with the same compiler.
Only public declarations are accessible through its alias. Import cycles and
untyped source are rejected. A host module loader supplies canonical source names
and contents; diagnostics and debugger locations retain the original module.

Host object types, properties and methods come from a `HostSchema`. The compiler
resolves their names and signatures. Runtime object IDs still pass through the
host resolver on use; compilation cannot guarantee that a game object remains
alive or retains a capability such as a UI component. See
[HostBindings.md](HostBindings.md).

## Math and typed JSON

Import `math` for scalar functions, interpolation, colors, random numbers,
vectors and Quaternion helpers. Arguments and results have fixed signatures;
see [the API reference](API.md#mathematics-and-vectors) and
[math.nc](../Samples/neocore/scripts/math.nc).

Import `system` to encode a checked value with `system.jsonEncode(value)` or
decode text with `system.jsonDecode<T>(text)`. For example,
`list<int> values = system.jsonDecode<list<int>>("[1,2,3]");`.
Structs use named JSON object fields. Invalid text or a schema mismatch is a
runtime error; missing and extra struct fields are rejected. See [JSON.md](JSON.md).

## Run and debug

```sh
neocore --file Samples/neocore/scripts/hello.nc
neocore --debug --asm --file Samples/neocore/scripts/hello.nc
neocore --smoke
neocore --debug-smoke
neocore --compiler-error-regression
```

The console without arguments presents the sample menu. `--asm` prints bytecode;
`--debug` includes runtime source information. The
[VS Code extension](../Tools/vscode-neocore/README.md) uses the typed `--lsp` and
`--dap` modes for diagnostics, completion, signatures, hover and debugging.
[SampleMigration.md](SampleMigration.md) records the sample and command coverage.

Memory management, reference-counted containers and cycle collection remain VM
services. Static typing does not require manual allocation or deletion in scripts.
