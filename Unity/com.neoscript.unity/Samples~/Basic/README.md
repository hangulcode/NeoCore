# Legacy NeoScript Basic sample

`NeoScriptDemo.cs` is retained for the inherited `.ns` package and its regression
tests. The migrated typed sample is in `Unity/com.neocore.unity/Samples~/Basic`.
Build it with `pwsh Unity/NeoCore/build_windows.ps1`, add the `com.neocore.unity`
package, and import its Basic sample. The typed package compiles `demo.nc` using
NeoCore's checked frontend and dedicated backend profile.
