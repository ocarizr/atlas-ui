# UI command foundation

`atlas::ui` is an independent SDK-free C++20 library. Phase 1 provides serial command
production and immutable batches; it does not yet create elements, apply state,
schedule work or draw UI. See the [phased implementation plan](../../docs/36_ui_implementation_plan.md).

```cpp
atlas::ui::command_buffer commands{limits, shared_memory, diagnostics};
commands.begin_frame(42);
commands.set(element, atlas::ui::property::width, 240.f, producer_id);
commands.set(element, atlas::ui::property::visible, true, producer_id);
const auto batch = commands.seal();
// Later phases apply the batch against live state and schedule independent work.
// Keep the batch alive while a worker borrows batch.writes()/batch.conflicts().
```

`element_handle` has a slot index and nonzero generation. Producer IDs are nonzero
session-assigned diagnostic identities. Phase 2 will validate handles against the
live tree; passing a numerically valid handle currently does not prove liveness.
Width/height accept finite nonnegative floats, opacity accepts [0, 1], and visible
accepts bool. Unknown properties and mismatched values throw before admission.

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
