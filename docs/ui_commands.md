# UI commands

`atlas::ui` implements serial command production and immutable batches independently
of native rendering. Its public API and examples are in the
[UI library guide](../libs/ui/README.md).

## Command contract

A creating-thread `command_buffer` accepts absolute assignments to width, height,
opacity and visibility. Handles contain a slot and nonzero generation; numerical
validity does not prove that a live element exists. The library has no live tree,
state application, layout, painting, input routing, MVVM or Lua execution.

`begin_frame()` establishes a positive increasing identity. `set()` validates types
and ranges before admission. `seal()` publishes an immutable batch with one last
serial writer per element/generation/property. Sealed batches retain their own
storage and can be read by workers after synchronized publication.

Every repeated key within the frame is a conflict, even equal assignments by the
same producer. Records include the last two origins and total write count. Earlier
batches are never changed retroactively. Conflict warnings are capped; full records
remain accessible independently of logging. Last-writer selection is a predictable
fallback and does not establish the consumer's intended authority.

## Budgets and failure

Raw writes and batch ordinals have explicit per-frame limits. Reserved producer
storage avoids allocation during append. Sealing prepares privately and publishes
only after allocations succeed; failure permits retry or rollback. Discarding pending
commands preserves previously published history. Retaining batches across frames
requires a separate consumer memory budget.

Optional PMR storage must support concurrent release when batches are released on
workers. The buffer is owner-affine and non-reentrant; diagnostics cannot mutate it
during sealing. Profiling follows the shared build policy, and logging is injected.

The current native application draws a textured quad through generic renderer
passes; it does not consume UI commands. UI delivery is tracked in the
[UI milestone](https://github.com/ocarizr/atlas-ui/milestone/4).
