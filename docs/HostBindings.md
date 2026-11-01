# Typed C++ host bindings

NeoCore resolves registered host members and their signatures before script
execution. Include [`NeoCoreHost.h`](../NeoSource/NeoCoreHost.h), link
the `NeoCore::Runtime` CMake target. Create a public `Runtime` with the bindings
when several programs must share one backend VM. The static `Program::Compile`
overload remains a convenience for an isolated program/runtime.
The frontend fixes signatures before execution; the backend is not a completely
tagless VM.

## Schema and execution connections

`HostSchema` contains nominal object types, global names, method signatures, and
property types with separate read/write permissions. `HostBindings` adds callable
C++ adapters, an object resolver, resolver user data, and initial global IDs.
The schema contains no live object pointers or executable function addresses.

Supported parameter/property types are `int` (int64), `float` (binary64), `bool`,
`string`, `Vector2`, `Vector3`, `Vector4`, `Quaternion`, registered native object
types, typed lists/maps/sets/arrays, structs and function values. Strings, collections,
function values and object types may carry null; value types never do. Methods also
support `void` results. Ordinary methods have one exact signature;
event registration selects a declared callback signature by a literal event name.
Scripts receive ordinary int-to-float widening before the call. The language has
no reference parameters.

Create all schema entries before constructing `HostBindings`. Bind every method,
getter, setter, and global before creating the runtime, even if a source file
does not use them. Failed registration/connection operations return failure and set
`LastError()`; `Validate(error)` checks the complete configuration.

For example, the metadata for a mutable counter can be declared as follows:

```cpp
NeoCore::HostSchema schema;
const NeoCore::TypeId counter = schema.AddObject("Counter");
schema.AddProperty(counter, "Count", schema.Types().Int(), true, true);
const NeoCore::HostBindingId add = schema.AddMethod(
    counter, "Add", schema.Types().Void(), { schema.Types().Int() });
schema.AddGlobal("Counter", counter);
const NeoCore::HostProperty* count = schema.FindProperty(counter, "Count");
```

Check each registration result in production code. `count->getter` and
`count->setter` are separate binding IDs; `0` means that accessor is absent.
Connect both accessors, `add`, and the global through `HostBindings`, then call
`Program::Compile(source, bindings, error, sourceName, debug)`.
The type and global may both be named `Counter`. A user declaration cannot hide
a registered type/global, and scripts cannot instantiate a native type.

The corresponding checked source can use:

```cpp
Counter saved = Counter;
saved.Count = 4;
saved.Add(3);
int previous = saved.Count++;
```

`HostBindings::Bind(id, callback)` connects a `bool(HostCall&)` adapter.
Inside an adapter, use the accessor required by its declared signature:
`Int`, `Float`, `Bool`, `String`, `Vector2`, `Vector3`, `Vector4`, `Quaternion`,
or `Object`/`ObjectId`. For string, collection, function and object parameters,
check `IsNull` before accessing the payload: a script may pass null, and the adapter
decides whether that is acceptable. These accessors are not dynamic
conversions; matching them to the registered signature is the adapter's contract.
`Receiver()` supplies the already-resolved object for the member's owner type.

Use `HostCall::Argument` and `ReturnValue` for owning aggregate/function values.
Declare struct names and fields in `schema.Types()` before binding members;
indirect cycles such as `list<Node>` are supported, while recursive inline struct
values are rejected. Collections retain fixed types and shared identity; structs
copy by value across the host boundary.

Use the matching `Return*` method for a non-void result, or `ReturnNull` for a
reference-typed result that has no object. Return `true` without a result for a void
method/setter. An omitted/wrong result kind, or a null result for a value type, fails
the host call.
`Fail(message)`, a `false` callback result, and thrown C++ exceptions also become
script runtime errors. Object results are IDs; their liveness is checked when
they are subsequently resolved for use.

## Typed event registration

Use `HostSchema::AddEvent(owner, eventName, callbackType, registrationMethod)` to
declare an event whose callback is a function type; a script cannot register a `null` literal. The final argument
defaults to `"AddEvent"`. Each event returns its own binding ID; connect that ID
through `HostBindings::Bind` just like an ordinary method. Registration itself
returns `void` and accepts `(string, callbackType)`. Callback results may have any
supported function return type.

```cpp
const NeoCore::TypeId object = schema.AddObject("GameObjectType");
const NeoCore::TypeId updateType = schema.Types().FunctionOf(
    schema.Types().Void(), { schema.Types().Float() });
const NeoCore::TypeId hitType = schema.Types().FunctionOf(
    schema.Types().Void(), { schema.Types().Int() });
const NeoCore::HostBindingId update = schema.AddEvent(object, "OnUpdate", updateType);
const NeoCore::HostBindingId hit = schema.AddEvent(object, "OnHit", hitType);
schema.AddGlobal("GameObject", object);
```

These signatures accept the original callback-registration style in checked `.nc`:

```cpp
void OnUpdate(float dt) { /* update script state */ }
void OnHit(int damage) { /* handle the event */ }
GameObject.AddEvent("OnUpdate", OnUpdate);
GameObject.AddEvent("OnHit", OnHit);
```

The first argument must be a string literal. Escapes in that literal are decoded
before lookup; event names are case-sensitive and must be nonempty without NUL.
Variables, `const` names and computed strings are not accepted by this specialized
registration path. An unknown event, mismatched callback signature or wrong argument
count fails compilation. Function values and closures of the declared type are
accepted; the callback function does **not** need `export`. `export` is required
for host lookup by function name, not for passing a checked function value.

Different events under one registration method may have different callback types.
Duplicate event names, ordinary methods with the same member name, and conflicting
properties are rejected. General method overloading is still unsupported. For
hosts that intentionally accept dynamic event names with one uniform callback type,
register an ordinary `AddMethod` instead and validate the name in the adapter.
`FindEvent(owner, registrationMethod, eventName)` retrieves a specific event binding;
`FindMethod` alone returns only the first member representative. Saved images include
event names and signatures in their schema compatibility check.

An event adapter receives the literal name in `HostCall::String(0)` and an owning
function value in `HostCall::Argument(1)`. Store that function, the caller's
`CallerInstance().Id()`, and the receiver's `ReceiverId()` separately. The receiver
can be another object or a shared service; it does not identify the callback's
script instance. Store the registration during initialization and dispatch it only
after initialization returns. Resolve the original caller through
`Runtime::FindInstance` and invoke `Instance::Call(function, arguments)` when idle.
Keep a suspended invocation for `Resume`; queue or skip subsequent events according
to the host's policy.

NeoCore does not own an event dispatcher. The host controls duplicate-registration
behavior, removal, event order and object lifetime. Remove registrations and dispose
their function values on reset, script replacement or teardown; an instance ID
survives reset and a named callback could otherwise run against the new state.
Discard registrations whose caller or receiver has been destroyed. Retained
function values keep program data alive, but do not keep an instance or engine
object alive. The [mini game](../Samples/neocore/minigame/README.md) demonstrates
registration, per-instance delivery and cleanup.

## Vector values and property components

Register vector property and method signatures with `schema.Types().Vector2()`,
`Vector3()`, `Vector4()`, or `Quaternion()`. Host adapters use the corresponding
`HostCall` accessor and `ReturnVector2`/`ReturnVector3`/`ReturnVector4`/
`ReturnQuaternion`. These return or accept small owning `Vector2Value`,
`Vector3Value`, `Vector4Value`, and `QuaternionValue` structs with float32 fields.
Vectors use `x, y, z, w` order; quaternions use **`w, x, y, z`**.
Returning a quaternion for a Vector4 signature, or a vector of the wrong
dimension, is a host-contract error even when their storage sizes match.

Vector arguments are copied before any object resolver runs and survive script
reentry. No adapter receives a borrowed pointer into a VM vector. A local script
copy of a host vector is independent from the host property.

For a registered `Position: Vector3`, `object.Position.x = 3.0` evaluates the
right-hand side first, captures the receiver ID once, reads Position, changes
its x component, and writes the resulting Vector3 through the setter. `+=` and
pre/post-increments follow the same path. Both the getter and setter separately
resolve the captured object ID; deletion or type mismatch prevents the next
callback. Component writes require both accessors, while assigning an entire
vector can use a write-only property. Host component references are not supported.

The [vector host regression](../Tests/neocore_vector_host_regression.cpp) provides
a complete schema, value adapters, object-ID resolver, and mutation/error cases.

## Object IDs and ownership

`ObjectHandle` is an opaque unsigned 64-bit ID. `0` denotes null; any object-typed
global, argument or result may carry it. Scripts cannot convert IDs to integers, manufacture
objects, or inspect pointer bits. Compatible native handles support equality.
Script variables copy IDs; they do not own or retain the corresponding C++ object.

The resolver receives `(userData, declaredType, objectId)` and returns a borrowed
pointer or null. It must validate both the object's current identity/liveness and
the requested nominal type. Use a registry/generation scheme that prevents stale
IDs from silently referring to replacement objects. A stored ID is not proof that
the object is still alive.

Before a member adapter runs, NeoCore rejects a null receiver, then resolves the
receiver and all non-null native object arguments. An invalid one fails the call
before the adapter starts, even if the adapter would not have accessed that argument.
Null arguments remain null without calling the resolver. The resolver must be a side-effect-free
lookup: do not reenter scripts, destroy objects/programs, or invalidate pointers
while another argument is being resolved.

`Runtime` snapshots the schema, function connections, and initial global IDs once.
Its programs share that immutable registration set; changing the original
`HostBindings` afterward does not update the runtime. Static `Program::Compile`
creates the equivalent snapshot in its isolated convenience runtime.
The resolver's user-data pointer and any pointers captured by callbacks are not
deep-copied or owned by NeoCore; keep their target registry/data alive for the
lifetime of every instance that can use them. The host also controls object
destruction and ID reuse.

Host globals cannot be rebound from script, but writable object properties can
change. `Instance::SetHostObject(name, id, error)` changes a global ID in that
instance without recompiling or changing its siblings. Set it before `Run` when
file initializers must observe the instance-specific object. The next access reads
the new ID; a script variable that previously copied the global still holds its
previous ID. `Program::SetHostObject` targets only the program's default instance.
Globals accept ID zero; a script then reads the global as null, and a member access
on it fails at runtime. The host validates actual object state when a nonzero ID
is resolved for a later member call.

## Shared runtime, programs and execution instances

The host ownership hierarchy is one `Runtime`, several compiled `Program` objects,
and independent `Instance` objects for each program. `Runtime::Compile` and
`Runtime::LoadImage` use the same backend VM, binding snapshot and execution-context
pool. This avoids allocating a separate backend runtime for every script asset.
Static `Program::Compile` and `Program::LoadImage` remain isolated convenience
entry points; use the runtime methods for engine-wide sharing.

`Program::CreateInstance(error)` creates an independent set of globals, bound
global IDs, and invocation state. Each instance initializes its globals at most
once through `Run`; `Call` initializes them first if necessary. An initialization
failure stays a failure rather than rerunning partially completed side effects.
Instances retain the shared code even if the `Program` factory is destroyed.
Programs and instances retain their shared runtime implementation. They do not
allocate a separate VM per object. The public `Runtime` wrapper is the host's
entry point for looking up live instances and should remain available while the
host has queued events.

`Program::Run`, `Call`, `Resume`, `IsSuspended`, and `SetHostObject` are convenience
operations on one default instance. Explicit instances are suitable for several
objects using the same compiled script. All programs and instances sharing a
runtime must execute serially on one host thread; this interface does not promise
parallel VM access.

The runtime fixes its binding snapshot and HTTP transport at creation. Supply
module loaders, definitions and assembly output options to each compile call;
runtime creation does not establish defaults for those options. Compilation,
image loading and image saving are rejected during execution in that runtime.
Perform them between host execution calls, including while instances are suspended.

Shared allocation does not make program-local types or function indices
interchangeable. Runtime-owned collections, structs and functions still cannot
be passed between different programs, even in the same runtime. A host may copy
plain scalar/vector values or reconstruct data under the destination program's
declared type. Cross-program calls and exported-value exchange need an explicit
typed host contract.

`Instance::Call` and `Resume` return `Result` with `ExecutionStatus::Completed`,
`Suspended`, or `Failed`. `ok()` is true for both successful completion and a
successful suspension; use `suspended()` or `Instance::IsSuspended()` to decide
whether another host frame must resume execution. `sleep(0)` suspends once rather
than running the continuation inside the current call. Positive sleep uses elapsed
real time and takes integer milliseconds. Negative sleep durations clamp to zero;
durations above `INT32_MAX` milliseconds fail at runtime. `Resume(deltaTime)` accepts
a finite, nonnegative duration within the float32 range, but that parameter is currently reserved and does not advance the
sleep timer. A resume may suspend again or return the completed typed result.

One instance owns one pending execution. Calling a new exported function while it
is suspended fails without replacing the pending call. Queue such events in the
host until the instance is idle. If automatic file initialization suspends during
`Call`, the instance retains that original function and its owned argument values.
`Resume` completes initialization and then starts the retained call; do not submit
it again. This does not queue later calls made while the instance is busy.
Destroying a suspended instance abandons its own pending execution and leaves
other instances intact.

See the [mini game host](../Samples/neocore/minigame/README.md) for one program
shared by three instances, frame/event dispatch, callback reentry, suspension, and
object destruction between sleep and resume.

## Reentry, borrowing, and execution order

`HostCall::CallerInstance()` identifies the instance executing the callback.
It is independent of `ReceiverId()`: a script can call a method on another object
or on a shared service. `Instance::SetUserData`/`GetUserData` associate a non-owning
host context with that instance; `HostCall::InstanceUserData()` reads the same
context. In contrast, `HostCall::UserData()` is the shared resolver/binding context.
NeoCore neither owns nor frees either user-data pointer.

For delayed events, retain the caller's `Id()` and an owning function value from
`HostCall::Argument`, rather than a `HostCall` or an `Instance*`. On delivery,
look the ID up with `Runtime::FindInstance`, discard an event whose caller no
longer exists, and invoke the value on that resolved instance through `Call`.
The returned instance pointer is borrowed only for the current synchronous host
operation. A live instance can be suspended; queue the event until it becomes
idle. An instance keeps its ID across `Reset`; the host must cancel queued events
that belong to the previous script state. Reset invalidates captured closures,
so a retained callback can still fail even when the host finds an instance.
Treat that failure as an expired callback instead of redirecting it to another
instance. Neither the caller ID nor its user-data pointer pins an engine object;
continue resolving engine object IDs separately on each access.

`HostCall` copies scalar/string/vector arguments and object IDs before resolver callbacks
run. These snapshots survive synchronous script reentry. Object pointers returned
by `Receiver`/`Object` are borrowed only until reentry or an external lifetime
change; they are not pinned resources.

Use `HostCall::CallScript(name, arguments)` for a synchronous nested call. It uses
the checked `Instance::Call` boundary of the invoking instance. Typed native,
null and aggregate arguments follow their registered signatures. A shared service callback
therefore reenters its caller's instance. Reentry while that instance's top-level
initialization is incomplete is rejected. It invalidates the host call's cached object
pointers before and after execution. Reacquire them through `Receiver`/`Object`
after a successful nested call. A nested failure is sticky: ignoring its `Result`
and returning `true` cannot turn the outer host call into success.

Ordinary script calls during top-level initialization are checked separately:
the compiler rejects a call that can reach a read or write of a file variable
before its initializer completes, including through recursive/transitive calls.
Host globals are already bound before initialization and can be used there, but
their callbacks cannot reenter script code until initialization finishes.
A failed top-level initialization is remembered and is not executed again.

If another host callback or deletion path changes object lifetime without going
through `CallScript`, use `ResolveReceiver`/`ResolveObject` to force a new lookup.
Check returned pointers before dereferencing them. Never continue using an old
local C++ pointer merely because the wrapper's cache was invalidated. Do not
destroy the executing `Instance` (or a `Program` owning its executing default
instance), and do not invoke instances sharing the runtime concurrently from
another thread. Retaining a `HostCall`, borrowed pointer, or argument reference for later
asynchronous execution is unsupported. Retain an owning function value instead
and invoke it through the checked instance/host-call API. Closures remain bound to
their creating instance and are rejected after reset or in a peer instance;
named function values can execute in peers sharing the same program.

A property read invokes its getter. Assignment first evaluates the right-hand
side, then the entire target receiver path once. Compound assignment then reads
the property, computes the result, and calls its setter. Pre/post-increments keep
their usual result values. Nested property paths preserve these rules.
Read-only/write-only misuse is rejected during compilation.

## Current implementation limits

The frontend fixes member IDs at compilation, and linking chooses typed argument
readers plus fixed callback connections. Execution does not search the object's
member table by name. The transitional backend still uses private binding objects,
generic VM call frames/value slots, and generated property-write helpers. It is
not fully tagless. With debug metadata enabled, generated property-helper source
locations map back to the original property operation, while internal helper
frames may still appear in the call stack. Null-receiver checks, result validation, and lifetime
resolution remain intentional runtime checks. This milestone adds no benchmark
claim.

One source-location limitation remains in the textual intermediate representation:
when an assignment spans multiple lines, a failure while evaluating its receiver
path can report the right-hand side's line instead of the receiver's original line.
For example, if a registered `Target()` method fails in
`Counter.Target().Count =` followed by `2;` on the next line, the diagnostic can
point to that next line. This also applies to compound assignments. Generated
getter/setter-helper failures still map to the property operation's original line;
the receiver-path limitation does not change evaluation order or error propagation.

The [host regression](../Tests/neocore_host_regression.cpp) exercises complete
connections, property effects, ID resolution, and synchronous reentry;
the [frontend regression](../Tests/neocore_host_frontend.cpp) covers accepted and
rejected binding syntax.

Native locals, parameters, properties and results can be null. The compiler does not
require a guard; a null check is an ordinary `if`, and an unguarded member access on
a null value fails at runtime. A nonzero ID is not proof that the object is alive;
each host callback still resolves it.

```cpp
void UpdateParent()
{
    GameObject pr = GameObject.Parent;
    if (pr != null) {
        pr.Position.x = 3.0;
    }
}
```

Here the host must register the `GameObject` type/global, the `Parent` object
property, and a read/write `Position: Vector3` property. `pr` keeps the returned parent ID even
if the parent relationship later changes. The saved parent object is resolved
from that saved ID rather than by reevaluating `GameObject.Parent`.

Typed structs, collections and retained function values use owning boundary
values. [Unity.md](Unity.md) describes the corresponding C ABI and managed
adapters. Overloads, default arguments and general asynchronous script reentry
remain unsupported. Basic sleep and host-driven resume use the instance API.

For an embedded editor, include `NeoCoreDebugger.h` and obtain the runtime-owned
debugger through `Runtime::GetDebugger()`. It attaches to typed `Instance` objects,
uses original `.nc` source locations, and exposes typed stack/variable snapshots.
See [Embedding.md](Embedding.md#embedded-debugging) for execution and lifetime rules;
hosts do not need backend worker handles or the private `DebugAccess` interface.

Type and binding IDs are local to their schema snapshot. Serialized program
images include the schema and validate the supplied host registrations at load;
the current format requires matching registration order. Callbacks, resolvers,
object IDs and transports are reattached rather than serialized. Do not persist
live pointers or treat IDs as stable across independently constructed schemas.

## Host migration responsibilities

Export registered types, members, globals and compile defines with
`HostSchemaDocument` from `NeoCoreHostSchemaIO.h`. The console accepts the exported
schema for checking, language diagnostics and optional `.nc` mock implementations.
See [HeadlessHost.md](HeadlessHost.md) for the format, CLI workflow, receiver and
object-lifetime rules, and the bounded `Error::diagnostics` list.

- Create one runtime from a complete, fixed schema and binding set. Compile each
  script asset through that runtime and cache its program.
- Create instances for individual script attachments. Set their host context and
  global object IDs before running file initialization.
- Keep engine object registries, ID generation checks and capability checks such
  as UI membership in the host resolver/adapters. Static signatures do not prove
  that an object still exists or has the required current state.
- Replace dynamically shaped arguments/results with exact typed contracts, such
  as separate methods or declared result structs. Optional arguments, overloads
  and arbitrary cross-program aggregate transfers need explicit adaptation.
- Drive frame calls and resume pending execution from the host. Queue delayed
  callbacks by caller instance ID and owning function value; resolve the ID at
  delivery and cancel old queued events on reset or script replacement.
- Connect editor breakpoints, stack inspection and stepping through the public
  debugger. The editor still drives execution through the instance API.
