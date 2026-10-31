# NeoCore — working rules

This is the single entry point; `CLAUDE.md` and `GEMINI.md` refer here. Keep this file within 80 lines.
Read [StaticTyping.md](docs/StaticTyping.md) before changing the language. The original NeoScript repository retains the dynamic language documentation.

## Repository and shared work

- This repository is public. Do not record private repository names, engine paths, or game-project names here.
- Work in the NeoCore checkout. The original NeoScript checkout and engine/content repositories are separate projects.
- Preserve other agents' and the user's edits. Do not reset shared files with `git checkout --`; isolate or back up measurement changes.
- Resource names are lowercase. Code comments are Korean; documents and commit messages are English.
- Source files containing Korean must use UTF-8 BOM for MSVC. Preserve existing line endings.

## Commits

- Use `[Tool] English subject`, with an English body describing the change and reason. Do not put performance numbers in commit messages.
- Use `[Claude]`, `[Codex]`, or `[Gemini]` for the agent making the commit. Preserve the configured author/committer identity.
- Codex-authored changes require a blank line followed by `Co-authored-by: Codex <codex@openai.com>`.
- Commit only when requested. Push only when the user explicitly requests a push.

## Language and implementation

- NeoCore requires explicit declarations: `int x;`, `int F(int x) { ... }`, `void F() { ... }`. Do not reintroduce `var` or `fun` in NeoCore source.
- NeoCore source uses `.nc`. The separate NeoScript repository retains dynamic samples and regressions. Do not attach its dynamic LSP/parser to `.nc` as a typed language service.
- `int` is int64; `float` is binary64. `int32` and `float32` are not user-declarable scalar types; 32-bit storage belongs only to compact arrays and vector components.
- Nullability is explicit (`T?`); reject unproven null access. Numeric/bool/vector defaults are initialized; nonnullable references require initializers.
- Functions can call later functions and recurse. Variables and constants must be declared before use, including file variables referenced from function bodies.
- Keep evaluation order, overflow/division behavior, value/reference semantics, and host error unwinding unless a language change is explicitly approved.
- Numeric expressions, scalar writes, script calls and returns emit from typed IR. Struct scalar targets use flat raw field paths; resolve writes after the RHS and preserve expression results. Calls carry exact signatures, ordered arguments and explicit value copies. Host/builtin calls, aggregate intrinsics, declarations/control flow and unmigrated selectors retain ordered transitional paths. Unsupported features must fail explicitly.
- Preserve typed operand provenance; never infer a payload type from a variable name or debug line. Slot reuse still requires destination reference cleanup. Typed op metadata is centralized in `NeoVMTypedOps.h`; default builds exclude generic arithmetic/negation/read-arithmetic instructions, and `NOP_LOG_NOT` consumes checked bools.
- Structs, vectors, typed/compact arrays, nested collections/sets, modules, function values/closures, coroutine signatures and aggregate/native host schemas are checked today. The compiler/VM migration is ongoing. Do not claim the static VM is finished.
- Vector components are float32; scalar reads are float64. Quaternion constructors/host values use w,x,y,z. Component writes preserve value semantics and stored-value results; host writes resolve the saved receiver ID separately for getter and setter.
- No pointers or `T&` declarations, parameters, or returns. Structs/vectors copy by value; functions return modified values for explicit caller assignment. Struct clone helpers preserve nested copies; fields have no alias cells or in-place assignment helper. Internal boxed vector locals serve component writes only. Collections/native IDs keep their existing shared identity semantics.
- Keep the core, public API, C ABI, console, and mini game C++11-compatible. Do not introduce C++14+ syntax or library dependencies. Vendored PCRE2 requires C99.
- Order `switch` cases by actual enum/numeric value; put `default` last and preserve fallthrough.

## Build and regression

```sh
cmake -S . -B Build/neocore -DNEOCORE_BUILD_TESTS=ON -DNEOCORE_BUILD_EXAMPLES=ON
cmake --build Build/neocore --config Release --parallel
ctest --test-dir Build/neocore -C Release --output-on-failure
```

- Check accepted/rejected typed programs, lowering semantics, and `NeoCore.h`. The backend carries no dynamic arithmetic opcodes; this checkout no longer builds the dynamic NeoScript test/console targets.
- `docs/SampleMigration.md` maps converted samples; `Samples/bench/check.nc` checks the eight workloads without timers; `bench.nc` is the timed suite. Its explicit map fallback and string conversions preserve results, not identical bytecode.
- The CLI defaults to non-debug execution. Use `neocore --debug --file source.nc` for runtime source locations; keep timed benchmarks in non-debug mode.
- Native Windows NeoCore: open root `NeoCore.sln` (also available through `Samples/console/console.sln`). For the library alone, open `Windows/NeoCore.sln`. The `.nc` runner is `Build/Windows/bin/x64/Release/neocore.exe`; `Windows/NeoCore.vcxproj` builds `NeoCore32/64[-d].lib`.
- Console functional checks: `--smoke`, `--debug-smoke`, `--compiler-error-regression`, `--bench-check`, and `--pooltrim-check`. Report observed results rather than assuming historical counts.
- The stdio `--dap` and `--lsp` modes must compile through the NeoCore frontend. Run their protocol regressions separately from functional sample checks.
- Check non-Windows compilation for C++ source changes; Windows-only assumptions have broken Linux/Android before.
- NDK clang: `%LOCALAPPDATA%/Android/Sdk/ndk/25.1.8937393/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe` with `--target=aarch64-linux-android24 -std=c++11 -Werror=c++14-extensions -Werror=c++17-extensions -fsyntax-only -INeoSource`.
- Where available, use GCC with `-std=c++11 -Wpedantic` as well; Clang alone has missed C++14 extensions.

## Host integrations

- Typed C ABI/Unity live in `Unity/NeoCore` and `Unity/com.neocore.unity`; the VS Code extension lives in `Tools/vscode-neocore`. Keep static signatures and instance lifetime checks at all host boundaries.
- Keep the C ABI out of `NeoSource`: source-based hosts compile `NeoSource/*.cpp` wholesale.
- Plugin binaries are not committed. Build the typed package with `pwsh Unity/NeoCore/build_windows.ps1`; `Unity/build_windows.ps1` delegates to it.
- Enable `NEOCORE_BUILD_UNITY_BRIDGE` for C ABI integration checks. Test the C header as C, managed wrappers, callback lifetimes and instance isolation; report unavailable Unity Editor checks explicitly.
- Host members use `HostSchema` plus `HostBindings`; follow [HostBindings.md](docs/HostBindings.md). Resolvers validate typed IDs without side effects; borrowed pointers cannot survive reentry or external deletion. Backend dynamic dispatch alone is not a static signature.

## Performance

- Ask the user before starting timed benchmarks so video playback and other builds can stop.
- Run baseline/trial binaries alternately in the same session. For the inherited eight-case suite, run NeoScript/Lua/C++ from `Samples/bench` and verify all checksums.
- Build layout can change timings. Do not compare separate sessions as proof of an improvement, or report unmeasured numbers.
- Inherited NeoScript benchmarks are not NeoCore performance claims. Static checking alone does not demonstrate faster VM execution.

## Documentation

- `ReadMe.md`: current NeoCore entry points, attribution, and clearly labeled inherited backend material.
- `docs/StaticTyping.md`: accepted language design, implemented subset, and remaining milestones; update it with language changes.
- `docs/ScriptAuthoring.md`, `docs/API.md`, `docs/Embedding.md`, `docs/Unity.md`: current typed language and hosting guides. Document supported features and actual limitations without implying dynamic fallback.
