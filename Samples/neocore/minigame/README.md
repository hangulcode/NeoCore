# NeoCore mini game host

This console sample connects a checked `.nc` script to a small C++ world. It runs
without graphics or timers and verifies shared compilation, independent script
instances, frame/event dispatch, suspension, and object-ID validation.

## Build and run

From the repository root:

```sh
cmake -S . -B Build/neocore -DNEOCORE_BUILD_EXAMPLES=OFF
cmake --build Build/neocore --config Release --target neocore_minigame
ctest --test-dir Build/neocore -C Release -R '^neocore_minigame$' --output-on-failure
```

The sample is included when `NEOCORE_BUILD_TESTS` or `NEOCORE_BUILD_EXAMPLES`
is enabled. With the Visual Studio CMake generator, run:

```powershell
Build/neocore/bin/Release/neocore_minigame.exe --verify
```

For the checked-in Windows projects, open
[neocore_minigame.sln](neocore_minigame.sln), which includes both the mini game and
its NeoCore library dependency. Alternatively, open [NeoCore.sln](../../../NeoCore.sln)
and select `NeoCoreMiniGame` as the startup project. Both solutions map Debug/Release
and x86/x64 to matching library configurations. Opening only the `.vcxproj` does
not load the library into Visual Studio's solution build; use the `.sln` so the
library is rebuilt when needed. Command-line MSBuild can also build the project directly:

```bat
MSBuild Samples\neocore\minigame\neocore_minigame.vcxproj /p:Configuration=Release /p:Platform=x64 /m
Build\Windows\bin\x64\Release\neocore_minigame.exe --verify
```

The native project references `Windows/NeoCore.vcxproj`; it does not compile a
second copy of the VM. Debug/Release and x86/x64 use the same CRT and library rules
as the NeoCore console.

The build defines `NEOCORE_MINIGAME_SCRIPT` as the absolute source path to
[scripts/mover.nc](scripts/mover.nc), so launching from another working directory
still finds it. To use another script with the same events and exported queries, pass
`--file path/to/mover.nc`. `--verify` is optional; both forms run the checks.

Successful execution prints:

```text
NeoCore minigame: all checks passed (1 program, 3 instances, frame/event/sleep/ID lifetime)
```

## Original motion and offline replay

The second mode ports the inherited two-object motion sample to
[scripts/replay.nc](scripts/replay.nc). It runs 120 fixed 1/60-second simulation
steps without timers. Red and Blue share one checked program and keep separate
origins, elapsed time, speeds, update counters, and function values.

```powershell
Build/neocore/bin/Release/neocore_minigame.exe --motion --verify --record Build/neocore/bin/Release/scene-data.js
```

Open `Build/neocore/bin/Release/viewer.html` to play, pause, or scrub the 121
recorded frames, including the initial state. Both CMake and the native Windows
project copy the offline viewer beside the executable. The recording uses the
inherited viewer format, `window.neoScene`, with `fps: 60` and two Vector3 positions
per frame. The browser replays recorded native positions; it does not execute
NeoCore. Recordings are generated artifacts and are not committed.

`--record` also selects motion mode. `--motion --file path/to/replay.nc` selects a
different typed script. Omit `--verify` after intentionally changing its motion.
Verification compares every frame with an independent C++ scalar calculation,
checks both update counters and four log messages, and checks the original final
positions. It prints `NeoCore motion: all checks passed` on success.

The typed script uses the original registration shape:

```nc
void OnUpdate(float dt) { /* update this instance */ }
this.AddEvent("OnUpdate", OnUpdate);
```

The host declares `OnUpdate` with `HostSchema::AddEvent` and a
`fn(float)->void` callback type. The compiler selects that event's binding and
checks the callback signature before execution. C++ retains the function as a
`Scalar` together with `HostCall::CallerInstance().Id()`. Each frame it finds that
instance through the shared `Runtime` and calls `Instance::Call(callback, args)`.
The callback is private: `export` is needed only for host lookup by function name,
such as the sample's `UpdateCount()` query. The host validates object IDs, rejects
nonfinite position writes, and applies an instruction budget to each update.

Run both mini game checks with:

```sh
ctest --test-dir Build/neocore -C Release -R '^neocore_minigame' --output-on-failure
```

## One program, several instances

[main.cpp](main.cpp) declares `GameObject` and `Services` in a `HostSchema`, then
connects their members with `HostBindings`. The owning `GameObject` of each instance
is exposed to its script as the global `this`; `Services` is shared. Compilation resolves the member names,
argument types, and result types before execution. Registration selects each
concrete member callback once; execution has no member-name switch.

`Runtime::Create(bindings, error)` creates one shared runtime with fixed bindings.
`Runtime::Compile(source, error, sourceName, true)` compiles once.
`Program::CreateInstance(error)` creates each independent execution state. Before
`Instance::Run()` initializes file globals, the host calls
`Instance::SetHostObject("this", id, error)` for that instance. Consequently,
`origin`, counters, speed, and saved script variables are independent even though
all three instances share one compiled program and one service implementation.
The instances retain the compiled program after the `Program` factory is destroyed.
Host resolver/callback user data still belongs to the host and must remain alive.
The new `Instance` API wraps the existing backend Runtime/Program/Worker model and
shared execution-context pool used by the inherited host sample; it does not
create a separate VM or runtime for each game object.

## Frame calls, events, and suspension

The script registers private `OnUpdate(float dt)`, `OnEvent(int strength)`, and
`DelayedEvent()` functions with `this.AddEvent`. Their signatures are
declared separately in the host schema, so an event name or callback signature
mismatch is a compile error. Event names in these calls must be string literals;
the language does not assume particular names such as `OnUpdate`.

The host stores the receiver object's ID separately from the registering script
instance's ID. An event on another object still invokes the original script
instance and its own globals. Dispatch copies the stored callback before calling
it, so a callback can replace its registration without invalidating that call.
Registration only stores the callback; it does not invoke it during initialization.
This sample keeps one callback per receiver/event pair, replacing a previous
registration of the same pair. Hosts can implement multiple subscribers separately.

`DelayedEvent()` saves an object handle and calls `sleep(0)`.
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

`World::CancelEvents(instanceId)` releases registrations before resetting or
destroying an instance. `Reset()` preserves its public ID, so ID lookup alone
cannot distinguish callbacks registered before reset. After reset, the host binds
the instance's object and runs initialization to register fresh callbacks. Object
destruction also removes its registrations. Stored function values keep their
program and captures alive, but do not keep their caller instance alive;
`Runtime::FindInstance` returns null after that instance is destroyed.

`Services.Record` calls `HostCall::CallScript("UpdateCount")` from inside its
callback. The nested call reads the invoking instance's counter, demonstrating
that a shared service does not accidentally enter another instance.

## Object lifetime remains a runtime check

The host stores object IDs in scripts, never borrowed C++ object pointers. Every
property access resolves the ID again through the host resolver. A compound
property write resolves its receiver for the getter and setter independently.
Compilation fixes the signature, not whether that particular object is still alive.

The sample destroys an object during suspension, creates another with a fresh ID,
and rebinds the instance's `this` global. The saved `target` local still holds
the old ID: accessing it after resume reports a script error with its source
location instead of touching the replacement object. Destroying a suspended script
instance also leaves its neighbors' counters and invocation state intact.
Additional checks register an event on a different receiver, verify that it runs
in the registering instance, then cancel/reset/re-register and destroy that instance.

The complete host binding contract is in [HostBindings.md](../../../docs/HostBindings.md).
