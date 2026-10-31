# NeoCore for Unity

The Basic sample uses real `.nc` declarations and the typed NeoCore runtime.
It retains the original wobbling Transform, per-frame delta adjustment, and
first-frame log. Unity passes time/deltaTime as `float` script parameters
(binary64), reads the script vector through three scalar exports, and updates
the Transform in C#. This package does not expose the legacy `Unity.*` dynamic
object dispatch.

1. Close Unity, then run `pwsh Unity/NeoCore/build_windows.ps1` from the repository.
2. In Unity 2022.3 or newer on Windows x64, add this folder's `package.json` using
   **Package Manager > Add package from disk**.
3. Import the **Basic** sample from Package Manager.
4. Add `NeoCoreDemo` to a GameObject. Assign the imported `demo.nc` asset to its
   **Script** field and enter Play Mode.

The package's `.nc` importer creates a TextAsset without changing the source
extension. It is an asset importer, not a language server. Compile errors and
runtime errors include the source location when available.

`NeoCore.Unity.Program.Compile` creates checked code. `CreateInstance` creates
independent global state; `Instance.Call` invokes exported functions with
`Scalar` arguments. Dispose instances/programs when finished. The editor guard
also disposes remaining resources before reload, editor exit, and after Play
Mode. The sample boots again after a domain reload or resource cleanup.

The C# adapter currently allocates argument/result objects and resolves exports
by name. It has no execution timeout or instruction budget. Use this sample with
trusted, bounded scripts. Suspension is exposed by the API, but Basic expects
each frame update to complete immediately. See `Unity/NeoCore/README.md` for the
native ownership contract and current limitations.

The inherited `com.neoscript.unity` package, Basic `.ns` demo, and its tests remain
available for legacy backend regression. They do not validate this typed package.
