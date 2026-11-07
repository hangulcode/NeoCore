# Compiler-owned storage and lifetime

NeoCore's storage migration moves type selection and ownership planning to the
compiler. Its goal is payload-only copies and stores for statically typed VM
slots, without an allocation test, a type switch, or a repeated type-tag write.
The VM execution model, script value semantics, and public host API remain intact.

## Eight-byte representation

`VarInfo` is an eight-byte, untagged payload. Its `_type`, `_vecCount`,
`_unused` and `_vecX` members are removed, and a size assertion enforces this.
VM frames, globals, constants, closure captures and vector-array elements use
this payload directly.

| Language value | Payload representation |
| --- | --- |
| int / float / bool | Integer, binary64 or boolean payload |
| Vector2 | Two inline float32 components |
| Vector3 / Vector4 / Quaternion | Pointer to shared `VecInfo` storage |
| string / collections / struct / owned builtin object | Pointer to the corresponding owned representation |
| host object | Non-owning object ID; the host resolves and validates it on access |
| function | Zero for null, encoded named-function ID, or aligned closure pointer |
| coroutine | Pointer to its execution context |

Vector3 and Vector4 retain reference-counted value semantics and copy-on-write.
Their dimensions come from checked instructions and immutable storage descriptors.
Vector3 construction and detached writes have allocation/reference costs that its
former inline representation avoided. Smaller slots alone do not prove a speedup.

The program owns immutable frame, global and outgoing-call layouts. Captures
retain a shared immutable layout, so their ownership information survives the
creating frame. `ValueRef` is a borrowed view over a payload and its layout; it is
not stored in each VM slot. For a fixed slot, `SetType` checks the contract in
Debug and never writes a type tag. Typed numeric instructions access the payload
directly.

Null owning references use a zero pointer, and null host objects use a zero ID.
Numeric bits are never inspected as an ownership state. A function slot uses the
low bit to distinguish a named-function ID from an aligned closure pointer; this
is a representation choice within one checked function type, not a dynamic
language type.

## Compiler and call layouts

Image version 0161 carries `SLOT` layouts, function result storage and `CALL`
plans. A slot layout entry occupies 8 bytes; a cleanup entry occupies 4 bytes.
Programs share these arrays across instances and recursive calls. The loader
validates serialized plans rather than deciding new storage optimizations.
Older images must be recompiled and host libraries rebuilt.

The compiler assigns separate compatible temporary slots and emits fixed
integer/float arithmetic, copies and immediate stores. Those instructions write
only the payload, including in fused operations. Numeric declaration values are
not initialized again in the frame prologue. Reference locals start empty.
Arguments retain the values installed by the caller.

`PREPARE_CALL` selects the compiler-produced outgoing window after argument
expressions have finished. Nested argument calls therefore cannot overwrite
already evaluated arguments. The caller copies arguments into that window;
script entry then selects the callee's fixed layout without clearing those
arguments. Async dispatch similarly selects its fixed `void(bool,string)`
callback window before writing the completion values. Named and captured
callbacks use the same argument contract.

The runtime tracks active frame ranges in a reusable vector. Ordinary local
access uses a cached contiguous layout view rather than a frame search. Frame
range capacity can grow with call depth; there is no new type-plan allocation
on each call. Transitional cleanup of outgoing windows and exceptional paths
still traverses the relevant storage views.

Copy/store instructions encode their storage policy at compilation. Owned copies
retain the new pointer before releasing the old pointer. Host object IDs have a
separate non-owning policy and still require validation on engine access. Return
instructions preserve results and captures before releasing local ownership.

## Remaining tagged boundaries

`BoundaryValue` is a separate sixteen-byte value used by host transfers, native
scratch values, GC work queues and the existing list/map/set entries. Struct
fields represented by those containers therefore still use this form. It is
not a wrapper around every VM stack slot.

Generic bridge operations read a `ValueRef`'s immutable descriptor or a boundary
value's explicit tag. Completing tag removal from collection storage requires
container element/field layouts and their host/GC paths to move together. The
current change removes tags from `VarInfo`; it does not claim that every backend
operation or heap container is tag-free.

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
storage kind. Boundary scratch values retain explicit ownership metadata. Reusing a
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

## Current migration status (2026-11-07)

The eight-byte payload and compiler-owned frame/call layouts are implemented.
The opcode-family, general fusion and in-place arithmetic changes are retained.
List/map/set entry storage and some generic bridge operations remain transitional,
as described above. No performance claim is made for this storage migration
without a fresh paired benchmark.

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
