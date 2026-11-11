# NeoCore in Unity

Use the typed [Unity package](../Unity/com.neocore.unity) and
[bridge build instructions](../Unity/NeoCore/README.md). Scripts use `.nc`.
The importer, runtime wrappers and native plugin compile through the static
NeoCore frontend; the former NeoScript plugin is a separate product.

## Surface and setup

The package exposes Program, Instance, Result, Scalar, CompileOptions, typed host
schemas and owning value handles. C consumers use
[NeoCoreC.h](../Unity/NeoCore/NeoCoreC.h), whose `Nc*` entry points use opaque
handles and length-delimited UTF-8 strings. Check `NcGetAbiVersion` rather than
assuming the old NeoScript ABI or result layout is unchanged.

The supported boundary includes int64, binary64, bool, strings, null references,
Vector2/3/4, quaternions, registered object IDs, typed collections, structs and
function values. Unity vectors marshal through float32 components. Quaternion
host values use w,x,y,z; the Unity conversion helpers reorder to/from Unity's
constructor layout.

Register native object types, properties, methods and globals before compilation.
`CompileOptions` also supplies module loading and an optional HTTP transport.
Object resolvers validate live IDs at use; do not retain an engine pointer across
script reentry. [HostBindings.md](HostBindings.md) defines the same contract for
C++ and managed adapters.

`CompileOptions.Event(owner, eventName, callbackType, adapter,
registrationMethod = "AddEvent")` declares typed event registration. Build the
callback type with `options.Function(resultType, parameterTypes...)`. For example,
`options.Event(entity, "OnUpdate", options.Function(ScriptTypes.Void,
ScriptTypes.Float), adapter)` accepts
`GameObject.AddEvent("OnUpdate", OnUpdate)` with `void OnUpdate(float dt)`;
`OnUpdate` need not be exported. Other events on the same member can have different
callback signatures. The name must be a string literal; unknown names and wrong
callback types fail compilation. C hosts use `NcSchemaEvent` with an explicit
registration-method name and length. Schema registration returns a binding ID,
or zero on failure; callback userdata ownership transfers only on success.

`CompileOptions.Method(owner, name, result, parameters, modes, adapter)` declares
`out` parameters with one `ParameterMode` per parameter. The adapter writes them
with `call.OutInt(index, value)`, `OutFloat`, `OutBool`, `OutString`, `OutObject`,
`OutNull` or `call.Out(index, scalar)`; scripts call the member with
`out variable`. An unwritten `out` parameter leaves the script variable unchanged.
C hosts use `NcSchemaMethodOut` (a `uint8_t` mode per parameter, or `NULL` for
inputs only) and `NcHostOut*`. These entry points raise the C ABI version to 2;
the managed package checks `NcGetAbiVersion` and must be paired with a plugin
built from the same revision.

Inside the adapter, `call.Argument(1)` returns the owning function value,
`call.CallerInstanceId` identifies the script caller and `call.ReceiverId`
identifies the event receiver. They are separate identities. `Instance.Id`
(`NcInstanceId` in C) lets the host maintain its own live-instance lookup;
`NcHostCallerInstanceId` exposes the callback caller in C. Do not treat either
instance ID as an engine object ID or retain a borrowed `HostCall`. Invoke the
stored function through the original live instance's `Call` overload. Queue or
skip events while it is suspended, then resume the existing invocation. Remove
registrations and dispose retained functions before reset, script replacement or
instance disposal. Reset preserves the instance ID and does not remove host-owned
registrations. Never invoke a newly registered callback before initialization
returns. The C ABI and managed package expose registration and identity helpers,
not an automatic event dispatcher or shared-runtime lookup wrapper.

The Basic sample and managed smoke driver illustrate initialization, exported
calls and transform updates. Build the native plugin before importing/playing:

```powershell
pwsh Unity/NeoCore/build_windows.ps1
pwsh Unity/NeoCore/check_managed.ps1
```

The package has Android and iOS build routes under `Unity`; follow the bridge
instructions for toolchain prerequisites. Native plugin artifacts are build
outputs, not source-controlled binaries.

## Ownership and execution

Programs share code across instances. Each instance keeps its own globals,
object IDs and pending execution. Calls expose completion, suspension and failure;
`RequireCompleted()` rejects both errors and unfinished results. Call/Resume budgets,
cancellation/reset, exported globals, image save/load, collection operations,
typed JSON and retained function calls are exposed through the typed bridge.

Scripts can use `system.jsonEncode(value, sortKeys?)` and
`system.jsonDecode<T>(text)` after `import system;`. Decoding names the static
target type and checks the input shape, including nested fields and collections;
there is no untyped JSON map fallback. Host-side `Program.ParseJson` and
`Program.ToJson` remain available.

For a typed resource loader, declare the method's return type in `CompileOptions`
and call `HostCall.ReturnJson(text, strictIntegers = true)` from its adapter. C
hosts use `NcHostReturnJson(call, bytes, length, strictIntegers)`. The helper
decodes into that declared result type in the calling program. It returns false
and records a script error when parsing or shape validation fails, so the caller
does not continue with a partial result. Strict integer decoding rejects JSON
integer tokens outside int64. Passing `false` permits their conversion to float64,
subject to the declared result type; fractional values never become `int` fields
implicitly. Script-side decoding always uses strict mode.

Dispose owned programs, instances, retained values and options explicitly.
A copied managed Scalar containing an owned handle shares that handle; use
`Clone()` when an independent handle is required. Dispose does not deep-destroy
other shared collection aliases. Runtime resources are not freed from arbitrary
GC finalizer threads. The editor reload guard releases tracked resources before
a domain reload. Do not dispose/reset an executing instance inside its callback;
the API checks active execution.

Callbacks must obey their declared signature. Their argument/return accessors
are not dynamic coercion APIs. Retained closures cannot migrate to another
instance or survive reset as executable callbacks. Named functions may be used
by peer instances of the same compiled program.

The HTTP adapter runs transport work off the script thread. A transport observes
cancellation and returns an owned response; it must not call Unity main-thread
APIs or reenter the VM from its worker callback. Script completion callbacks run
when the instance processes completion. Install a transport explicitly; the core
does not contain an HTTP implementation.

## Validation limits

C ABI smoke and managed P/Invoke tests run without a Unity Editor license.
`Unity/NeoCore/check_unity.ps1` exercises the importer, PlayMode sample and domain
reload in an installed, licensed Editor. A successful native/managed check alone
does not certify PlayMode or every mobile device. See the bridge README for
available platform scripts and observed validation.
