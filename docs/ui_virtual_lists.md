# Virtual lists

`atlas::ui::virtual_list` owns the content of an empty vertical-only scroll
viewport. It realizes intersecting logical rows plus configurable overscan on
each edge. Logical count does not determine the number of tree elements.
Horizontal and two-dimensional virtualization are not provided by this API.

The controller borrows the host tree, input session and shared job dispatcher.
It creates neither threads nor a scheduler. The tree and input must belong to
the same owner and outlive the controller. Its viewport binding and content
subtree belong exclusively to the controller.

## Immutable sources

An `item_source` supplies stable nonzero keys, exact outer heights, prefix
offsets, total height, key lookup and half-open position lookup. Its const
methods must be thread-safe and must not access storage or mutate widgets.
Application data arrives through the streamer; these methods consume available
buffers or metadata retained until joined preparation returns.

`fixed_item_source` uses one-based index keys and arithmetic positions with
constant metadata storage. Those keys represent immutable index identities.
For insertion/reordering with stable application keys, use
`variable_item_source` or a custom immutable source. The variable source owns
exact supplied heights, prefix geometry and a sorted key index: O(N) source
memory and O(log N) lookups, separately from bounded widget realization.
It creates no hidden widgets to measure the full list. Async size changes
publish a replacement immutable source with stable keys. Change item revisions
when bindings/resources must be recreated.

Coordinates use float layout units. Sources reject nonfinite, nonpositive or
collapsed extents; the arithmetic source caps its count at 16,777,216.
Ordinary floating-point precision still applies at large positions: this is not
an infinite-coordinate scrolling implementation.
Rows share representable endpoints so distant coordinates do not introduce gaps;
their effective heights can be quantized by float precision. Sources reject
extents where adjacent arithmetic rows can collapse to the same coordinate.

Rows occupy exact-height canvas slots spanning the content width. Visuals must
remain inside their logical row boxes, or use explicit row clipping. Arbitrary
overflowing/rotated visuals cannot expand the source's visible-range calculation.
Viewport and ancestor masks apply to both paint and hit testing. Existing
scrollbars use the complete logical extent, rather than realized rows.

## Host integration

```cpp
auto viewport = tree.create(parent);
tree.set_panel(viewport, atlas::ui::scroll_layout{});
atlas::ui::virtual_list list{
    tree, input, viewport, dispatcher,
    [](atlas::ui::row_context& row) {
        auto visual = row.tree().paint(row.root());
        visual.fill = true;
        row.tree().set_paint(row.root(), visual);
        // Build children and register handlers through row.bind(...).
        // Retain subscriptions/assets with row.retain(...).
    }};
auto items = std::make_shared<atlas::ui::fixed_item_source>(
    atlas::ui::fixed_item_options{100000, 20});

// Between frames, after applying previous scroll commands:
list.update(items, viewport_content_height);
auto source = tree.snapshot();
auto geometry = layout.calculate(source);
input.begin_frame(frame, source, geometry);
list.restore_focus();
input.process(events);
input.tick();
input.late_tick();
input.end_frame();
tree.apply(tree.commands().seal());
```

Supply the actual inside-padding viewport height, recomputed on resize.
Paint/input use the resulting source and geometry together. Hit regions remain
frozen within an input frame; committed wheel/drag offsets are realized at the
next safe boundary.

Factories run serially against a private draft and may modify only their own
root and descendants. Do not retain draft handles or `row_context`. Initialize
fresh binding state, using `retain` for shared immutable application resources.
Roots are sized/positioned by the controller after construction. Heavy work
belongs outside the factory and serial input callbacks.

## Identity, focus and cleanup

A logical key, physical handle and `row_ticket` are different identities.
Unchanged key/revision rows retain their subtree and handlers. Expired bindings
are removed; bounded tree slots are reused with fresh element generations and
monotonically increasing binding tickets. The idle widget cache bound is zero.
Rebound rows receive fresh state instead of inheriting another item's properties.

Async completions return to the owner and call `list.accepts(ticket)` before
applying row data. Validation and application must occur in one non-reentrant
owner operation. Failed staging burns generations: a handle predicted by a
failed factory cannot validate a later binding. Workers must never dereference
the controller or its mutable tree.

`request_focus(key)` queues reveal/realization, including unrealized items.
`update` resolves it before input capture; `restore_focus` runs after
`begin_frame`, before routing. Retained child controls keep their physical focus.
Manual scrolling preserves logical focus without snapping back; an unrealized
focused item uses the viewport as physical proxy. Moving focus outside the list
releases logical focus.

Up/Down and Home/End navigate logical order. `configure_navigation` supplies
optional semantic action IDs for keyboard/gamepad input. Navigation queues work
for the next safe boundary; further keyboard/action activation in the frozen
batch is suppressed until realization, and is not replayed against stale rows.
Pointer interaction uses captured visible geometry. Row handlers supply their
normal click/focus responses through `row.bind`.

Source replacement preserves the first visible key and within-row displacement
where possible. Removing the anchor selects the nearest surviving logical index;
shrinking content clamps offsets. A previously bottom-aligned nonzero range
remains bottom-aligned. Removing logical focus chooses its old index, or the new
last item; an empty source clears focus. Logical rebasing replaces physical anchor
compensation only in this owned viewport, avoiding duplicate adjustments.

Retired handlers keep resources through disable/end cleanup at the next input
boundary. New generations cannot receive old raw input, capture, listeners or
guarded async completions. Call `detach()` between frames to remove content and
retire registrations, then run input cleanup or orderly shutdown. Destruction
invokes no user callbacks and makes retained adapters inert.

## Budgets and publication

Defaults allow 128 realized rows, two overscan rows per edge, 16 nodes and eight
bindings per row. Visible rows plus overscan must fit; otherwise the update fails
instead of publishing an incomplete range. Tree, input and command budgets remain
independent. Pending retirements consume input admission: run the normal input
boundary between scroll updates.

Source lookup, allocation, admission, cancellation or binding failure preserves
the published tree revision, rows, input table and source. Accepted jobs join
before buffers are released. Cancellation is checked again after factory staging.
External side effects in factories cannot be rolled back; use RAII resources.

Preparation uses immutable inputs and exclusive numeric outputs. The injected
`frame_executor` uses thresholds/grain and at most `max_chunks + 1` jobs, never
one job per logical item. Defaults keep this cheap kernel inline. An unchanged
source/viewport/realized range performs no PMR allocations or factory calls.

Changed ranges stage a copy of the bounded live UI tree and input table for
atomic publication. Transient cost scales with host tree capacity/live nodes
and input capacity, not logical source count. Old/new leases can coexist until
cleanup; factory assets have separate budgets. This is not a zero-allocation
recycling claim. EnTT, shared ownership and arbitrary consumer allocations are
not all PMR allocations.

`ui.virtual.update` profiles complete realization and reports unwinding failures
through injected diagnostics. Joined metadata uses existing phase instrumentation.
Global AUTO/ON/OFF profiling policy remains authoritative.

## Validation and measurement

Tests compare reproducible variable-height sources against a retained sequential
reference, including bounds, clips and scroll ranges. Other contexts cover
empty/short/large lists, rapid reversal, focus/removal/reorder, nested masks, stale
tickets, failed bindings, admission, cancellation and every observed PMR
allocation point in replacement staging.

`atlas_ui_benchmark --virtual` reports full CPU realization, capture, layout,
paint and input latency at 1,000, 100,000 and 10,000,000 logical rows. It reports
realized/live nodes, admitted jobs, PMR requested live/peak bytes and allocation
calls, plus global C++ allocation requests. It excludes source startup, storage,
decoder/GPU work and OS/allocator retained memory. `--virtual-smoke` checks
structural bounds without timing assertions. These observations do not establish
process RSS, universal speedups or production editor readiness.
