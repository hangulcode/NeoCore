# Host schemas and headless implementations

The engine exports its registered static interface; the console uses the same
interface to check `.nc` files without loading the engine. Optional script
implementations provide deterministic substitutes for selected host members.
Neither workflow changes the engine's real bindings, object lifetime checks or
per-instance execution model.

## Export the registered interface

Include `NeoCoreHostSchemaIO.h` in the embedding host:

```cpp
NeoCore::HostSchemaDocument document;
document.schema = registeredSchema;
document.defines = compileOptions.defines;
std::string error;
if (!document.SaveFile("host.json", error)) {
    ReportError(error);
}
```

`HostSchemaDocument::ToJson` / `FromJson` operate on strings; `SaveFile` /
`LoadFile` use UTF-8 paths. Failed reads leave the output document unchanged.
The versioned JSON includes native types, structs and field defaults, nested
collection/function/coroutine types, methods, readable/writable properties,
event signatures, globals and typed compile defines. Integer defaults retain
all 64 bits; floating defaults retain their bit patterns, including signed zero.
Registration order and schema-local IDs are preserved. Resolver callbacks,
implementations, live objects, object IDs and file paths are not serialized.
Export again whenever the registered interface or compile defines change.

## Check files or use language diagnostics

```sh
neocore --host host.json --check scripts/main.nc
neocore --host host.json --max-errors 100 --lsp
```

Both modes use the typed compiler and imported modules. `--check` also compiles
the backend but does not run file initialization. Schema-only `--file` runs
ordinary script code; the first accessed host member without an implementation
fails with its name, for example `no host implementation: Services.Log`.
Host globals are symbolic objects in this mode, not a simulation of the engine.
The normal embedding API still requires complete `HostBindings`.

For VS Code language diagnostics, set `neoCore.hostSchema`, optionally
`neoCore.hostImplementation` and `neoCore.resourceRoot`, and
`neoCore.maxDiagnostics` (default 100). Paths accept `${workspaceFolder}`.
Changing these settings restarts the language server. Restart it after replacing
the exported schema file. These settings configure the language server; the DAP
debug adapter does not yet load external host schemas or implementations.

## Entry-only private access for tests

The console can let one test entry file access private declarations in its
directly imported modules without changing their `public` modifiers:

```sh
neocore --entry-private-access --host host.json --host-impl tools/fake_host.nc \
  --file tests/state_test.nc
neocore --entry-private-access --host host.json --check tests/state_test.nc
```

Only the actual source file named by `--file` or `--check` receives this permission,
including its function bodies. It may use `alias.name` for an imported module's
private function, global variable, constant or struct. Constants remain read-only,
and type, declaration-order and host-lifetime checks still apply. The flag is not
accepted by the menu, named samples, `--emit-backend`, LSP or DAP.

Imported modules and the `--host-impl` file retain ordinary visibility. The internal
launcher used with a host implementation does not replace the original test entry
as the privileged file. Import aliases continue to share one module initialization
and one set of globals; the permission neither duplicates modules nor marks their
declarations public. It is local to this compilation and adds no VM access check.

Keep application entry-point checks separate and run them without the flag. Use
this exception only for tests that intentionally depend on module internals.
Public `Program::Compile` and `Runtime::Compile` have no option for enabling it;
the console uses a separate internal compilation entry point. Omit the flag for
the existing private-access diagnostics.

## Optional unused-declaration warnings

```sh
neocore --check app.nc --warn-unused
neocore --host host.json --host-impl tools/fake_host.nc \
  --entry-private-access --check tests/state_test.nc --warn-unused
```

`--warn-unused` is accepted only with `--check`. After successful type checking it
examines file-scope constants, global variables and named functions throughout the
entry file's import bundle. Each warning includes the original file, line, column
and declaration name. Warnings go to stderr; compilation still succeeds with exit
code 0. Compilation errors retain their ordinary diagnostics and nonzero exit code,
and do not produce unused warnings. Checking never executes initializers.

A constant reference in another constant, a `case` label or a struct field default
counts as a use. Globals assigned but never read receive a distinct warning. Local
variables, parameters, struct fields, host-schema declarations and compiler-generated
helpers are excluded. Function calls and function values passed to callbacks or
host implementations count as uses. Self-recursion alone does not count.
Host-callable exported functions and globals are exempt; `public` alone does not
mark a declaration used.

This is reference analysis, not complete execution reachability. References inside
uncalled functions and branches still count, and mutually referencing unused
functions may remain unreported. Compound updates and reads of aggregate owners
also count as reads. A warning is a cleanup hint, not proof that deleting the
initializer or declaration preserves side effects. No code is removed or rewritten.

Results describe this entry file's bundle, not every possible application or test
entry. Check a test entry to include its references; `--entry-private-access` grants
access without itself marking declarations used. With the warning option omitted,
compilation performs no additional usage recording or analysis traversal. The
warning pass does not change generated bytecode or add VM checks.

## Implement selected members in .nc

```sh
neocore --host host.json --host-impl tools/fake_host.nc \
  --resource-root data --debug --file scripts/main.nc
```

The implementation is a private module initialized before the application, in
the **same program and instance**. It has its own globals and imports and can use
normal typed collections, closures, `print` and typed JSON APIs. The compiler
links registered functions directly to typed host calls; it does not resolve
member names on every execution. `--emit-backend` uses the same schema and
implementation options.

Assume the schema declares `Services.UserData`, type `UserDataService`, and
`SetInt(string, int)` / `GetInt(string, int) -> int`:

```nc
map<int,map<string,int>> stores = {};
UserDataService userData = Headless.New_UserDataService();

UserDataService GetUserData(ServiceRoot self) { return userData; }
void SetInt(UserDataService self, string key, int value) {
    int id = Headless.Id_UserDataService(self);
    if (!stores.contains(id)) stores[id] = {};
    stores[id][key] = value;
}
int GetInt(UserDataService self, string key, int fallback) {
    int id = Headless.Id_UserDataService(self);
    if (!stores.contains(id)) return fallback;
    if (!stores[id].contains(key)) return fallback;
    return stores[id][key];
}

Headless.Bind("Services.UserData", GetUserData);
Headless.Bind("Services.UserData.SetInt", SetInt);
Headless.Bind("Services.UserData.GetInt", GetInt);
```

Every implementation receives its owning native type as the first argument,
followed by the declared parameters. Return and argument types must exactly match
the schema. `Headless.Bind` is a file-scope declaration with a literal member path
and a named function. Duplicate registrations, including aliases for the same
member, are errors. Unregistered members retain the explicit missing-member error.

Canonical paths are `OwnerType.Method`, `OwnerType.Property.get` /
`.set`, and `OwnerType.AddEvent.EventName` (using the actual event method name).
Paths through declared global objects and native-valued getters are also accepted,
such as `Services.UserData.SetInt`. A readonly property's path without `.get` is
an alias for its getter. All objects of an owner type share the implementation;
the `self` ID distinguishes their individual state.

For each native type `Node`, the console supplies:

- `Headless.New_Node()` creates a fake object with a fresh ID.
- `Headless.Id_Node(value)` returns its ID, or zero for null.
- `Headless.Destroy_Node(value)` invalidates its ID.
- `Headless.SetGlobal_this(value)` replaces a declared global named `this`.
  The same `SetGlobal_NAME` form is generated for other declared globals.

Native receiver IDs are validated on every mapped call. Destroyed objects cannot
be used as live receivers and IDs are not reused. Store per-object properties and
event callbacks in maps keyed by ID; storing a callback retains its closure.
The implementation can expose explicit public helpers to deliver fake events.

Mapped implementations are synchronous. The compiler rejects `sleep`, `yield`,
`coroutine.resume`, or indirect function calls in a mapped function or a reachable
named helper, including other mapped members. This conservative rule also rejects
an indirect call that happens to be synchronous. Creating/storing callbacks is
allowed; an ordinary public event-delivery helper can call them outside a mapped
host invocation. Normal application coroutine behavior is unchanged.

## Read resource text

Only the implementation module can access the `Headless` API, including through
an aliased API object. Ordinary application modules cannot call its methods.

`Headless.ReadText(relativePath)` reads UTF-8 text relative to `--resource-root`;
the default root is the application file's directory. A leading UTF-8 BOM is
removed, while newlines are preserved. An empty file returns an empty string;
an unreadable file or an absolute path returns null.
`Headless.LastError()` provides the most recent read error and is empty after a
successful read. Resource-root resolution is not a filesystem sandbox.
Implement a schema's `Resource.LoadText` by returning `Headless.ReadText(path)`;
large JSON data files need not become source-code literals.

## Multiple compile diagnostics

`CompileOptions::diagnosticLimit` bounds independent diagnostics across the import
bundle (default 100; console `--max-errors N`, range 1..10000). The C++
`Error::diagnostics` list contains message, original filename, line and column.
Legacy `Error.message/sourceName/line/column` still report the first diagnostic.
The CLI prints the list and exits with failure; LSP publishes it per source file,
including unsaved open-module overlays.

The parser recovers at statement/block boundaries and the checker continues with
other statements and functions. Invalid declarations suppress dependent follow-up
errors. Independent missing imports are collected. Unrecoverable lexical/module
structure errors can stop the affected compilation phase; the compiler does not
invent executable code for an invalid bundle.

The console regression `neocore_headless_cli_regression` covers schema-only
checking, script implementations, file-backed JSON, closure events, ID deletion,
missing members, private helpers, wrong signatures, CLI and LSP diagnostics.
