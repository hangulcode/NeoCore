# NeoCore host bridge

`NeoCoreC.h` exposes checked `.nc` compilation and the shared-program,
independent-instance runtime to C and C#. It uses opaque owned handles and an
ABI version handshake. The backend remains C++11; the consumer header is C99.

## Builds and verification

| Platform | Command | Package output |
| :--- | :--- | :--- |
| Windows x64 | `pwsh Unity/build_windows.ps1` | `Runtime/Plugins/x86_64/NeoCoreUnity.dll` |
| Android | `pwsh Unity/build_android.ps1 -Abi arm64-v8a` | `Runtime/Plugins/Android/arm64-v8a/libNeoCoreUnity.so` |
| iOS device | `bash Unity/build_ios.sh` | `Runtime/Plugins/iOS/libNeoCoreUnity.a` |
| iOS simulator | `bash Unity/build_ios.sh simulator` | Same package path, replacing the device archive |

Android also accepts armeabi-v7a and x86_64, exports only C entry points, and uses
16 KiB alignment. iOS merges the typed bridge/compiler/runtime/backend archives;
managed players use `__Internal`. Build iOS on macOS/Xcode and keep device and
simulator archives separate. Deployment scripts copy licenses and target
`com.neocore.unity`. Close Unity before replacing loaded plugin binaries.

Desktop CMake needs `NEOCORE_BUILD_UNITY_BRIDGE=ON` and
`NEOCORE_BUILD_TESTS=ON`. Build, then run
`ctest --test-dir Build/neocore -C Release --output-on-failure`.
C smoke, boundary, and integration checks cover owned results, signatures,
callbacks, collections, images, modules, and HTTP cancellation.

`pwsh Unity/NeoCore/check_managed.ps1` runs managed P/Invoke tests. Installed
Unity assemblies provide real vector/AOT types. Without an Editor, the standalone
harness uses test-only substitutes and explicitly skips Editor compilation.
`-UnityManaged <Editor/Data/Managed>` chooses an installation. This does not run
Unity PlayMode or an IL2CPP player.

`pwsh Unity/NeoCore/check_unity.ps1 -Unity <Unity.exe>` tests the importer,
Basic sample, PlayMode and domain reload in a temporary Build project. The package
also has Editor and PlayMode test assemblies. Native cross-compilation is distinct
from testing a deployed Android/iOS player.
Add `"testables": ["com.neocore.unity"]` to the host manifest and use Unity Test
Runner's EditMode/PlayMode tabs to run package tests.

## Ownership and static contracts

Every successful creation, compile/load, value capture, and result operation
returns an owned handle with a matching destroy function. Destroy accepts null.
A null result indicates allocation failure. UTF-8 result/error/source pointers
remain valid until their result is destroyed; input text is copied. Strings and
map keys preserve embedded NUL bytes.

Instances retain programs. Owned collections retain the runtime and survive
instance reset and public program/instance destruction. Collections preserve
shared identity; structs and vectors copy by value. Hosts declare exact static
signatures before compilation. Boundary argument checks run before script side
effects. Int-to-float widening is allowed; vector dimensions and Vector4 versus
Quaternion must match exactly.

Native objects carry nominal types and 64-bit IDs. A resolver validates each
access; borrowed engine pointers must not survive reentry. Retained named
functions belong to their program. Captured closures belong to their original
instance; reset/destruction invalidates them, including nested closures in
collections. Cross-program value transfer is rejected.

Use all instances/values of one program sequentially on one owning thread.
Dispose managed Program, Instance, CompileOptions and owned Scalar results.
Managed Scalar copies share a handle; disposing one invalidates the copies.
Clone obtains another handle, copies structs and preserves collection identity.

Dropped managed values queue native release from their finalizer.
Resources.Drain releases only on the creating thread, automatically before normal
instance/value operations. No VM operation runs on the GC finalizer thread.
Resources.DisposeAll handles remaining resources during editor reload or player
teardown. Do not abandon an owning worker thread with queued releases.

## Migration coverage

| Earlier capability | Typed NeoCore equivalent |
| :--- | :--- |
| Shared programs, independent instance globals | Program.Compile, CreateInstance, Run, Call |
| Methods, properties, per-instance native objects | CompileOptions schema, callbacks/resolver, SetHostObject |
| Instance binding hook and opaque user data | SetHostObject before Run; host state keyed by receiver ID |
| Scalars and vectors | Scalar, int64/binary64, Vector2/3/4, Quaternion |
| Heterogeneous object-shaped maps | Struct/DefineFields and CreateValue(type, fields) |
| Lists/maps/sets/dense and compact arrays | Owned typed values, checked mutations, identity, reserve/resize |
| Raw numeric array views | Bounded CopyTo/CopyFrom with int64/double/int32/float32 storage |
| Script callbacks and reentry | Owned fn values, HostCall.Call, Instance.Call(function) |
| Exported globals | ReadGlobal/WriteGlobal, including boxed vector storage |
| Imports | Copied .nc sources through SetModuleLoader |
| Compile-time definitions | DefineInt/Float/Bool/String/Null/Identifier, checked token replacements |
| Bytecode images | SaveImage/LoadImage with descriptors and schema validation |
| Sleep, time slicing, timeout | CallLimited, Resume, IsSuspended, Cancel, Reset |
| HTTP/HTTPS | Explicit host transport; HttpClientTransport helper |
| JSON | Declared-type ParseJson and ToJson with named struct fields |
| GC, pools, diagnostics | CollectCycles, TrimMemory, pool settings, AllocationStat |
| Logging | Process-wide Diagnostics.SetLogSink/ClearLogSink |
| Importer/AOT/reload | .nc TextAsset importer, link.xml, AOT callbacks, reload/subsystem guards |

Static equivalents replace dynamic compatibility: objects with differently typed
fields use structs; loading source means compiling another checked program or
loading its typed image. Failed calls return Result rather than a dynamic pCall.
Register callbacks before compilation. Image loading requires the matching
schema and fresh bindings; images never store native pointers or live engine IDs.

List indexed removal is supported. Arrays expose replacement, append, resize,
clear, reserve and bulk copies; they do not provide list removal. Bulk operations
require exact storage width and validate the complete slice before mutation.
No borrowed Span can outlive a resize/reentry. Collection sizes are checked
against representable backend limits; no additional memory cap is imposed.
Bool and vector arrays use checked per-element At/Set; numeric arrays additionally
support bulk copies. Per-instance objects must be bound before the first Run or
Call, because Call initializes the script automatically.

JSON objects require map<string, T> or exact struct fields. Unknown/missing fields
fail. Numeric conversion widens int to float and never truncates float to int.
Vector/compact-float narrowing rejects nonfinite or overflowing results. Vector
JSON is an array; Quaternion order is w,x,y,z. Native objects, functions, cyclic
graphs and nesting beyond 128 levels are rejected.
The inherited JSON codec rejects embedded NUL and invalid UTF-8 in strings and
keys. Compile-time string definitions also reject NUL because source literals
cannot represent it. Length-aware host arguments/results retain NUL unchanged.

Cancel/Reset cannot interrupt an executing callback. Reset discards that instance's
outstanding callback state; HTTP may finish in the background. Runtime destruction
signals transport cancellation and waits for it to return. Custom transports must
observe cancellation. HTTP callbacks run on workers and must not access Unity
objects or the VM. The VM cannot forcibly terminate arbitrary host code.

The original NeoScript repository retains its dynamic bridge and Unity package.
This checkout contains the typed NeoCore integration only.
