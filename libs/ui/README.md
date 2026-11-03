# UI element state and commands

`atlas::ui` is an independent SDK-free C++20 library. It provides serial command
production, immutable batches and an owner-thread EnTT-backed element tree.
Named phases reuse an injected jobs dispatcher with joined outputs and sequential
fallback. The library does not implement layout or draw UI. See
[frame execution](../../docs/ui_frames.md) and [UI delivery tracking](https://github.com/ocarizr/atlas-ui/issues/27).

```cpp
atlas::ui::element_tree tree{{}, shared_memory, diagnostics};
const auto element = tree.create();
auto& commands = tree.commands();
commands.begin_frame(42);
commands.set(element, atlas::ui::property::width, 240.f, producer_id);
commands.set(element, atlas::ui::property::visible, true, producer_id);
const auto batch = commands.seal();
tree.apply(batch);
const auto requested = tree.requested(element);
// Keep the batch alive while a worker borrows batch.writes()/batch.conflicts().
```

`element_handle` has a slot, nonzero generation and session identity. Each tree owns
one bound producer and validates all targets before application. A standalone
`command_buffer` remains available for packet preparation; its batches have session
zero and cannot be applied to a tree. Producer IDs are nonzero caller-assigned
diagnostic identities. They are not access-control capabilities.
Width/height accept finite nonnegative floats, opacity accepts [0, 1], and visible
accepts bool. Unknown properties and mismatched values throw before admission.

## Tree and publication

`create(parent)` appends a child, or creates an independent root when parent is
empty. `parent`, `first_child` and `next_sibling` return value handles; children
follow insertion order. Independent roots have no sibling chain. `reparent`
rejects self/descendant cycles before mutation; moving to the current parent is a
no-op. `remove` destroys the complete subtree iteratively. Reused slots advance
generation; exhausted generations retire the slot instead of wrapping.

Topology edits are immediate owner-thread operations, each validated before its
mutation. They are not deferred structural commands; batch transactions cover
absolute scalar property writes. No external EnTT mutation/callback API is exposed.

Sessions are unique within the process. Foreign, invalid and stale handles fail
before topology/property mutation; `contains` returns false for them. Numeric
handles are not serialized identities. Destroyed-session batches remain immutable
but cannot apply to a replacement tree. No public header exposes EnTT storage.

`requested()` returns committed **local requested values**, by value. Pending or
sealed-but-unapplied commands are invisible. Child local visibility/opacity does
not change when a parent changes. There is no computed layout, inherited opacity
or culling result; those values must not be inferred from requested reads.

`apply()` validates the session, batch order and every winning target before its
nonallocating publication pass. Each frame begins with ordinal one; subsequent
batches must be consecutive, and replay/older frames are rejected. Moving to a
newer frame explicitly abandons unapplied work from older frames. Failed batches
leave requested state, invalidation and the application cursor unchanged. An
irrecoverably stale batch is discarded by beginning a newer frame, not by silently
dropping its bad writes. Cross-batch conflict history remains in the producer.

New elements are layout/paint dirty. Size and hierarchy changes dirty affected
ancestry and the affected subtree; visual changes dirty only that subtree's paint.
Unrelated branches remain unchanged. Equal committed assignments are no-ops.
`acknowledge(handle)` clears only that element's flags. This is conservative
invalidation, not a layout solver; overlapping writes can revisit descendants.

Live capacity defaults to 4096, bounded to 1..65536. PMR owns preallocated slot,
free-list/traversal and command storage. EnTT entity/component storage and PIMPL/control
blocks use ordinary allocations; capacity is not a process-memory/fragmentation
guarantee. Topology and property state are owner-only, including reads. Do not pass
the live tree to workers. Immutable batch publication still requires synchronization.

The header-only EnTT dependency is compiled privately into the archive. Installed
consumers load the jobs and diagnostics targets before `share/atlas/ui.cmake`; no EnTT header
or target is required by their public API. Rebuild consumers with matching headers
and archives because handles and command packet layouts include the session field.

## Deterministic preparation

- `begin_frame(id)` requires a positive increasing frame and no unsealed commands.
- `set()` validates then appends to reserved storage on the creating thread.
- `seal()` publishes one last-writer winner per element/generation/property in the
  current batch, ordered by original frame-wide sequence. Only absolute assignments
  may coalesce; there is no action/event/delta merging API.
- Every repeated key in the frame is a conflict, including equal values or one
  producer writing twice. The immutable record includes the final two write origins
  and the total write count for that key in the frame. It is not a complete audit of
  every overwritten value. Last-writer selection is a deterministic fallback, not
  conflict resolution approval.
- Conflict history includes earlier sealed batches in that frame. Their contents
  are never changed retroactively. A new frame resets history; another generation
  or another property is a different key.
- `discard_pending()` rolls back only unsealed commands, retaining earlier published
  history. This supports discarding a failed dispatch before publication.

The producer is owner-affine and non-reentrant. Foreign calls throw before touching
mutable state; foreign destruction terminates. Immutable batch copies retain their
own data and allocation resource after the producer resets or is destroyed. Publish
them through normal synchronization before worker reads. Borrowed spans do not own
the batch. Moved-from batch queries return identity zero and empty spans.

## Budgets and diagnostics

Defaults are 4096 raw commands and 32 batches per frame, with four warning details
per conflicting batch. Supported bounds are 1..65536 commands, 1..1024 batches and
0..64 warning details. Raw sealed writes still consume that frame's command budget.
At the last batch, new writes are rejected rather than accepted into an unpublishable
state. Overflow is explicit; admission never silently drops work.

Serial append allocates no storage after construction. Sealing sorts a private copy
of bounded frame history, then publishes after all potentially failing preparation.
Allocation failure leaves pending data and batch numbering unchanged for retry or
rollback. An empty seal is allowed and consumes a batch ordinal. Retaining batches
over many frames is a consumer memory-budget responsibility.

An injected shared PMR resource owns vector storage; absent one, the standard
new/delete resource is used. Metadata/control blocks still use ordinary allocation.
If batches can be released on workers, the injected resource must support concurrent
allocation/deallocation; shared ownership alone does not make a resource thread-safe.
The application can inject its existing shared `atlas::memory` resource without
making UI choose an allocator backend.

Injected diagnostics emit a warning summary and capped conflict details in
`ui.commands`. Set the detail cap to zero for summary-only warnings. Full conflict
records remain available when logging is disabled, filtered, fails or drops records.
Warnings identify frame, batch, target/generation, property and producer sequences.
Fix competing writers in the consumer; there are no per-property success logs.

`ui.commands.seal` emits an optional CPU scope following `ATLAS_PROFILING=auto|on|off`.
Both logging and profiling are opt-in through injected services; there is no file
access or telemetry transport. Synchronous diagnostics cannot mutate the producer
while sealing. Logger failures do not change the prepared batch.

Tree create/remove/reparent/apply operations emit optional `ui.cpu` scopes; failed
operations log through the injected destination without replacing exceptions.
Initialization/release messages use debug severity, with no per-write success logs.
Diagnostic callbacks cannot reenter either the tree or its bound producer.
