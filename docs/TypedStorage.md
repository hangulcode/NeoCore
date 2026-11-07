# Compiler-owned storage and lifetime

NeoCore's storage migration moves type selection and ownership planning to the
compiler. Its goal is payload-only copies and stores for statically typed VM
slots, without an allocation test, a type switch, or a repeated type-tag write.
The VM execution model, script value semantics, and public host API remain intact.

## Final eight-byte representation

The agreed target is an eight-byte, untagged `VarInfo` payload. Remove `_type`,
`_vecCount`, `_unused` and `_vecX` from that payload. This target is not yet the
representation used by the implemented foundation below.

| Language value | Payload representation |
| --- | --- |
| int / float / bool | Integer, binary64 or boolean payload |
| Vector2 | Two inline float32 components |
| Vector3 / Vector4 / Quaternion | Pointer to the existing `VecInfo`-style shared storage |
| string / collections / struct / owned builtin object | Pointer to the corresponding owned representation |
| host object | Non-owning object ID; the host resolves and validates it on access |
| function / coroutine | A uniform callable or execution-context representation |

Vector3 adopts Vector4's reference-counted value semantics and copy-on-write.
This representation change is implemented: Vector2 occupies the eight-byte
payload inline, while Vector3 and Vector4 use shared `VecInfo` storage. Array
initialization, typed reads/writes, host returns and frame cleanup preserve their
reference ownership. The former inline `_vecX` and `_unused` members are removed;
the redundant per-value dimension field is also removed. The transitional
per-value type tag still distinguishes Vector2, Vector3 and Vector4 while the
remaining tagged paths are migrated.
Vector dimensions come from the checked instruction or enclosing type descriptor;
in the final representation they are not stored per value. Vector3 construction
and detached writes have allocation/reference costs that the former inline
representation avoided. Smaller
slots alone are not evidence that these vector workloads become faster.

Immutable type layouts must cover frames, globals, collection key/value types,
struct fields and closure captures. An owning object retains access to its layout
for as long as it can outlive its creating instance or program handle. GC and
exceptional unwinding follow ownership layouts, including suspended frames and
outgoing argument windows. They must not inspect absent per-value tags or infer
whether arbitrary payload bits look like a pointer.

Owning-reference null and empty/uninitialized ownership use a zero pointer. Host
object null uses a zero ID; numeric and ID bits are never interpreted as an
ownership state. Function values currently mix plain
function indices and allocated closures, so their representation must be unified
before removing their tag. Public host/debugger views may carry explicit type
descriptors at the boundary; ordinary VM copies and stores must not reconstruct a
tagged value or query a side table on every operation.

All transitional boxed producers and consumers must be migrated before changing
the shared payload size. Keeping sixteen-byte wrappers for every VM slot would
not achieve this target. Bytecode images and host libraries need rebuilding when
the final layout changes.

## Implemented foundation

Image version 0160 carries the compiler's `SLOT` layouts. A layout entry occupies
8 bytes in memory; a cleanup entry occupies 4 bytes. The program shares these
arrays across its instances and recursive calls. No per-call layout allocation
or per-store lookup is introduced.

The compiler assigns compatible numeric temporaries separate slots and emits
fixed integer/float arithmetic, numeric copies, and immediate stores. Those
instructions write only the payload. The same contract is preserved by fused
instructions. Numeric tags are established once on frame entry for compatibility
with the remaining tagged backend; numeric declaration values are not zeroed
there. Function arguments retain the values installed by their caller.

Named string, collection, owned builtin-object, asynchronous-request and three/four-component
vector copies select their retain/release operation from the instruction's storage
kind. Null is still a runtime value state. Function values keep a separate variant
policy because a named function and an allocated closure have different ownership.
Return instructions preserve the result and captures, then traverse only the
compiler's ownership list. Instance destruction/reset uses the global layout.
Host object IDs have a separate non-owning copy policy. They never enter pointer
reference-counting or cleanup, and host access still validates each ID.

This is not the complete removal of `VarInfo` tags. Reference copies still carry
the boundary tag, and generic outgoing argument windows, unconverted temporary
producers, host transfers and exceptional unwinding retain tagged handling.
Removing those tags requires migrating their producers, readers and debugger/GC
boundaries together. Ordinary declaration/temporary slots already have an explicit
layout, so that next step does not need to rediscover their types at runtime.

## Runtime representation

- A program owns immutable function and global layouts. Each slot records its
  exact language type and its physical storage/cleanup kind. Source/debug names
  are not a source of type information.
- Runtime plans use contiguous arrays and frame-relative offsets. A call must
  not allocate a map, vector, type descriptor, or cleanup record. Recursive calls
  share the same plan and use different frame bases.
- Copy/store instructions encode their storage policy at compilation. They do
  not look up a slot's type in a runtime table on each execution.
- Temporary reuse is limited to compatible storage and ownership kinds. Adjacent
  operand windows retain their required order and contiguity. Call windows and
  representation-polymorphic function values have explicit ownership rules.
- Tagged values at public, debugger, container, and transitional backend
  boundaries are separate from the long-term payload-only frame contract.
  Retaining a boundary representation does not authorize dynamic type selection
  inside a statically typed copy/store.

## Local lifetime

Numeric values are initialized by their declarations; a frame prologue must not
zero them again. Owning reference slots start empty so that a skipped declaration
or a failing initializer can be cleaned safely. Assignment retains the new value
before releasing the previous value, including self-assignment and aliases.

Named declarations have distinct slots throughout a function, including sibling
blocks and shadowed names. Leaving a lexical block does not reuse its slot for a
different declaration. A fixed slot has one storage and cleanup policy throughout
that frame. Repeated execution of the same declaration in a loop still replaces
and releases its previous value; a function-entry initialization cannot substitute
for that release. A declaration without an initializer resets its default value
on each execution. Captured and otherwise escaped values keep their own ownership.

The temporary allocator reuses a fixed slot only for the same exact type and
storage kind. Transitional boxed temporaries keep generic cleanup. Reusing a
physical stack region for a different function is handled at the call boundary,
not by changing that function's immutable slot policy. Future cross-type local
slot reuse would require compiler-proven lifetime transitions and cleanup on every
control-flow edge; it must not be introduced merely because lexical blocks differ.

Before returning, the VM preserves the result and synchronizes surviving closure
captures, then releases that frame's remaining owned values. Numeric slots are
absent from the cleanup list. Errors and cancellation release the same ownership
exactly once. Suspension retains the frame and its values until it resumes or is
cancelled. Closure synchronization must never run after its frame was released.

## Global lifetime

Global layouts belong to the shared program; global values belong to each
instance. Reassignment releases the previous value immediately. Instance reset
and destruction release all instance-owned global references. Returning from a
function, sleeping, or cancelling a call does not reset globals. Program-owned
constants and other instances' globals are not part of instance cleanup.

## Checkpoint status (2026-11-07)

This checkpoint retains all local opcode-family, general fusion, in-place
arithmetic and compiler-owned storage work. `_vecCount`, `_unused` and `_vecX`
are removed. `_type` remains, so `sizeof(VarInfo)` is still 16 bytes; this commit
does not complete the eight-byte migration.

The next step is to finish exact layouts for generated helpers, outgoing call
arguments, collection entries and closure captures, then migrate exceptional
unwinding, GC and host/debugger boundaries together. Only after those paths no
longer depend on a per-value tag can `_type` be removed and the eight-byte size
assertion enabled. Do not add a mutable per-slot tag table or retain a tagged
wrapper for every ordinary VM slot as a substitute.

The checkpoint passes the complete Windows x64 Debug suite (85 tests), including
image round trips, storage lifetimes, modules, vector arrays and C ABI integration.
Changed compiler/runtime sources also pass Android arm64 C++11 syntax checks.
The final vector-tag changes have not received a new complete Release run or
performance measurement; earlier measurements do not describe this snapshot.

## Validation

Regression coverage must include alternating functions with different slot
layouts, recursion, repeated allocation/reassignment, self-assignment, partial
initialization failures, closures, suspension/cancellation, host reentry, global
reset, and shared-program instance isolation. Check live allocations while the VM
is still alive: its final emergency sweep can hide retained references.

Block tests cover shadowed names with different storage kinds, repeated owning
declarations, null defaults, conditional declarations, break/continue, early
returns, failures in alternate branches, and captures that outlive their blocks.
They also check that live ownership stays bounded inside a running loop.

`neocore_storage_regression` checks these lifetime rules in normal and debug-info
images. Its optional `--stress` mode runs ten million allocating script calls and
ten million numeric script calls, validates the result, and compares live owned
objects before the caller returns. This is an untimed correctness check, not a
performance benchmark. Allocation counters are explicitly refreshed before each
snapshot; a cached counter after cancellation is not evidence of a leak.

Serialized images preserve compiler-produced layouts and storage choices. The
loader validates them; it does not infer new optimizations. Disassembly and debug
locations remain tied to source operations. Timed benchmarks require separate
user approval and compare the complete current worktree against the new build.
