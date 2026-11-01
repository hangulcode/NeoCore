# NeoCore static language

NeoCore is an independent, statically typed language derived from NeoScript.
This document separates the agreed language design from the initial implementation.
The shared execution backend is implementation infrastructure. Its dynamic source
syntax and APIs are not evidence that a feature is accepted by the NeoCore frontend.

**`.nc` is the canonical NeoCore source extension.** Dynamic `.ns` samples and their
tools belong to the [NeoScript repository](https://github.com/hangulcode/NeoScript).
The typed VS Code extension provides completion, hover, signature help, diagnostics
and DAP debugging; see [the extension guide](../Tools/vscode-neocore/README.md).

## Implementation boundary

The frontend tokenizes source, builds typed expressions and statements,
collects function signatures, and checks types before producing typed IR and a
transitional backend statement skeleton. The checked program then runs on the
existing bytecode VM. This is not a dynamic compatibility mode for NeoCore source.
Unsupported syntax and unknown types are compilation errors; they do not fall back
to untyped compilation. Generated backend source is an implementation artifact.
Backend helper, temporary, host-binding and IR marker names begin with `$neocore_`.
The `$` character is not valid in NeoCore source identifiers or host schema names;
only the checked backend compilation accepts these internal identifiers. User names
beginning with `__` remain valid. Literal strings and comments do not affect the
internal prefix.

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
instructions, preserving checked divisor and shift-count rules. Two numeric-negation
instructions complete the 30 numeric typed instructions.

An int/int comparison remains an exact signed 64-bit comparison. A mixed int/float
comparison converts the integer to binary64, matching the existing generic comparison
rule; integers above 2^53 can round before comparison. This is not a new exact
integer-versus-float comparison policy. Bool, string, reference and null comparisons
continue to use their existing generic instructions.

Negation inverts the comparison result rather than replacing an ordered comparison
with its apparent opposite. Thus `!(nan < x)` is true while `nan >= x` is false.
Ordinary supported `if` range conditions retain the inherited single range instruction.
Source-negated ordered range conditions remain two typed comparison branches so that
the NaN behavior of each original comparison is preserved.
Compound `while` conditions retain the inherited multi-instruction path; a single
numeric comparison in `while` uses a direct typed branch.

Numeric expressions and named scalar writes reach the bytecode emitter as `TypedIR` in `NeoCoreIR.h`.
Nodes retain the semantic operation, checked type ID, numeric storage type, literal
value or resolved symbol identity, ordered operand indices, and original source span.
Mixed arithmetic has explicit conversion nodes. A collision-free marker in the
statement skeleton identifies an IR root; it contains no numeric operand or operator
text. The emitter validates the graph and emits instructions directly from its nodes.
The compile descriptor borrows the IR only until compilation returns; compiled
programs and saved images do not retain pointers to the graph.

Host/builtin calls, container/host writes, and selectors that have not migrated to IR use typed
`LegacyExpression` leaves. Each leaf is compiled when traversal reaches its original
evaluation position, with the current lexical scope and original source coordinates.
For example, `(1 / 0) + F()` must fail before `F()` executes. Earlier operands are
snapshotted before later effects, including assignments hidden inside an IR marker.
Logical short-circuiting remains in the existing statement/expression compiler.
Unary negation, bitwise negation, and logical inversion also have semantic IR nodes.
Numeric negation selects `NOP_I64_NEG` or `NOP_F64_NEG`; bitwise negation uses typed
integer XOR. `NOP_LOG_NOT` reads a checked bool payload in NeoCore, without dynamic
truthiness conversion.

Named `int`, `float`, `bool`, and `string` assignments have explicit storage targets
in IR. Numeric compound assignments and prefix/postfix increment/decrement use the
same path. A target records a resolved variable and an ordered flat struct-field
path. This includes local/file variables and parameters,
and scalar fields of named structs; host properties, vector components, and
collection elements retain their existing write paths.

The RHS finishes before the target path is resolved or a compound assignment reads
the old target value. Explicit numeric widening precedes the operation. Assignment
and prefix results retain the stored value; postfix results retain the old value,
even when later operands modify the same storage. Typed arithmetic errors occur before
the write, preserving the destination while retaining already completed RHS effects.
Struct fields store values directly, without alias cells. Whole-struct assignment
replaces the destination with an independent copy. Internal boxed vector locals
remain only for component writes.

Named script calls carry resolved function signatures and ordered argument nodes.
Arguments finish left to right before being moved to the shared call window.
Struct copy and the transfer of `null` into a reference-typed slot are explicit IR operations. Return records
carry their owner function, value and source location; the emitter verifies their
types and reuses the existing call/return instructions. Void fallthrough and
generated helper bodies still use the transitional statement compiler.

Arrays, vectors, and generated helpers retain private compile-only intrinsics in
the textual skeleton. Their metadata carries types and source lines, and each
intrinsic emits instructions without a runtime helper call frame. Declarations,
function layout, control flow, and these remaining paths have not yet moved to a
whole-program IR emitter.

Arrays and vectors add 59 dedicated instructions, described in the same central
metadata header as the numeric instructions. Vector2/3/4 construction, arithmetic,
component reads and functional component replacement use known component layouts.
Array construction, reads, writes and append select bool, int64, binary64, int32, float32 or a
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

`const` initializers, `case` labels and struct field defaults share one frontend
constant evaluator over the checked expression tree. Constants are evaluated once
at declaration, including integer-to-float widening, and subsequent references use
that stored value. Lowering emits the evaluated literal rather than asking the
backend to recalculate the expression. Integer wraparound, division errors, shift
bounds, decoded strings and signed zero follow the same rules in all three uses.
Only a resolved vector/Quaternion constructor can be evaluated as a call; an
ordinary function returning a vector is not a constant constructor. Host/builtin calls,
lists/maps, whole-struct helpers and host property
write composition retain their existing paths. Host vector calculations use the
new instructions inside the write helper, while getter/setter calls independently
revalidate the saved receiver ID. Bool/string conversions remain checked generic
conversions. Numeric expressions now emit from structured IR; the remaining
compile-only intrinsics and compatibility leaves are explicit migration boundaries,
not a complete typed-AST-to-bytecode backend replacement.
NeoCore bytecode images use version `0151` and must be regenerated; the NeoCore
image container that carries the static type table is at version 4. Regenerate older
images: generated-code source mappings now mark shared helpers so their failures
report the user callsite. The five generic `NOP_TABLE_*` arithmetic-write
instructions, their handlers and
unused table-update helpers have been removed. Compound collection writes now
emit a read, arithmetic operation and ordinary write; WREF pairs only with
`CLT_MOV`. Vector compound RHS values round to float32 on storage, not before arithmetic.
Out-of-range compound writes fail at the initial read and report a read-index
error; the destination remains unchanged.
The unused `NOP_AND3`, `NOP_OR3`, and `NOP_VERIFY_TYPE` instructions were also removed; active bitwise operators and range-loop conversions
retain their existing behavior. Earlier images are rejected because opcode numbers
changed.

Generic three-operand arithmetic and bitwise instructions (`NOP_ADD3`, its local
variant, and the corresponding subtraction, multiplication, division, remainder,
shift, XOR, AND, and OR forms), generic compound arithmetic (`NOP_ADD2` through
`NOP_XOR2`), `NOP_MOV_MINUS` and its local variant, and fused
`NOP_READ_ADD`/`SUB`/`MUL`/`DIV` are excluded from the NeoCore opcode set and VM
dispatch. The frontend always emits the checked typed path; its generic lowering
mode has been removed. Literal folding uses compiler semantic operations rather
than generic VM opcode numbers. Comparisons still needed for bool, string, and
reference values, and other unmigrated operations, remain available.

CMake links `NeoCore::Runtime` to `neocore_backend`. Dynamic NeoScript library
targets and their separate test distribution have been removed from this checkout.
The shared VM code required by typed execution remains; the generic arithmetic,
logical, increment and runtime type-query opcodes of the dynamic language have been
deleted from the source rather than compiled out.

The current frontend covers scalar and vector declarations, nominal structs and their fields,
typed arrays, homogeneous lists/maps/sets, nested collections, named functions,
function values and closures, forward and recursive calls, block scopes, conditions,
range loops, `foreach`, returns, and checked library calls. String, regex, UTC time,
TimeSpan, Stopwatch, HTTP requests, and script coroutines have explicit signatures.
Script modules are resolved and combined before static checking; they do not enter
the inherited dynamic compiler directly.
Explicit host schemas additionally expose nominal native objects, host globals,
methods, and read/write properties with scalar, vector, native, struct, collection
and function signatures. Members and
argument types are resolved before backend compilation; see [HostBindings.md](HostBindings.md).
Host event schemas additionally associate a registration member and literal event
name with an exact function type. `GameObject.AddEvent("OnUpdate", OnUpdate)`
therefore checks `OnUpdate` against the host's declared signature without requiring
`export`. Different event names may use different callback signatures; this is
compile-time event selection, not general method overloading. Unknown names,
nonliteral names (including `const` identifiers), and mismatched callbacks fail
compilation. The existing host-call instruction invokes the selected binding.
The host retains the callback value and caller instance ID, resolves its receiver
object ID at runtime, and owns dispatch, suspension scheduling and registration
cleanup on reset or destruction.
Host object values may be null: an unbound global, a host result such as a failed
lookup, or a variable that was never assigned. The compiler does not track null
guards. Compare with `null` wherever it is useful; a member access or method call on a
null host object fails at runtime with "cannot access ... on a null host object",
in the same class as access to a destroyed object.

`array32<int>` and `array32<float>` retain compact int32/float32 element storage
while reads produce the ordinary int64/binary64 scalar types. Their constructors,
reads, writes and append operations select dedicated instructions. Integer stores
reject out-of-range values before mutation; float stores use the common float32
rounding rule. Assignment/prefix results are the stored value and postfix results
are the old value. `int32` and `float32` remain unavailable as scalar declarations.

The owning host value API supports typed aggregates and retained function values.
Closures retain their creating instance identity; reset or cross-instance invocation
is rejected. Script JSON uses `system.jsonEncode(value, sortKeys)` and
`system.jsonDecode<T>(text)` with a compile-time target type. Host decoding uses
an explicit registered target type. The
[typed C ABI and Unity package](../Unity/NeoCore/README.md) expose these contracts
without dynamic fallback. The CLI's `--lsp` and `--dap` modes use the NeoCore
frontend and source maps. Dynamic NeoScript integrations remain in the original repository.

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
string concatenation in constant initializers remain unsupported. Nonfinite constant
results, division by zero, integer division overflow and shift counts outside
`[0,63]` are compile errors at the original expression. The same logical and string
operators remain available in ordinary
runtime expressions. `export const` is rejected; use `public const` for a module constant.

## Value parameters

NeoCore does not support pointers or `T&` local declarations, parameters or returns.
Functions return changed scalar, vector or struct values, which callers assign explicitly.
Structs and vectors have value semantics. Collections retain shared-container semantics,
and native objects remain validated IDs; removing language references does not change
these identity or lifetime rules.

## Scalar types and conversions

| Type | Meaning | Default without initializer |
| --- | --- | --- |
| `int` | Signed 64-bit integer | `0` |
| `float` | IEEE binary64 floating-point value | `0.0` |
| `bool` | Boolean | `false` |
| `string` | String reference; may be null | `null` |
| `void` | No returned value | Not a variable or parameter type |

Nullability is a property of the type category rather than a per-declaration marker.
`int`, `float`, `bool`, `Vector2/3/4`, `Quaternion` and structs can never hold null;
assigning `null` to them or comparing them with `null` is a compile error.
Strings, lists, arrays, maps, sets, function values, coroutines and host objects can
always hold null, default to null when declared without an initializer, and may be
compared with `null`. The `T?` spelling is rejected with a message that names the
rule. Using a null reference (`s.len()`, `items[0]`, `f()`, `go.Position`) is a
runtime error, not a compile-time proof obligation.

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

`math`, `system`, and `coroutine` are built-in imports. Numeric math calls currently
include `sin`, `cos`, `sqrt`, `abs`, `floor`, `ceil`, and `pow`; integer arguments
widen to float. The checked library table explicitly lists every supported signature.
Other backend library functions and host members absent from the supplied schema
are rejected rather than delegated to unchecked dynamic resolution.

Integer arithmetic retains the backend's 64-bit wrapping rules for addition,
subtraction, multiplication, and negation. Integer division and remainder reject
zero divisors and the minimum-integer divided by minus-one case. `%` is integer-only.
Shifts require an integer count in `0..63`. Floating-point arithmetic retains signed
zero, NaN, and infinity. Runtime failures remain failures rather than becoming
unchecked native operations simply because source types are known.

## Control flow

Control-flow forms are blocks, `if`/`else`, `switch`, `while`, range `for`, `foreach`,
`break`, `continue`, `return`, and `yield`. Range iteration uses an explicit integer declaration:

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

`switch` keeps the inherited table-dispatch form: the VM evaluates the selector
once and jumps through a per-switch table, so matching does not grow into a
comparison chain. The selector must be `int` or `string`. `float` is rejected
because exact comparison is unreliable, `bool` is rejected because `if` already
expresses a two-way choice, and a null `string` selector falls to `default`. The
selector type is known at compile time, so each `case` must list compile-time
constants of exactly that type, including `const` names and constant arithmetic;
a label of another type, a duplicate value, and a second `default` are compile errors.

```cpp
const int Idle = 0;
const int Moving = 1;

string Describe(int state, string tag)
{
    switch (state)
    {
    case Idle:
        return "idle";
    case Moving, 2:
        if (tag == "fast")
            break;
        return "moving";
    default:
        return "unknown";
    }
    return "fast";
}
```

There is no fallthrough: a clause ends at the next `case`, `default`, or `}` and
continues after the switch. Combine labels with commas instead of stacking empty
clauses; a clause without a body is rejected so that C-style fallthrough intent
cannot be silently lost. `break` leaves the switch, `continue` applies to the enclosing
loop, and each clause body is its own block scope. A switch returns on every path
only when it has a `default` and every clause returns. The clause order does not
affect matching, and an unmatched value runs `default` or skips the switch.

## Current host-call boundary

`NeoCore::Instance::Call(name, arguments)` invokes exported functions and validates
arguments before executing the instance. The function-value overload invokes a
retained callback without requiring export. Compile once with `Runtime::Compile`
(or isolated `Program::Compile`), then create
independent execution states with `Program::CreateInstance(error)`. Each instance
owns its script globals, host-global IDs, and pending invocation while retaining
the shared code. `Program::Call` and the other convenience methods delegate to one
default instance. Instances sharing a program execute serially on the host thread.
Reference-typed parameters and results accept null; value types reject it.
Host-initiated script reentry is rejected while top-level initialization is still
running. Initialization runs at most once, and a failed initialization remains a
failure on subsequent `Run`/`Call` requests.

An instance can suspend through `sleep(int)` and resume through `Resume(deltaTime)`.
`Result::status` distinguishes `Completed`, `Suspended`, and `Failed`; successful
suspension still has `ok() == true`. Check `suspended()` or `IsSuspended()` and let
the host drive subsequent frames. `sleep(0)` yields once. Positive sleep uses real
elapsed time in integer milliseconds; negative durations clamp to zero and
durations above `INT32_MAX` milliseconds fail at runtime. The finite, nonnegative
`deltaTime` parameter is currently reserved and does not advance its timer. A busy
instance rejects another call without replacing its pending execution; the host owns event queuing. If file
initialization suspends during `Call`, the original function and argument values
are retained. `Resume` finishes initialization and then executes that retained call
without a retry. Callback reentry remains synchronous and targets the calling instance.

The owning boundary marshals declared structs, collections, native IDs and function
values as well as scalars and vectors. Runtime-owned values retain their source
program and cannot cross into another program, even within a shared runtime.
Closures are additionally restricted to their creating instance and expire on reset.
Unsupported boundary kinds fail explicitly before the requested execution starts.

The separate script-to-host direction uses `HostSchema` and `HostBindings` from
`NeoCoreHost.h`. `Program::Compile(source, bindings, error, ...)` copies the binding
configuration, checks the source against its schema, and links each member to a
fixed callback. It supports `int`, `float`, `bool`, `string`, declared native object
types, vectors/quaternions, and `void` results. Host structs,
collections, overloads, and default arguments are deferred.
See [the host binding contract](HostBindings.md) for setup, object ownership,
callback failures, and reentry rules.

Native types have nominal identity and reference semantics. A native variable
declared without an initializer is null. A native type and a host
global may have the same name; their expression/declaration contexts distinguish
them. Scripts cannot construct native objects, convert their handles to numbers
or strings, or print their internal representation. Equality compares compatible
native handles; unrelated native types are incompatible.

Host globals are read-only handle bindings in script code. Their writable
properties can still be changed. `Instance::SetHostObject` replaces a global ID
in that instance; existing script variables holding an older ID keep that value.
Set instance-specific IDs before `Run` when global initializers use those objects.
`Program::SetHostObject` changes only the default instance.
Native parameters and results are supported between checked script functions,
but do not extend the scalar host `Call` entry point.

Property reads call the registered getter. Assignments evaluate the right-hand
side before the complete target object path; compound assignments then call the
getter, compute the result, and call the setter. The receiver is evaluated once.
Pre/post-increments preserve their respective result values. Read-only and
write-only restrictions are compile errors.
Native values can be null, can be stored and passed to host parameters as null, and
can be compared with null. The host adapter rejects a null receiver before the
callback runs and passes null arguments through to the host callback, which decides
whether null is acceptable. Object liveness is still validated by the runtime
resolver on every host call.

This implementation uses private fixed binding objects and generated property
helpers over the inherited call machinery and value slots. Host argument readers
are selected from the schema, but the entire VM is not tagless. Host result
validation, null-receiver checks, and object-lifetime resolution remain runtime work.
With debug metadata enabled, generated property-helper locations are mapped to
their original property operation's source line. Internal helper frames can still
appear in the call stack. No performance claim follows from this binding milestone.

## Struct values

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

Particle Advance(Particle particle, float dt)
{
    particle.position.x += dt;
    particle.age += 1;
    return particle;
}

Particle particle = Particle(Position(1.0, 2.0), 0);
Particle snapshot = particle;
particle = Particle(Position(10.0, 20.0), 3);
particle.position.x = 12.0;
particle = Advance(particle, 0.5);
```

The final `particle.position.x` is `12.5`; `snapshot.position.x` is still `1.0`.
The [complete sample](../Samples/neocore/scripts/structs.nc) prints
`12.5`, `1`, `21`, and `4` on separate lines.

`Position(x, y)` is a positional constructor with one argument per field. Ordinary
numeric conversion rules apply to each field. `Position()` and `Position p;` use
recursive defaults: numeric zero, `false`, zero vectors, the identity quaternion,
null for string, collection, function and host-object fields, or a nested struct
whose fields have defaults. The positional constructor still requires one argument
per field; declared defaults do not shorten it.

A field of type `int`, `float`, `bool`, `string`, `Vector2/3/4` or `Quaternion` may
declare its own default: `int target = -1;`, `float best = 999.0;`,
`string name = LABEL;`, `Vector3 spawn = Vector3(0, 1, 0);`. The initializer is a
compile-time constant: a literal, a `const` name, a sign, constant arithmetic, or a
vector constructor whose components are constants. Collection, function, host-object
and struct fields cannot declare defaults; they start as null or use their own field
defaults. Declared defaults apply to `Position p;`, `Position()`, and to fields that
lenient JSON decoding finds missing. They are evaluated once, stored in the type
table and the image, and reused by the host boundary.
Nonconstant field initializer expressions, methods, and `const` struct declarations
remain deferred. Copying a struct copies its nested struct values, while its
collection fields keep their shared-container identities.

Copying a struct in a declaration, assignment, by-value argument, constructor field,
or return creates an independent value, including nested structs. By-value arguments
are captured at their evaluation point before later arguments can modify the source.
Different struct names remain incompatible even when their fields have identical
names and types. Whole-struct assignment completes its right-hand value evaluation
before replacing the destination value.

The backend stores a struct as a flat field list. Nested structs occupy nested
field lists; individual fields and the root are not boxed into alias cells.
The clone helper recursively copies nested structs. There is no in-place assignment
helper to preserve field aliases. Recursive by-value layouts and struct `==`/`!=`
are rejected. A struct value itself can never be null.

The owning host-call API marshals structs against their exact registered fields.
Host schemas can declare nominal structs and use them in properties, method
arguments and results. Structs crossing a value boundary copy nested struct fields;
collection fields retain their container identity. Transitional generated helpers
report runtime errors at the user callsite and are omitted from user-facing error
stacks. User module functions and lambda bodies retain their own source locations.

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
and struct fields. Language references, including `Vector3&`, component references,
and host-property references, are rejected. Vector indexing
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
Array variables and elements cannot bind language references.

`array32<int>` and `array32<float>` use the same constructor/index/method forms
with compact four-byte storage. Their scalar expressions use ordinary int64 and
binary64 values. Stores reject int32 overflow and round float32 values according
to the common narrowing rule; compound/prefix results return stored values.
Ordinary and compact array types are distinct and do not implicitly convert.
Typed host marshaling and bulk copying preserve the declared storage width.
String/struct/native-object elements, nested arrays and array literals remain
unsupported. Mismatched writes fail during type checking, including a float
written to an integer array.
The typed path emits dedicated storage instructions and compile-time write
sequences rather than runtime array helpers. Bounds and allocation checks remain.
The transitional source bridge and other generic VM paths still exist; this does
not make the complete runtime tagless.

The [particles sample](../Samples/neocore/scripts/particles.nc) updates position
and velocity Vector2 arrays and compares every step with scalar array references.
It uses small, exactly representable inputs and checks a fixed checksum without
timing the workload.

## Lists, maps, sets, and iteration

The collection forms are `list<T>`, `map<K, V>`, and `set<T>`. Lists and map
values can contain scalar, vector, struct, collection, function, or object values.
Map keys and set elements follow the same rule: only `int` or `string`, because the
backend hashes and compares them by value. Nested collections and collection fields
in structs are supported. A collection variable declared without an initializer is
null; reference-typed list elements and map values may hold null while value-typed
elements cannot. Map keys and set elements must never be null. Insertion and set
construction reject null at runtime before storing it, including nullable strings.
Language element references remain unsupported. Mutable collection types are invariant.

```cpp
list<float> values = [1, 2.5];
values.resize(4);
values[2] = 3;

map<string, int> counts = {};
counts["alpha"] = 2;
int present = counts.get("alpha", 0);
int missing = counts.get("missing", 0);
```

List literals check every element and materialize numeric widening. Indices are
integers. `list.resize(size)` preserves retained elements and initializes newly
added numeric/bool elements with zero/false and reference elements with null.
`list.resize(size, fill)` uses an explicit fill value evaluated once. Regrowth does
not expose stale entries. Lists support indexed assignment, compatible
compound assignment, and increments/decrements. Numeric compound writes and
increments/decrements lower through checked `ci64write`/`cf64write` intrinsics.
They emit element reads, typed arithmetic, and ordinary writes without generic
compound-arithmetic instructions or runtime numeric type dispatch. Receiver, index,
and RHS evaluation retain their existing order, including prefix/postfix results.

Maps accept `{}` or typed key/value literals, and gain entries through `m[key] = value`. Direct
`m[key]` reads have type `V`. When `V` is a reference type an absent key reads as
null; when `V` is a value type such as `int` an absent key is a runtime error
("map key not found"), so prefer `m.get(key, fallback)` or `m.contains(key)` for
reads that may miss. `m.get(key, fallback)` returns `V` and requires an explicit
compatible fallback. The receiver, key, and fallback are evaluated left to right
before lookup; fallback evaluation is eager, even for a present key. Compound
assignment and increments on a map element (`m[key] += 1`) are rejected; read, compute
and assign instead. `m.remove(key)` explicitly removes an entry and returns whether
it existed.

Lists support `len`, `resize`, `reserve`, `append(value[, index])`,
`insert(index, value)`, `remove(index)`, `remove_swap(index)`, `pop`, `find`,
`contains`, `clear`, `clone`, and `sort`. Removal and pop return the removed value;
an invalid index or empty pop fails at runtime. Maps support `len`, `get`,
`contains`, `remove`, `reserve`, `keys`, `values`, `clear`, `clone`, and `sort`.
Keys and values produce typed lists. A stored key such as `"get"` or `"len"`
does not shadow these statically resolved methods.

`sort` takes `fn(T,T)->bool`, where T is the list element or map value type.
Map sorting rearranges values among the existing keys, retaining inherited behavior.
The comparator runs synchronously; suspension and time-limited native sort calls
retain their runtime rejection rules. Struct comparators remain rejected until
the native callback adapter can provide independent by-value arguments.

List concatenation `a + b` and collection `clone` allocate a new outer container.
Nested collections retain shared identities. Struct elements are independently
copied by clone, concatenation, map values, and each added resize fill. Searching
for a struct with `find`/`contains` is rejected because struct equality is undefined.

After `import system;`, `set<T>()`, `set<T>(typedList)`, or `system.set(typedList)`
construct a typed set. Sets support `len`, `add`, `remove`, `contains`, `values`,
`clear`, and `clone`. Union `|`, intersection `&`, and difference `-` return new
sets of the same type and leave the operands unchanged.
All sets reject null elements, including `set<string>`. This applies to construction,
`add`, C++/Unity APIs and JSON decoding. A set variable itself may still be null;
`contains(null)` and `remove(null)` on a non-null string set return false.

`foreach (T value in collection)` iterates lists, arrays, or sets. Map iteration
uses `foreach (K key, V value in map)`; use `map.values()` to iterate values alone.
Iteration variable types must match exactly. Struct iteration variables are
independent value copies. `foreach (string character in text)` produces one
ordinary string per Unicode code point, suitable for subsequent string methods.

Lists and maps retain reference semantics: assignments, by-value parameters, and
returns share the same container. Element writes and list resizing are visible
through aliases. Rebinding one collection variable does not rebind another variable
that refers to the original container. This differs from independent struct values.

## Function values and closures

Every function value has a complete signature. Named functions can be assigned,
passed, returned, and stored in typed collections. Anonymous functions use the
same explicit parameter and return types:

```cpp
int Add(int a, int b) { return a + b; }
fn(int,int)->int operation = Add;
int result = operation(2, 3);

fn()->int MakeCounter(int initial)
{
    int count = initial;
    return fn()->int { count += 1; return count; };
}
fn()->int counter = MakeCounter(10);
print(counter()); // 11
print(counter()); // 12
```

Signatures are invariant; assignment and invocation check all parameter and return
types. Struct arguments/results keep their value-copy rules. Every non-void
anonymous function must return a compatible value on all paths.

Closures snapshot enclosing locals at creation. Scalars, structs, and vectors
are independent captured values, including nested struct fields and vectors
whose outer locals use internal component-write cells. Captured values persist
between invocations. Collections and native handles retain their usual shared
identities; globals remain globals. Nested returned closures retain their captures
after enclosing calls finish. Capture creation and named function references are
conservatively included in global-initialization dependency checks.

Lowering uses private factory helpers and the existing backend closure storage.
Lambda numeric expressions still use checked typed IR; lambda returns currently
use checked expressions with the transitional return path. Generated callback
frames can appear in debug stacks. This is not a replacement of the complete
backend function representation or public host callback API.

## Strings, time, HTTP, and coroutines

Strings expose `len`, `sub(start,count)`, `codepoint(index)`, `find`, `upper`,
`lower`, `trim`, `ltrim`, `rtrim`, `replace`, `replaceAll`, and `split`.
`split` produces `list<string>`. String lengths and substring positions count
Unicode code points; inherited case-conversion and trimming behavior is unchanged.
`regex_match(pattern[, ignoreCase])` returns bool,
`regex_search(pattern[, ignoreCase])` returns `list<string>`, and
`regex_replace(pattern,replacement[, ignoreCase])` returns string. A successful
search includes the complete match at index zero followed by captured groups;
no match returns null, so compare the result with `null` before indexing it. Regex character classes retain their
inherited byte-oriented behavior.

`system.time`, `time_ms`, `utc_date`, `utc_time_ms`, `utc_parts`, `utc_format_ms`,
and `utc_parse_ms` have checked signatures. UTC parts are `map<string,int>`.
`TimeSpan` and `Stopwatch` are nominal opaque library objects requiring
initializers, normally `system.time_span(...)`, `time_span_ms`,
`time_span_seconds`, or `system.stopwatch()`.
TimeSpan exposes `total_milliseconds`, `total_seconds`, `parts`, `format`,
`add`, `subtract`, and `compare`. Stopwatch exposes `elapsed`, `elapsed_ms`,
`elapsed_us`, `elapsed_seconds`, `start`, `stop`, `reset`, `restart`, and
`is_running`. TimeSpan arithmetic returns new spans; Stopwatch methods share
the same watch through aliases. Checked signatures preserve the backend's
overflow, format, date-range, and monotonic-clock runtime checks.

`AsyncRequest request = system.async_create();` creates an HTTP request object;
the inherited `aysnc_create` spelling is also accepted. `get(timeoutMs,url,callback)`
and `post(timeoutMs,url,body,callback)` require `fn(bool,string)->void` callbacks.
The other methods are `add_header(string,string)`, `wait()->bool`, and `close()->void`.
The host supplies a transport through `CompileOptions.httpTransport`; the library
does not make an untyped host member available. Callbacks execute on the owning
script worker when completions are processed. Backend reentry, suspension, and
error-unwinding rules still apply.

`system.jsonEncode(value, sortKeys = false)` produces a string from the expression's
static type. `system.jsonDecode<T>(text)` requires an explicit supported target
type and returns exactly that type; nested collection arguments are supported.
Both require `import system;`. Structs use JSON objects with their declared field
names, and all fields/elements are checked during conversion. Functions, native
IDs, coroutine/service handles and maps with non-string keys are rejected
recursively at compile time. Malformed text or a value that violates the declared
shape is a runtime error. This built-in syntax does not add user-defined generic
functions. Host callbacks can use `HostCall::ReturnJson` to decode against their
declared return type in the calling program; see [JSON.md](JSON.md).

After `import coroutine;`, `coroutine<fn(int)->void>` describes a coroutine whose
entry takes an int and returns void. `coroutine.create(function)` fixes that
signature; `resume(handle,args...)` checks it on every call, `status(handle)`
returns a string, and `close(handle)` closes the handle. Entry arguments are
consumed at initial start; subsequent resumes supply the same checked signature.
`yield;` suspends execution through the existing VM scheduler. Coroutine values
are distinct from host-driven `Instance::Resume`; the public scalar host boundary
does not marshal coroutine handles.

## Script modules

`import name;` and `import name as alias;` resolve `.nc` source through the host's
`CompileOptions.moduleLoader`. A dependency initializes at its first import site,
after preceding importer statements and before following ones. Repeated imports
share the same module state within an instance. Only `public` members are visible
across modules, including `public struct` types: an importer names such a type as
`alias.Type` in every type position, including local, parameter and return types,
generic arguments such as `list<alias.Type>` and `map<string, alias.Type>`, function
types, and constructor calls. All importers share one type identity. `export struct`
and `const struct` are rejected. A private struct may still appear in a public
signature; importers can use such values but cannot name their type. Imported
`export` declarations do not expose new entry points to the outer host. Private module declarations are isolated by
name rewriting. Self-imports, import cycles, inconsistent loader results, and
untyped module contents are rejected. Diagnostics translate rewritten locations
back to the source module. The runtime `system.load`/`pcall` functions have been
removed from the backend: module contents must pass the same static frontend as
the root script.

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
timing. Ask the user before starting timed benchmarks. Historical comparison reports
describe their measured snapshots, not the current NeoCore build.

## Remaining language design boundaries

`int32` and `float32` are not public scalar types. Compact storage belongs to
`array32<int>`, `array32<float>` and vector components. Mutable collection types
are invariant. Missing map values read as null for reference types and fail for value
types; removal is explicit. Heterogeneous
collections and implicit dynamic fallback remain excluded. Script JSON decoding
uses `jsonDecode<T>` and host decoding uses an explicit registered target rather
than exposing an untyped script object.

Structs have nominal identity and value assignment. Classes, inheritance,
user-defined generics and function overloading remain deferred. Built-in collection
parameters do not imply arbitrary user generics. Registered host schemas support
structs, collections and complete function signatures; retained function values use
the same instance/lifetime rules as script calls. Overload/default-argument metadata
and schema-independent image linking are not implemented. Current images require
matching schema registration order. Native object validation, callback reentry,
suspension and error unwinding remain runtime responsibilities.

## Remaining implementation stages

Host interfaces and compile defines can be exported/imported through
`HostSchemaDocument`. The console accepts `--host` for typed CLI/LSP diagnostics
and `--host-impl` for synchronous, receiver-aware mock functions in the same
program/instance. Compile errors are collected up to a configurable bundle-wide
limit. See [HeadlessHost.md](HeadlessHost.md); engine integration and external-host
DAP execution remain separate work.

1. Expand typed operations to remaining collection reads/writes and
   function argument/result transfers. Preserve the VM dispatch, frame, and
   execution-control architecture.
2. Replace the private textual intermediate representation with direct typed bytecode
   emission and slot-lifetime metadata. Remove destination cleanup only where lifetime
   analysis proves that the slot cannot own a reference.
3. Validate loaded bytecode against type/signature metadata and compare each conversion
   stage with a preserved baseline build and matching results.
4. Extend the editor's semantic symbol model where it currently offers declaration
   scanning and checked built-in signatures. Keep typed compilation as the diagnostic
   authority; do not advertise unsupported LSP operations.
5. Continue validating host aggregates, retained callbacks, JSON, images and coroutine
   lifetime behavior across platforms. Public samples and regressions target NeoCore;
   the original dynamic corpus remains in the NeoScript repository.

The current backend still stores dynamic tags and performs generic work outside the
implemented typed operations. An early numeric-VM snapshot measured on 2026-10-31
found a 7.5% increase in the sum of eight case medians versus generic lowering in
the same executable; see the [historical paired measurement](TypedVmBenchmark.md).
That snapshot predates the current IR and aggregate instructions. The later
[typed IR snapshot](TypedIrBenchmark.md) of commit `fd0e229` measured the sum of
case medians at 75.1% above the archived NeoScript baseline, with `loop_sum` at
1.92× and `map_str` at 7.44×, without a paired generic-op control. The
[three-way snapshot](ThreeWayBenchmark.md) of commit `91d6d58` measured 1.14× the
Lua sum, with `loop_sum` at 1.79× and `map_str` at 6.10× Lua. Local operand
handling, constant localization, and numeric conversion costs require fresh paired
measurements before claiming an improvement or attributing that difference.
Performance claims require paired baseline/trial runs with matching results; inherited NeoScript benchmark
numbers are not NeoCore measurements. Ask the user before starting timed benchmarks.
