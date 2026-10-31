# NeoCore static language

NeoCore is an independent, statically typed language derived from NeoScript.
This document separates the agreed language design from the initial implementation.
The inherited [script guide](ScriptAuthoring.md), [API reference](API.md), and
[embedding guide](Embedding.md) describe the NeoScript backend until explicitly migrated.
They are not evidence that a feature is accepted by the NeoCore frontend.

**`.nc` is the canonical NeoCore source extension.** Existing `.ns` files remain
legacy NeoScript backend fixtures. Typed editor support is a separate milestone;
the inherited `.ns` parser/LSP is not a NeoCore type checker.

## Implementation boundary

The frontend tokenizes source, builds typed expressions and statements,
collects function signatures, and checks types before producing numeric IR and a
transitional backend statement skeleton. The checked program then runs on the
existing bytecode VM. This is not a dynamic compatibility mode for NeoCore source.
Unsupported syntax and unknown types are compilation errors; they do not fall back
to untyped compilation. Generated backend source is an implementation artifact.

The typed execution path preserves the VM's instruction dispatch, call frames,
and value storage. Checked integer and floating-point `+`, `-`, `*`, and `/` expressions
select eight dedicated numeric instructions. Integer-to-float widening and explicit
float-to-integer conversion select two more instructions when the input type is known.
Mixed arithmetic explicitly converts the integer operand before the floating-point
operation. Integer overflow/division rules and floating-point behavior remain unchanged.
Existing literal folding can eliminate an arithmetic or conversion instruction entirely.

Numeric comparisons add six value instructions: integer and floating-point forms of
equality, less-than, and less-than-or-equal. They cover `==`, `!=`, `<`, `<=`, `>`, and
`>=` through operand swapping and result inversion. Both operands are evaluated left
to right before any slot swap, so `>` and `>=` do not reverse side effects. Six matching
direct-branch forms avoid producing a temporary bool when a condition consumes the
comparison. Integer remainder, bitwise AND/OR/XOR, and left/right shifts add six
instructions, preserving checked divisor and shift-count rules. There are 28
numeric typed instructions in total.

An int/int comparison remains an exact signed 64-bit comparison. A mixed int/float
comparison converts the integer to binary64, matching the existing generic comparison
rule; integers above 2^53 can round before comparison. This is not a new exact
integer-versus-float comparison policy. Bool, string, and nullable comparisons continue
to use their existing generic instructions.

Negation inverts the comparison result rather than replacing an ordered comparison
with its apparent opposite. Thus `!(nan < x)` is true while `nan >= x` is false.
Ordinary supported `if` range conditions retain the inherited single range instruction.
Source-negated ordered range conditions remain two typed comparison branches so that
the NaN behavior of each original comparison is preserved.
Compound `while` conditions retain the inherited multi-instruction path; a single
numeric comparison in `while` uses a direct typed branch.

Numeric expressions now reach the bytecode emitter as `TypedIR` in `NeoCoreIR.h`.
Nodes retain the semantic operation, checked type ID, numeric storage type, literal
value or resolved symbol identity, ordered operand indices, and original source span.
Mixed arithmetic has explicit conversion nodes. A collision-free marker in the
statement skeleton identifies an IR root; it contains no numeric operand or operator
text. The emitter validates the graph and emits instructions directly from its nodes.
The compile descriptor borrows the IR only until compilation returns; compiled
programs and saved images do not retain pointers to the graph.

Calls, assignments, and selectors that have not migrated to IR use typed
`LegacyExpression` leaves. Each leaf is compiled when traversal reaches its original
evaluation position, with the current lexical scope and original source coordinates.
For example, `(1 / 0) + F()` must fail before `F()` executes. Earlier operands are
snapshotted before later effects, including assignments hidden inside an IR marker.
Logical short-circuiting remains in the existing statement/expression compiler.
Unary negation, bitwise negation, and logical inversion also have semantic IR nodes;
some still use existing general-purpose VM instructions after compilation.

Arrays, vectors, and generated helpers retain private compile-only intrinsics in
the textual skeleton. Their metadata carries types and source lines, and each
intrinsic emits instructions without a runtime helper call frame. Declarations,
function layout, control flow, and these remaining paths have not yet moved to a
whole-program IR emitter.

Arrays and vectors add 51 dedicated instructions, described in the same central
metadata header as the numeric instructions. Vector2/3/4 construction, arithmetic,
component reads and functional component replacement use known component layouts.
Array construction, reads, writes and append select bool, int64, binary64 or a
specific vector storage layout at compile time. Length, resize and reserve have
shared array instructions; internal reference-cell allocation/reads/writes preserve owned values.
The handlers retain bounds, capacity, allocation and destination-reference cleanup,
but do not rediscover the input element type during each operation.

Array and vector writes are expanded into instruction sequences during compilation.
They evaluate the RHS, then capture the target and index once, read the old value
when required, calculate, and store. Component expressions return the float32 value
actually stored; postfix expressions preserve the old value. These operations do
not create script helper frames or invoke native library constructors. Three/four
input operations use a validated contiguous temporary-slot range. Each instruction
still occupies eight bytes; there are no extension slots or hidden resume states.

Constant initializers retain the existing constant evaluator. Ordinary scalar
compound assignments, user/host calls, lists/maps, struct helpers and host property
write composition retain their existing paths. Host vector calculations use the
new instructions inside the write helper, while getter/setter calls independently
revalidate the saved receiver ID. Bool/string conversions remain checked generic
conversions. Numeric expressions now emit from structured IR; the remaining
compile-only intrinsics and compatibility leaves are explicit migration boundaries,
not a complete typed-AST-to-bytecode backend replacement.
Bytecode images use version `0147` and must be regenerated. The unused
`NOP_AND3`, `NOP_OR3`, and `NOP_VERIFY_TYPE` instructions and their dedicated
handlers have been removed; active bitwise operators and range-loop conversions
retain their existing behavior. Earlier images are rejected because opcode numbers
changed. A generic lowering
mode remains available for differential tests.

The current frontend covers scalar and vector declarations, nominal structs and their fields,
typed arrays and scalar lists/maps, named functions, forward and recursive calls, mutable references
to named variables and struct fields, block scopes, conditions, loops, returns, and
scalar intrinsics. The limited `math` and `system`
imports expose only `math.sin`, `math.cos`, and `system.clock` in this milestone.
Explicit host schemas additionally expose nominal native objects, host globals,
methods, and read/write properties with scalar/vector/native signatures. Members and
argument types are resolved before backend compilation; see [HostBindings.md](HostBindings.md).
Nullable scalar/string types can be declared and compared with null, but implicit
nullable unwrapping is not implemented. For example, an `int?` cannot be added to
an `int`, or passed to an `int` parameter, merely because it was compared with null.

Typed sets, nested collections, imported script modules, richer host
signatures, closures, JSON, and coroutine integration are later milestones. Some types already
have compiler-side descriptors; that alone does not make their syntax executable.
The existing Unity package, LSP/DAP adapters, and legacy console have not been
converted into NeoCore entry points.

Frontend diagnostics identify the original source line and column in every mode.
The CLI omits debug metadata by default; use `neocore --debug --file source.nc` for
runtime source-line information. Transitional lowering preserves source line counts;
IR nodes retain original operator locations; rewritten declarations and compatibility
expressions can still change columns in diagnostics produced by the backend.
Timed benchmarks run without `--debug`.
The inherited raw bytecode `LoadProgram` API does not restore the source filename
registry; saved debug images retain instruction lines and function frames, while
source compilation also supplies filenames. The dedicated op path preserves this boundary.

## Declarations and functions

Every variable, parameter, and function return type is explicit. There is no `var`
declaration, no `fun` declaration keyword, and no inferred function return type.

```cpp
int count = 0;
float speed = 1.5;
bool enabled = true;
string label = "player";

int Fib(int n)
{
    if (n < 2)
        return n;
    return Fib(n - 1) + Fib(n - 2);
}

void Reset()
{
    count = 0;
}

print(Fib(12));
```

Function signatures are collected before bodies are checked. A function may call
a later function or itself, including mutual recursion. Variables must still be
declared before use; this also applies to a function's access to file variables.
There is no function overloading in the initial language. Function prototypes are
not a substitute for a body.

Top-level calls also require every file variable accessed by the called function
to have completed initialization at the call site. The compiler follows transitive
calls, including recursive cycles, and treats both reads and writes as accesses.
This analysis is conservative: an access inside a branch still counts even when
a particular run would not take that branch. For example, `int first = ReadLater();`
before `int later = 1;` is rejected when the later definition of `ReadLater` reads
`later`. Moving the call after `later` is initialized makes that ordering valid.

A `void` function can use `return;` or reach its closing brace. It cannot return a
value, and a void call cannot be used as a value. A non-void function must return a
compatible value on every path that can reach the end. Return analysis is
conservative: an unproven path is not treated as returning.

File declarations retain the separate visibility roles: `public` exposes a symbol
to script modules; `export` exposes it to the host. One does not imply the other.
Host access and module access must eventually check the declared signatures at
their boundaries, not merely rely on the source declaration.

`const`, `public`, and `export` declarations are file-scoped. A constant needs an
initializer. Scalar literals, earlier constants, numeric arithmetic, and integer
bit operations are supported. Logical operators (including `!`), comparisons, and
string concatenation in constant initializers are rejected until the typed
constant evaluator is implemented. The same operators remain available in ordinary
runtime expressions. `export const` is rejected; use `public const` for a module constant.

## Mutable references

The reference form is a mutable, non-rebindable alias to existing storage:

```cpp
void Adjust(float& value, int& count)
{
    value += 0.5;
    count += 1;
}

float value = 1.0;
int count = 0;
float& alias = value;
alias = 3.0;
Adjust(value, count);
```

`alias = 3.0` writes `value`; it does not make the alias point elsewhere. A reference
binds once at declaration or function invocation, must have an initializer, and
requires an exact-type assignable location. A conversion or temporary value cannot
bind to a reference parameter. For example, an `int` cannot bind to `float&` even
though an ordinary float parameter accepts an integer conversion.

The implementation supports named local/file variables, parameters, and their struct
fields as targets, including reference forwarding and multiple arguments referring to the
same variable. A reference to a by-value parameter changes that parameter's local
storage. A reference declaration cannot be `const`, `public`, or `export`.
Binding a reference to a range-loop iteration variable is explicitly unsupported.

The transitional backend represents address-taken scalar variables with internal
shared cells. A reference aliases that cell directly; it does not copy a value in
and write it back after the call. This preserves overlapping aliases and evaluation
order, but is not the final storage layout or a performance optimization. Ordinary
variables whose address is never taken keep their existing backend representation.

There are no pointers, reference rebinding operators, reference return values, or
escaping references. The scalar host-call API does not bind reference parameters;
these bindings currently happen in checked script code. Struct and scalar-field
references follow the same binding rules. Collection element references are rejected
because collection resizing can invalidate their locations.

## Scalar types and conversions

| Type | Meaning | Default without initializer |
| --- | --- | --- |
| `int` | Signed 64-bit integer | `0` |
| `float` | IEEE binary64 floating-point value | `0.0` |
| `bool` | Boolean | `false` |
| `string` | Non-null string reference | Initializer required |
| `T?` | A value of `T` or null | `null` |
| `void` | No returned value | Not a variable or parameter type |

The compiler may infer the type of an intermediate expression, but never changes
the declared type of a variable after an assignment. Assigning an integer to a
float inserts a numeric conversion, including at argument and return boundaries.
Float-to-int conversion is explicit. Integer-to-float conversion can lose precision
for integer magnitudes above 2^53. A bool is not a numeric value or an integer alias.
Conditions must have type `bool`; numeric and string truthiness are not supported.

The scalar intrinsics `print`, `tostring`, `toint`, and `tofloat` are checked using
their frontend signatures. `int(value)` and `float(value)` are explicit conversion
spellings for `toint` and `tofloat`. This does not expose arbitrary backend native functions.
Use an explicit conversion when assigning a floating-point value to an integer.
Nullable values require a supported explicit access mechanism before use as `T`;
they are never silently treated as a non-null value.

Only `import math;` and `import system;` are available as built-in module imports.
The frontend recognizes numeric `math.sin`/`math.cos` and zero-argument `system.clock`
with declared signatures. Other library functions, host members absent from the supplied schema, and
imported script modules are rejected rather than delegated to dynamic resolution.

Integer arithmetic retains the backend's 64-bit wrapping rules for addition,
subtraction, multiplication, and negation. Integer division and remainder reject
zero divisors and the minimum-integer divided by minus-one case. `%` is integer-only.
Shifts require an integer count in `0..63`. Floating-point arithmetic retains signed
zero, NaN, and infinity. Runtime failures remain failures rather than becoming
unchecked native operations simply because source types are known.

## Control flow

The initial control-flow forms are blocks, `if`/`else`, `while`, range `for`,
`break`, `continue`, and `return`. Range iteration uses an explicit integer declaration:

```cpp
int total = 0;
for (int i in 0, 10)
    total += i;
print(total);
```

The end is exclusive; the optional third expression is the step. This is not a
C-style three-clause `for`. Ordinary expression operands and call arguments retain
left-to-right evaluation. Assignments retain the backend's ordering rules, including
evaluation of the right-hand side before resolving a nested target path once such
targets are supported. Type checks and conversions must not reorder side effects.

## Current host-call boundary

`NeoCore::Program::Call` invokes exported scalar functions and validates arguments
before executing the program. Host scalar arguments cannot bind script reference
parameters. Null return values are supported; null arguments from the host are
explicitly rejected until the backend call adapter supports them. This is narrower
than the nullable values that checked script code can pass internally.
Host-initiated script reentry is rejected while top-level initialization is still
running. Initialization runs at most once, and a failed initialization remains a
failure on subsequent `Run`/`Call` requests.
Struct, collection, and native-object results are rejected before top-level initialization or the
requested function executes; the current host adapter only marshals scalar values.

The separate script-to-host direction uses `HostSchema` and `HostBindings` from
`NeoCoreHost.h`. `Program::Compile(source, bindings, error, ...)` copies the binding
configuration, checks the source against its schema, and links each member to a
fixed callback. It supports `int`, `float`, `bool`, `string`, declared native object
types, vectors/quaternions, nullable forms of those types, and `void` results. Host structs,
collections, reference parameters, overloads, and default arguments are deferred.
See [the host binding contract](HostBindings.md) for setup, object ownership,
callback failures, and reentry rules.

Native types have nominal identity and reference semantics. A native variable
requires an initializer unless its type is nullable. A native type and a host
global may have the same name; their expression/declaration contexts distinguish
them. Scripts cannot construct native objects, convert their handles to numbers
or strings, or print their internal representation. Equality compares compatible
native handles; unrelated native types are incompatible.

Host globals are read-only handle bindings in script code. Their writable
properties can still be changed. `Program::SetHostObject` replaces a global ID
in that program; existing script variables holding an older ID keep that value.
Native parameters and results are supported between checked script functions,
but do not extend the scalar `Program::Call` entry point.

Property reads call the registered getter. Assignments evaluate the right-hand
side before the complete target object path; compound assignments then call the
getter, compute the result, and call the setter. The receiver is evaluated once.
Pre/post-increments preserve their respective result values. Read-only and
write-only restrictions are compile errors, and properties cannot bind `T&`
because a getter/setter pair is not an addressable storage location.
Nullable native values can be stored, passed to nullable host parameters, and
compared with null. A null comparison in `if` refines a named, unaliased native
local or by-value parameter for member access in its non-null branch. Reassignment
or exposing a mutable reference invalidates this proof. Globals, nullable fields,
and general nullable scalar/vector unwrapping are not refined by this feature.
Object liveness is still validated by the runtime resolver on every host call.

This implementation uses private fixed binding objects and generated property
helpers over the inherited call machinery and value slots. Host argument readers
are selected from the schema, but the entire VM is not tagless. Host result
validation, nullable checks, and object-lifetime resolution remain runtime work.
With debug metadata enabled, generated property-helper locations are mapped to
their original property operation's source line. Internal helper frames can still
appear in the call stack. No performance claim follows from this binding milestone.

## Structs and field references

Structs are file-scoped nominal value types. Fields have explicit types and keep
their declaration order:

```cpp
struct Position
{
    float x;
    float y;
};

struct Particle
{
    Position position;
    int age;
};

void Advance(Particle& particle, float dt)
{
    particle.position.x += dt;
    particle.age += 1;
}

Particle particle = Particle(Position(1.0, 2.0), 0);
Particle snapshot = particle;
float& x = particle.position.x;
particle = Particle(Position(10.0, 20.0), 3);
x = 12.0;
Advance(particle, 0.5);
```

The final `particle.position.x` is `12.5`; `snapshot.position.x` is still `1.0`.
The field alias follows the original variable's storage through whole-struct
assignment. Writing `x` modifies the current `particle`, not a detached old value.
The [complete sample](../Samples/neocore/scripts/structs.nc) also retains a nested
struct alias and prints `12.5`, `1`, `21`, and `4` on separate lines.

`Position(x, y)` is a positional constructor with one argument per field. Ordinary
numeric conversion rules apply to each field. `Position()` and `Position p;` use
recursive defaults when every field supports one: numeric zero, `false`, nullable
scalar null, or a nested struct whose fields have defaults. A nonnullable string
field requires the full constructor; no uninitialized string is introduced.
Default field initializer expressions, methods, and collection fields are not part
of this syntax. `const` struct declarations are also deferred.

Copying a struct in a declaration, assignment, by-value argument, constructor field,
or return creates an independent value, including nested structs. By-value arguments
are captured at their evaluation point before later arguments can modify the source.
Different struct names remain incompatible even when their fields have identical
names and types. Whole-struct assignment completes its right-hand value evaluation
before updating the target's fields.

Struct references and references to fields require an exact-type mutable lvalue
rooted in a named variable or parameter. Existing aliases survive assignment to the
referenced struct or any containing struct. Temporaries and function return values
cannot be reference targets. References cannot be struct fields or escape through
return values. Recursive by-value field layouts, nullable structs, and struct
`==`/`!=` are rejected in this milestone. Nullable scalar fields remain supported.

The scalar host-call API does not marshal structs. A call requiring a struct result
is rejected before top-level initialization; struct values and references remain
available inside checked script code. Transitional generated helpers are appended
after the original source to preserve its line numbering; backend call stacks may
include those internal helper frames.

## Vectors and quaternions

`Vector2`, `Vector3`, `Vector4`, and `Quaternion` are distinct value types with
float32 components. Reading a component produces the ordinary binary64 `float`;
writing narrows with the inherited float32 rounding and overflow rules.
Integer constructor/component arguments widen to `float` before storage.

```cpp
Vector3 position = Vector3(1, 2, 3);
Vector3 copy = position;
position.x += 2.0;
Quaternion rotation = Quaternion(1, 0, 0, 0);
float real = rotation.w;
```

Constructors accept either no arguments or exactly the type's component count.
Vector constructor order is `x, y, z, w` up to its dimension. Quaternion order is
**`w, x, y, z`**, matching the inherited host API; `.w` selects the first stored
component and `.x` selects the second. Default vectors are zero; a default
quaternion is the identity `(1, 0, 0, 0)`. `Vector4` and `Quaternion` cannot be
implicitly interchanged.

Named component accesses are resolved during type checking and lowered to fixed
indices. Missing components, such as `Vector2.z`, are compile errors. Assignment,
numeric compound assignment, and pre/post-increment/decrement write back to the
containing vector. Scalar results of component mutations reflect the stored
float32 value; a postfix result preserves the previous component value.

Vectors copy by value through declarations, assignment, script arguments/returns,
and struct fields. A `Vector3&` can bind to a named vector or a struct vector field;
component references and host-property references are rejected. Vector indexing
and whole-vector equality are deferred rather than delegated to untyped operations.
See the executable
[vector sample](../Samples/neocore/scripts/vectors.nc).

`Vector2`, `Vector3`, and `Vector4` support addition/subtraction with the same
vector type, unary `+`/`-`, multiplication by a numeric scalar on either side,
and division of a vector by a numeric scalar. `+=`, `-=`, `*=`, and `/=` follow
the same type rules. Dimensions cannot mix; vector-vector multiplication,
scalar/vector division, and Quaternion arithmetic are rejected. Quaternion is
a distinct rotation type, not a four-component arithmetic-vector alias.

Arithmetic preserves the inherited vector rules: a scalar multiplier/divisor is
rounded to float32 before the component operations, and vector arithmetic uses
float32. This differs from explicitly reading a component and doing binary64
scalar arithmetic before writing it back. Float32 zero division follows IEEE
infinity/NaN behavior. Scalar-first multiplication still evaluates its scalar
expression first; lowering never swaps source side effects to reuse a VM operation.

Host vector properties additionally support whole-vector compound operations and
component writes. The compiler first
evaluates the right-hand side, evaluates the receiver path once to capture its ID,
calls the getter, updates the copied vector, and calls the setter. Both callbacks
independently resolve the same receiver ID. A component write needs both a getter
and a setter, as does a whole-vector compound operation; plain whole-value
assignment only needs a setter. If the object is deleted
during the getter, the setter fails before entering host code. A detached local
vector copy is independent; mutating it does not implicitly call a host setter.

## Typed arrays

`array<T>` uses the existing native array storage with a fixed element type.
Supported elements are `int`, `float`, `bool`, `Vector2`, `Vector3`, `Vector4`,
and `Quaternion`. Integers are int64, scalar floats are binary64, and vectors
retain float32 components. Array variables require an initializer.

```cpp
array<int> counts = array<int>();
array<float> weights = array<float>(4, 1.5);
array<Vector3> positions = array<Vector3>(4);
positions[0] = Vector3(1, 2, 3);
positions[0] += Vector3(0.5, 0, 0);
positions[0].x *= 2.0;
```

`array<T>()` creates an empty array with the type's default fill.
`array<T>(size)` uses that default; `array<T>(size, fill)` evaluates size first,
then the compatible fill value once. Default scalar fills are zero/false,
default vectors are zero, and Quaternion defaults to identity. Integer fill
values widen for float arrays. Array types are invariant: `array<int>` cannot
be assigned or passed as `array<float>`, and `array<Quaternion>` differs from
`array<Vector4>` despite shared backend component storage.

Supported methods are `len()`, `resize(size)`, `append(value)`, and
`reserve(capacity)`. Resizing preserves retained elements and initializes growth
with the constructor's saved fill value, including after shrinking and regrowing.
`reserve` changes capacity without changing length; append checks the element
type and adds one value. Array indices and sizes must have type `int`.
Bounds remain runtime checks, and sizes above `INT32_MAX` are rejected before
narrowing. Negative constructor sizes and reserve capacities fail; negative
resize sizes retain the inherited rule of clamping to zero.

Array assignment, function parameters, and returns share the container. Rebinding
one variable leaves other aliases unchanged. Reading a vector element returns an
independent value, while `positions[i].x = value` updates the stored element.
Assignments evaluate the RHS first, then the array and index once in that order;
component writes read and update the selected vector after those evaluations.
Compound operations and pre/post-increments preserve their stored/previous results.
Named array variables can bind exact-type references, but element and component
references are rejected because resizing can move their storage.

String/struct/native-object elements, nested/nullable arrays, array literals,
compact 32-bit arrays, collection fields in structs, and typed host array
marshaling are separate milestones. Unsupported element types and mismatched
writes fail during type checking, including a float written to an integer array.
The typed path emits dedicated storage instructions and compile-time write
sequences rather than runtime array helpers. Bounds and allocation checks remain.
The transitional source bridge and other generic VM paths still exist; this does
not make the complete runtime tagless.

The [particles sample](../Samples/neocore/scripts/particles.nc) updates position
and velocity Vector2 arrays and compares every step with scalar array references.
It uses small, exactly representable inputs and checks a fixed checksum without
timing the workload.

## Scalar lists and maps

The list/map subset is `list<T>` and `map<K, V>`. List elements and map
values must be nonnullable `int`, `float`, `bool`, or `string`; map keys must be
`int` or `string`. Collection variables require initializers. Nested collections,
nullable collections/elements, collection fields in structs, and element references
are not supported yet. Mutable collection types are invariant.

```cpp
list<float> values = [1, 2.5];
values.resize(4);
values[2] = 3;

map<string, int> counts = {};
counts["alpha"] = 2;
int present = counts.get("alpha", 0);
int? missing = counts["missing"];
```

List literals check every element and materialize numeric widening. Indices are
integers. `list.resize(size)` preserves retained elements and initializes newly
added numeric/bool elements with zero/false. `list.resize(size, fill)` uses an
explicit fill value evaluated once; string lists require that form. Regrowth does
not expose stale or null entries. Lists support indexed assignment, compatible
compound assignment, and increments/decrements.

Maps currently start with `{}` and gain entries through `m[key] = value`. Direct
`m[key]` reads have type `V?`; an absent key produces null. `m.get(key, fallback)`
returns `V` and requires an explicit compatible fallback. The receiver, key, and
fallback are evaluated left to right before lookup; fallback evaluation is eager,
even for a present key. Null is not a valid stored value or a removal operation.
Map removal is not exposed by this initial method subset.

The supported methods are list `len`/`resize`, map `len`/`get`, and string `len`.
String length counts Unicode code points. A stored map key such as `"get"` or
`"len"` does not shadow those statically resolved methods. Other inherited backend
methods require explicit frontend support before they can be called.

Lists and maps retain reference semantics: assignments, by-value parameters, and
returns share the same container. Element writes and list resizing are visible
through aliases. Rebinding one collection variable does not rebind another variable
that refers to the original container. This differs from independent struct values.

## NeoCore benchmark programs

[Samples/bench/check.nc](../Samples/bench/check.nc) runs the eight benchmark algorithms
with small inputs and checks expected results without timers. The full
[Samples/bench/bench.nc](../Samples/bench/bench.nc) retains the original workloads and
iteration counts, using explicit types, `tostring`, and `map.get(key, 0)`. All queried
map keys are present, so the explicit fallback does not change the checksum.

These adaptations preserve the intended work and results, not identical bytecode or
instructions. The backend combines dedicated numeric instructions with generic VM
operations and generated helpers; resizing, struct copies, and method adapters have costs.
Run the correctness program before timed comparisons and use non-debug mode for
timing. Ask the user before starting timed benchmarks. The inherited README numbers
remain NeoScript measurements until separately measured for NeoCore.

## Accepted design for later milestones

These rules are agreed targets, not a claim that the initial frontend implements them.

### Collections and storage widths

Ordinary arrays, lists, maps, and future sets have fixed element/key/value types.
An `array<int>` stores 64-bit integers and an `array<float>` stores binary64 values.
Mutable collections are invariant: an integer collection is not automatically a
float collection merely because individual integers can convert to floats.

**`int32` and `float32` are not user-declarable scalar types.** Neither variables nor
parameters nor return types can use those names. Compact 32-bit storage is a property
of the supported array form and vector components only. Compact integer arrays reject
out-of-range values. Compact float arrays use the defined float32 rounding and overflow
behavior. Reading an element as a scalar produces an ordinary `int` or `float`.
Ordinary arrays and compact arrays remain distinct types; there is no automatic
container conversion. Compact-array declaration syntax is to be finalized with that
milestone; its storage must not introduce public 32-bit scalar types indirectly.

Maps use explicit removal. A lookup that may miss returns a nullable value; assigning
null is not a substitute for removing a key. Heterogeneous collections and implicit
dynamic fallback are excluded from the initial language. JSON will use a separate
`JsonValue` interface with explicit kind checks/conversions or typed decoding.

### Further user types

The current struct implementation has declared fields, nominal identity, and value
assignment. Collections and future host objects retain reference semantics. Class
inheritance, user-defined generics, and function overloading are deferred. Built-in
collection type parameters do not imply support for arbitrary user generics.
Closure/function values require complete parameter and return signatures; their
surface syntax and capture representation will be specified before enabling them.

### Host integration

The scalar/vector/native C++ binding slice above implements registered signatures and
property permissions. Its remaining extensions include typed host value structs,
collections, overload/default-argument metadata, and complete retained
callback signatures. A portable schema/link ABI for offline compiled images is
also deferred; current type and binding IDs belong to a schema snapshot.
The same contract must eventually reach the C ABI and Unity. Object lifetime,
callback reentrancy, coroutine suspension, and error unwinding continue to apply.

## Remaining implementation stages

1. Expand typed operations to compound assignments, collection reads/writes, and
   function argument/result transfers. Preserve the VM dispatch, frame, and
   execution-control architecture.
2. Replace the private textual intermediate representation with direct typed bytecode
   emission and slot-lifetime metadata. Remove destination cleanup only where lifetime
   analysis proves that the slot cannot own a reference.
3. Validate loaded bytecode against type/signature metadata and measure each conversion
   stage against the generic path with matching results.
4. Expand the frontend to typed sets, compact arrays, nested collections, script modules,
   richer host signatures, and function values.
5. Connect JSON, coroutine APIs, the C ABI, Unity, and editor tooling to those types,
   and migrate the remaining backend samples and documentation.

The current backend still stores dynamic tags and performs generic work outside the
implemented typed operations. An early numeric-VM snapshot measured on 2026-10-31
found a 7.5% increase in the sum of eight case medians versus generic lowering in
the same executable; see the [historical paired measurement](TypedVmBenchmark.md).
That snapshot predates the current IR and aggregate instructions and does not
measure this build. Local operand handling, constant localization, and numeric
conversion costs require fresh paired measurements before claiming an improvement.
Performance claims require paired baseline/trial runs with matching results; inherited NeoScript benchmark
numbers are not NeoCore measurements. Ask the user before starting timed benchmarks.
