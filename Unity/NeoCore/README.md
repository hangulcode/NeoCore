# NeoCore scalar bridge

This bridge compiles `.nc` through `NeoCore::Program` and links only
`NeoCore::Runtime` and its `NEOSCRIPT_LEGACY_ARITHMETIC=0` backend.
`Unity/Native` and `com.neoscript.unity` remain the separate legacy integration.

Build and run the C ABI check on Windows:

```powershell
pwsh Unity/NeoCore/build_windows.ps1
```

The script builds `NeoCoreUnity.dll`, runs the smoke executable compiled as C,
and copies the plugin and licenses into `com.neocore.unity`. Close Unity before
rebuilding its loaded plugin. Native binaries are local build output.

For other CMake hosts, configure `-DNEOCORE_BUILD_UNITY_BRIDGE=ON`, build
`NeoCoreUnity` and `neocore_unity_c_smoke`, then run
`ctest --test-dir <build> -C Release -R neocore_unity_c_smoke --output-on-failure`.
Only the Windows x64 Unity deployment script and importer are provided here;
the bridge itself is portable C++11 and its consumer header is C99.

To exercise the actual Unity package, importer, PlayMode sample, and a script
domain reload in a fresh project under `Build`, run:

```powershell
pwsh Unity/NeoCore/check_unity.ps1 -Unity 'C:\Program Files\Unity\Hub\Editor\2022.3.62f3\Editor\Unity.exe'
```

The check copies the package into the temporary project's embedded packages;
Unity-generated metadata and test scenes stay in that project. Its log is
`Build/neocore-unity-smoke/smoke.log`.

An installed .NET SDK can also run the real managed P/Invoke bridge independently
of an Editor license. Supplying the installed Editor's managed-assembly folder
additionally compiles the importer, reload guard, sample, and Unity smoke driver
against its actual Unity APIs:

```powershell
pwsh Unity/NeoCore/check_managed.ps1 -UnityManaged 'C:\Program Files\Unity\Hub\Editor\2022.3.62f3\Editor\Data\Managed'
```

This fallback does not claim to run PlayMode or the Unity asset importer.

`NeoCoreC.h` uses opaque owned program, instance, argument, and result handles.
Each successful creation has a matching destroy call; destroy accepts null.
`NcCompile` and `NcInstanceCreate` always return an owned result through their
output parameter, except when result allocation itself fails. Execution returns
an owned result with Completed, Suspended, or Failed status. A null result means
allocation failure. Copy UTF-8 result/error/source bytes before destroying the
result; their pointer and length are valid only for that result's lifetime.
Input strings are copied and may be released when the call returns.

Instances retain their compiled program and remain usable after its public
program handle is destroyed. Instances of one program must run sequentially on
one thread. Do not destroy a handle while it is executing or reuse it afterward.
The managed wrapper keeps live resources until explicitly disposed or cleared
by its editor reload guard; it does not invoke the single-threaded runtime from
GC finalizers. Use `using` or `Dispose` in player code.

Supported calls have scalar arguments and scalar/void results: int64, binary64,
bool, UTF-8 string, and nullable scalar results. The underlying public API rejects
null arguments explicitly, even for nullable parameters. Argument count/type
checks happen before execution. `Run` and `Resume` preserve suspension status;
`RequireCompleted` throws for both failure and unfinished execution.

This is a narrow typed adapter, not the full inherited Unity API. Host schemas,
native objects, vector/collection marshaling, timeouts, instruction budgets,
cancel/reset, and mobile packaging are not exposed here. The Basic sample keeps
its vector inside typed script code and reads its components through float
exports. There is no dynamic compilation fallback or offline image conversion.
