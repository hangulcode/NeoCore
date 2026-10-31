# NeoCore for Unity

This package runs statically checked `.nc` through NeoCoreUnity. It preserves
managed script memory, shared programs, independent globals, sleep/time slicing,
and checked native engine IDs.

1. Build with `pwsh Unity/build_windows.ps1` from the repository.
2. Add this package.json with **Package Manager > Add package from disk**.
3. Import Basic, attach NeoCoreDemo, and assign demo.nc to Script.

The .nc importer produces a TextAsset. Basic drives a Transform and reboots after
domain reload. Build scripts also package Android plugins and an iOS static
archive. Close Unity before replacing a loaded plugin.

## Host binding example

```csharp
using (var options = new NeoCore.Unity.CompileOptions())
{
    var actor = options.Object("Actor");
    options.SetResolver((type, id) => actors.ContainsKey(id));
    options.Global("GameObject", actor, actorId);
    options.Property(actor, "Position", ScriptTypes.Vector3,
        call => call.Return(Scalar.From(actors[call.ReceiverId].transform.position)),
        call => {
            using (var value = call.Argument(0))
                actors[call.ReceiverId].transform.position = value.AsVector3();
        });
    program = NeoCore.Unity.Program.Compile(script.text, script.name + ".nc", true, options);
}
instance = program.CreateInstance();
instance.Run().RequireCompleted();
```

The host supplies the actor dictionary and ID. Signatures are fixed before
compilation; object liveness remains a runtime resolver check. Per-instance IDs
can be changed with SetHostObject. Callbacks remain rooted after compile options
are disposed until their final native owner is released.

Calls support scalars, vectors, native handles, typed collections, structs, and fn
callbacks. Declare schema structs with Struct/DefineFields; construct values with
program.CreateValue(typeName, fields). CallLimited/Resume expose budgets and
timeouts. SaveImage/LoadImage preserve checked code. SetModuleLoader supplies .nc
imports; SetHttpTransport supplies HTTP/HTTPS.

## Values and cleanup

Use using/Dispose for programs, instances, options, and owned collection/struct/fn
Scalar values. Structs and vectors copy; collections share identity. Copying a
managed Scalar shares its handle, so dispose it once or Clone for another owner.
Collections can outlive the program wrapper; captured closures are invalidated
when their original instance resets or is destroyed.

Discarded values queue native release; Resources.Drain performs release on the
owning thread and runs automatically before normal instance/value operations.
The editor guard disposes resources before reload and after PlayMode. Player
code must dispose long-lived resources or call Resources.DisposeAll on teardown.
Diagnostics.SetLogSink is process-wide; reload/subsystem hooks detach it.

Scalar.CopyTo/CopyFrom transfer bounded numeric array slices with exact storage
width. program.ParseJson(typeName,text)/ToJson(value) use declared field and element
types. Error results carry source coordinates when debug information is enabled.

See Unity/NeoCore/README.md in the repository for the migration matrix, complete
ownership contract, JSON restrictions, platform commands and test instructions.
This package does not compile legacy dynamic .ns sources.
