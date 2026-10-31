# NeoCore for Unity

The integration compiles typed `.nc` through NeoCore. Host methods, properties,
native object IDs, functions, collections, vectors, and structs cross checked
static signatures. There is no dynamic `.ns` fallback.

| Path | Contents |
| :--- | :--- |
| [NeoCore](NeoCore/README.md) | C ABI, native builds, migration coverage, verification |
| [com.neocore.unity](com.neocore.unity/README.md) | UPM package, importer, Basic sample |

Build Windows x64 with `pwsh Unity/build_windows.ps1`, then add
`Unity/com.neocore.unity/package.json` through Package Manager's **Add package
from disk**. Import Basic, attach NeoCoreDemo to a GameObject, and assign its
`demo.nc` TextAsset. Close Unity before replacing a loaded native plugin.

Android uses `pwsh Unity/build_android.ps1 -Abi arm64-v8a`. iOS uses
`bash Unity/build_ios.sh` on macOS/Xcode. Android has 16 KiB page alignment;
the iOS archive links through `__Internal`. Plugin binaries are not committed.

See [the bridge guide](NeoCore/README.md) for ownership, execution controls,
typed JSON, migration differences, and verification. The original NeoScript
repository retains the dynamic integration; this checkout ships the NeoCore
package only.
