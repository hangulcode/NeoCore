# NeoCore mini game host

This console sample connects a checked `.nc` script to a small C++ world. It runs
without graphics or timers and verifies shared compilation, independent script
instances, frame/event dispatch, suspension, and object-ID validation.

## Build and run

From the repository root:

```sh
cmake -S . -B Build/neocore -DNEOSCRIPT_BUILD_EXAMPLES=OFF
cmake --build Build/neocore --config Release --target neocore_minigame
ctest --test-dir Build/neocore -C Release -R '^neocore_minigame$' --output-on-failure
```

The sample is included when `NEOSCRIPT_BUILD_SMOKE` or `NEOSCRIPT_BUILD_EXAMPLES`
is enabled. With the Visual Studio CMake generator, run:

```powershell
Build/neocore/bin/Release/neocore_minigame.exe --verify
```

For the checked-in Windows projects, open [NeoCore.sln](../../../NeoCore.sln)
and select `NeoCoreMiniGame` as the startup project, or build its project directly:

```bat
MSBuild Samples\neocore\minigame\neocore_minigame.vcxproj /p:Configuration=Release /p:Platform=x64 /m
Build\Windows\bin\x64\Release\neocore_minigame.exe --verify
```

The native project references `Windows/NeoCore.vcxproj`; it does not compile a
second copy of the VM. Debug/Release and x86/x64 use the same CRT and library rules
as the NeoCore console.

The build defines `NEOCORE_MINIGAME_SCRIPT` as the absolute source path to
[scripts/mover.nc](scripts/mover.nc), so launching from another working directory
still finds it. To use another script with the same exported interface, pass
`--file path/to/mover.nc`. `--verify` is optional; both forms run the checks.

Successful execution prints:

```text
NeoCore minigame: all checks passed (1 program, 3 instances, frame/event/sleep/ID lifetime)
```

## One program, several instances

[main.cpp](main.cpp) declares `GameObject` and `Services` in a `HostSchema`, then
connects their members with `HostBindings`. Compilation resolves the member names,
argument types, and result types before execution. Registration selects each
concrete member callback once; execution has no member-name switch.

`Program::Compile(source, bindings, error, sourceName, true)` compiles once.
`Program::CreateInstance(error)` creates each independent execution state. Before
`Instance::Run()` initializes file globals, the host calls
`Instance::SetHostObject("GameObject", id, error)` for that instance. Consequently,
`origin`, counters, speed, and saved script variables are independent even though
all three instances share one compiled program and one service implementation.
The instances retain the compiled program after the `Program` factory is destroyed.
Host resolver/callback user data still belongs to the host and must remain alive.
The new `Instance` API wraps the existing backend Runtime/Program/Worker model and
shared execution-context pool used by the inherited host sample; it does not
create a separate VM or runtime for each game object.

## Frame calls, events, and suspension

The host drives exported `OnUpdate(float dt)` and `OnEvent(int strength)` through
`Instance::Call`. `DelayedEvent()` saves an object handle and calls `sleep(0)`.
Even zero-duration sleep yields execution: `Result::suspended()` and
`Instance::IsSuspended()` report the pending invocation, and the next host frame
uses `Instance::Resume(dt)` to continue it.

The sample resumes a suspended instance instead of also dispatching `OnUpdate`
in that frame. A call submitted while another invocation is suspended fails and
does not replace it. An engine should retain such events in its own queue and
submit them after the instance becomes idle. This sample verifies busy-call
rejection; it does not implement an event queue. For a longer sleep, `Resume` may
return another suspended result, so the host must continue driving later frames.
Sleep takes integer milliseconds and uses elapsed real time; the reserved
`Resume(deltaTime)` parameter does not advance its timer. All sample instances run serially on the host thread.

`Services.Record` calls `HostCall::CallScript("UpdateCount")` from inside its
callback. The nested call reads the invoking instance's counter, demonstrating
that a shared service does not accidentally enter another instance.

## Object lifetime remains a runtime check

The host stores object IDs in scripts, never borrowed C++ object pointers. Every
property access resolves the ID again through the host resolver. A compound
property write resolves its receiver for the getter and setter independently.
Compilation fixes the signature, not whether that particular object is still alive.

The sample destroys an object during suspension, creates another with a fresh ID,
and rebinds the instance's `GameObject` global. The saved `target` local still holds
the old ID: accessing it after resume reports a script error with its source
location instead of touching the replacement object. Destroying a suspended script
instance also leaves its neighbors' counters and invocation state intact.

The complete host binding contract is in [HostBindings.md](../../../docs/HostBindings.md).
