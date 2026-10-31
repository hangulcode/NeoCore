# Unity

The typed NeoCore `.nc` integration is in [NeoCore/](NeoCore/README.md), with its
own [UPM package and Basic sample](com.neocore.unity/README.md). Start with
`pwsh Unity/NeoCore/build_windows.ps1`, then import that package's Basic sample.

The remaining instructions below describe the inherited NeoScript `.ns`
integration. Its C ABI, package, and regression tests remain separate from the
typed plugin. The full legacy write-up is in
**[../docs/Unity.md](../docs/Unity.md)**.

| Path | Contents |
| :--- | :--- |
| `NeoCore/` | Typed scalar C ABI, native build, C smoke and Unity validation |
| `com.neocore.unity/` | Typed `.nc` UPM package and Basic Transform sample |
| `Native/` | The C ABI (`NeoScriptC.h` / `.cpp`) — the C++ facade flattened to plain C |
| `com.neoscript.unity/` | UPM package: C# binding, editor support, tests, sample |
| `build_windows.ps1` | `NeoScript.dll` for the editor and the Win64 player |
| `build_android.ps1` | `libNeoScript.so` (arm64-v8a by default) |
| `build_ios.sh` | `libNeoScriptUnity.a` — **runs on macOS with Xcode only** |

## Quick start

```powershell
pwsh Unity\build_windows.ps1
```

Add the package to the Unity project's `Packages/manifest.json` (paths resolve
against the `Packages` folder):

```json
"com.neoscript.unity": "file:../../path/to/NeoScript/Unity/com.neoscript.unity"
```

## Updating the core and binding

Rebuild the plugin after updating the package sources. The current C# binding
requires C ABI 5 and rejects an old DLL at startup. Imported script APIs now
use `public`; `export` remains the C# host visibility keyword. See the
[migration notes and JSON/property examples](../docs/Unity.md).

ABI 5 rejects disposal while calls or callbacks are active, preserves borrowed
readers across calls on different instances, and validates object-type ownership.
Quaternion helpers convert script w,x,y,z to Unity x,y,z,w; raw Vector4 and
array views keep script order. Rebuild the plugin together with the package.

## Verification

Run all three. Each sees what the others cannot: the smoke test covers the
binding surface, the PlayMode tests cover lifetimes across frame boundaries, and
the reload cycle covers cleanup at the domain boundary.

```powershell
Unity.exe -batchmode -nographics -projectPath <project> `
          -executeMethod NeoScript.Tests.NeoSmokeTest.RunAll -logFile -

Unity.exe -batchmode -projectPath <project> `
          -runTests -testPlatform PlayMode -testResults results.xml

Unity.exe -batchmode -nographics -projectPath <project> `
          -executeMethod NeoScript.Tests.NeoReloadCycleTest.RunAll -logFile -
```

In the editor it is **Tools > NeoScript > Run smoke test**.

Rebuilding the native plugin means **closing the editor**. Releasing the plugin
at runtime was tried and removed; see "Rebuilding the native plugin" in
`docs/Unity.md` for why.

## Worth knowing

The plugin binaries are not in the repository. They are build output and are
gitignored, so run the build scripts above before opening the project. Importer
settings are applied by `NeoPluginImportSettings.cs` rather than committed
`.meta` files, because Unity deletes a `.meta` whose asset is missing and there
is no way to commit the settings alone.
