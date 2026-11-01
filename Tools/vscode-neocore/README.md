# NeoCore for Visual Studio Code

This extension supports the statically typed `.nc` language. It uses the NeoCore
console's `--lsp` and `--dap` modes; source is checked by the typed frontend before
the shared VM backend receives its internal representation.

## Setup

Build the NeoCore console with the root `NeoCore.sln`, or with CMake. Open this
extension directory in VS Code and press F5 to start an Extension Development
Host, or package it with the VS Code extension packaging tool.

Open the NeoCore repository in the development host. Native Windows builds under
`Build/Windows/bin/x64/{Release,Debug}` and CMake builds under
`Build/neocore/bin/{Release,Debug}` or `Build/neocore/bin` are discovered
automatically. For another installation, set `neoCore.debugAdapterPath` to the
typed `neocore` executable. `neoCore.languageServerPath` can select a separate
typed executable; otherwise it uses the debugger executable.

The extension starts local compiler processes only in trusted workspaces.

For host-aware language diagnostics, set `neoCore.hostSchema` to an exported
HostSchema JSON file. Optional `neoCore.hostImplementation` and
`neoCore.resourceRoot` configure a console mock; `neoCore.maxDiagnostics` limits
independent errors (default 100). These paths accept `${workspaceFolder}`.
Settings changes restart the language server; restart it after exporting a new
schema. This does not configure DAP execution. See
[HeadlessHost.md](../../docs/HeadlessHost.md).

## Language support

- `.nc` syntax highlighting and snippets use explicit types, typed collections,
  structs, and `fn(...) -> T` function values. Removed `var`, `fun`, and public
  `int32`/`float32` scalar declarations are highlighted as invalid.
- Compiler diagnostics update for open and changed documents, including unsaved
  text. Imported open documents overlay their saved files. Diagnostic locations
  are translated to original module paths and UTF-16 editor columns.
- Completion, hover, and signature help use the typed builtin signature table
  and declarations in the current document. String, collection, TimeSpan,
  Stopwatch, and asynchronous-request methods are suggested for declared
  receiver types. Function signatures include parameter and return types.
- Completion is a declaration-based aid, not a second type checker. Arbitrary
  expression receiver inference, cross-module navigation, definition, and rename
  are not implemented. The compiler remains authoritative for validity.

## Debugging

Run **NeoCore: Create Launch Config**, open a `.nc` file, then press F5. Ctrl+F5
uses the same typed compilation path without stopping at breakpoints.

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "neocore",
      "request": "launch",
      "name": "Debug NeoCore",
      "program": "${file}",
      "cwd": "${workspaceFolder}",
      "libPath": "${workspaceFolder}/Lib"
    }
  ]
}
```

The adapter supports unconditional line breakpoints, continue, pause, step into,
step over, step out, call stacks, locals and globals, collection expansion,
variable-name evaluation, and runtime exception details. Imported `.nc` files
keep their original file/line locations. Locals use source names rather than
generated backend names, and struct children use declared field names and types.

The debugger runs bounded VM slices on its request thread. Pause, disconnect,
and input EOF are processed between slices. Native callbacks must return before
the VM can process another request. Runtime faults retain a bounded, read-only
snapshot; a fault cannot be resumed.

Conditional breakpoints, hit conditions, and logpoints are reported as unverified.
Expression evaluation does not execute arbitrary code. A snapshot retains at most
64 frames and applies the shared debugger's value/text budgets. The framing limit
is 8 MiB per request. The console checks standalone programs against its available
host schema; engine-specific bindings require that engine's typed host integration.

## Regression checks

```sh
node --check Tools/vscode-neocore/src/extension.js
node Tools/vscode-neocore/tests/extension_regression.js Build/neocore/bin/Release/neocore.exe
python Tests/debug_adapter_regression.py Build/neocore/bin/Release/neocore.exe
```

The protocol suite covers framing, Unicode, compiler diagnostics, signature help,
hover, modules, struct inspection, stepping, coroutine yields, faults, output
ordering, pause, and disconnect. NeoScript's `.ns` adapter is not a fallback for
unsupported `.nc` programs.
