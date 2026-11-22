# Scroll viewports

`ui::scroll_layout` owns zero or one content subtree. Its border is sized with the
normal requested dimensions, intrinsic hint, padding and constraints. The visible
viewport is the border minus padding, with each available extent saturated at zero.
All values use logical units; the host supplies its current logical viewport after
resize or DPI changes. There is no render-to-texture pass.

```cpp
const auto viewport = tree.create();
tree.set_panel(viewport, ui::scroll_layout{}); // Vertical scrolling by default.
const auto list = tree.create(viewport);
tree.set_panel(list, ui::vertical_layout{4});
// Add ordinary children to list.

tree.set_panel(viewport, ui::scroll_layout{false, true, {0, 120}});
const auto source = tree.snapshot();
const auto geometry = layout.calculate(source);
const auto scrolling = geometry.find_scroll(viewport);
const auto drawing = paint.calculate(source, geometry);
```

## Sizing and offsets

An enabled scroll axis excludes the child's extent from the owner's natural
measurement. The owner's own intrinsic hint, fixed request and constraints still
apply. A root can stretch to the application's finite viewport; a viewport inside
a naturally sized stack needs its own size hint, request or minimum on that axis.
No infinite child constraint or parent-relative content-size equation is used.

During arrange, enabled axes give the child a finite slot at least as large as its
natural outer extent. The child retains its natural size and leading alignment on
those axes, including when shorter than the viewport. Disabled axes use the normal
slot and authored alignment. A vertical list therefore fits the viewport width
and can exceed its height. Enable both axes for unconstrained two-axis content.

The content extent includes its border, its own padding and its outer margins.
Viewport padding belongs outside the clipped content rectangle. Each enabled
axis has range `max(0, content_extent - viewport_extent)`; disabled axes have zero
range and offset. Empty content has zero extent and range. A zero-size viewport
still has finite metadata and clips all child drawing. At offset zero the content
outer start aligns with the viewport start; at maximum offset its outer end aligns
with the viewport end. Explicit margins remain visible spacing at those endpoints.

Offsets must be finite; negative and excessive finite requests clamp in the
published result. Layout alone preserves the authored request for later range
changes. An active input session starts from the effective offset and normalizes
its enabled viewport requests at the command boundary, so content shrink does not
leave a latent oversized interaction offset that returns when content grows.

## Publication, painting and input

`layout_snapshot::scrolls()` stores sparse immutable `scroll_item` records only for
scroll owners. `find_scroll()` checks the full identity and returns viewport,
content extent, range and effective offset. These values share the layout packet's
session, revision and lifetime. Mutate the tree on its owner at a frame boundary,
then use matching element/layout snapshots for both paint and input. Retained
packets remain unchanged; stale layout publication and mixed revisions are rejected.

Offsets translate descendants during presentation. They do not change hierarchy,
authored positions, intrinsic dimensions or numeric layout rectangles. The owner
background does not move. Child clips intersect the transformed inner viewport
with inherited clips, including empty intersections. Painter/layer order is
unchanged. Input uses that same preparation, affine map and clip. The existing
rectangular-mask restriction applies: rotated masks that cannot be represented by
an axis-aligned scissor are rejected.

An offset-only change advances the revision but reuses exact numeric root caches:
zero measured nodes, zero arranged nodes and no layout jobs admitted. Sparse
metadata is rebuilt before cache commit. Failed allocation, validation or
cancellation publishes no partial packet. `ui.layout.scroll` is an optional
profiling scope under the shared diagnostics instrumentation policy.

## Authoring and cost

XML uses `panel="scroll"`, `scroll-horizontal`, `scroll-vertical`, `scroll-x` and
`scroll-y` attributes on the element. Defaults are horizontal disabled, vertical
enabled and zero requested offset. It uses the same typed policy in runtime and
editor documents. Prepared UI containing scroll panels uses envelope version 5;
scrollbar links or authored wheel action routes use version 6. Older documents
retain their existing versions and remain readable. Old envelopes cannot introduce
a scroll panel or newer chrome metadata.

The existing injected dispatcher, layout thresholds and shared PMR resources are
used. There is no private scheduler. Offset-only reuse avoids numeric layout work,
but immutable input/output capture and paint traversal still visit the retained
tree. Clipping does not virtualize it or bound large-list CPU/memory usage.
Realization and recycling are tracked in [#122](https://github.com/ocarizr/atlas-ui/issues/122).

Run `atlas_ui_benchmark --scroll` in Release for median/p95 end-to-end offset update
and paint times plus a separate PMR census. `--scroll-smoke` uses fewer samples.
It retains every item and reports requested PMR bytes, excluding process RSS,
allocator caches, tree default-resource allocations and non-PMR ownership blocks.
There are no hardware-dependent timing assertions.

## Interaction and focus

`input_session` supplies the built-in scrolling behavior without a Lua VM or a
per-container handler. Input stages remain serial on their owner; layout and paint
continue using their injected dispatcher and existing thresholds.

`input_kind::pointer_scroll` carries signed two-axis wheel notches in `scroll` and
logical pointer coordinates in `position`. Positive axes move right/down. One
notch is 40 logical units; fractional high-resolution notches are preserved. A
horizontal-only viewport maps a vertical-only wheel sample to its horizontal axis.
Two-axis viewports keep the supplied axes. A packet is consumed by the nearest
eligible viewport that moves. If already at its limit, it bubbles to an ancestor;
when a packet partially reaches a limit, that packet is consumed and the next can
propagate. Scrolling over a linked scrollbar addresses its controlled viewport.
Custom capture/target/bubble handlers can stop a packet before the built-in policy.

Wheel packets are transient in `atlas::input`; they never become held controls.
The Windows adapter normalizes wheel deltas to notches and screen coordinates to
client positions. Application coordinate conversion remains the host's boundary.
UI replay rejects nonfinite wheel values before admitting any prefix of the batch.

Eligible focus transitions automatically reveal the new target. Intermediate
items move minimally, already visible items stay stable, first items request offset
zero and last items request the maximum. First/last are evaluated per enabled axis
among eligible focusable descendants in numeric layout order. An oversized item
uses its leading edge; a single first/last item chooses the leading boundary.
Content padding and margins remain explicit endpoint spacing. Nested reveal
accounts for the inner displacement before requesting an outer offset.
`ensure_visible(target)` requests the same policy without changing focus.

Within scroll domains, portable arrow keys move focus in numeric list order;
Home/End choose the first/last eligible item. Vertical ordering is primary for a
two-axis domain. There is no wrap. Reaching a nested domain's edge can propagate
navigation outward. Bind semantic performed actions with
`configure_scroll_navigation({.previous = previous_action, .next = next_action})`
between frames for keyboard/gamepad action maps. IDs must be distinct and nonzero
when supplied; zero leaves an operation unmapped. An initial focus is selected by
the host or authored Flow entry point. Advanced spatial navigation remains separate.

`scroll_by(viewport, delta)` accepts finite logical deltas for explicit semantic
controls. Repeated requests are bounded by the input packet budget and coalesced
into at most one `scroll_x`/`scroll_y` write per viewport at `end_frame`, after its
callbacks. The command budget is checked for the entire flush before any write.
The host seals/applies commands and captures fresh layout/paint inputs at the
normal frame boundary. Frozen input hit regions are never moved halfway through
a replay batch. With no actual scroll request, input preserves offsets published
by another producer at an earlier sync point. Initial out-of-range normalization
also yields to an independent publication. Conflicting actual scalar requests
retain normal warnings.

Retained focus does not continually request reveal: manual scrolling stays where
the user placed it. When layout insertion/reordering changes a retained focused
item's logical origin, the session compensates offsets where the range permits,
inside-out through nested viewports. Resize/content changes reclamp; focus removal
retires the anchor. Explicit visibility still reserves ordinary layout space.

## Scrollbar chrome

Create an ordinary UI element outside the viewport subtree, author its track size,
appearance and placement, then call `set_scrollbar(track, style)` with a
`scrollbar_style` referencing the scroll owner. `reset_scrollbar` removes only the
chrome link and automatic thumb. The element's normal paint is its track; a solid
thumb in `style.color` is emitted immediately afterward in the same painter context.
Both use existing transforms, opacity and inherited clips. Hidden viewport ancestry
also hides its chrome. Sparse input and layout snapshots retain the link/geometry.

```xml
<element id="vertical_bar" parent="root" width="10" height="80">
  <layout horizontal="end"/>
  <paint fill="true" r="0.2" g="0.2" b="0.2"/>
  <scrollbar viewport="list_viewport" axis="vertical"
             min-thumb="12" hide-when-zero="true"/>
</element>
```

The proportional thumb is capped by track length and the finite authored minimum.
Zero range hides the chrome by default. With `hide_when_zero=false` it remains
visible with a full-track thumb but is disabled. Zero travel never divides by zero.
`layout_snapshot::scrollbars()` and `find_scrollbar()` expose the same immutable
track/thumb rectangles used for painting and hit decisions.

A track click pages by one viewport extent. A thumb press captures the chrome;
move/up use inverse-transformed coordinates and clamp proportional travel, including
outside the track. The thumb hit region is frozen with the frame. Capture persists
across ordinary offset publication, but resize/content changes affecting track,
range, thumb size or presentation transform cancel the drag before further input. Pointer cancel, activation
loss, removal, hide/disable or an admitted dispatch failure also release it. Wheel
input cancels an existing thumb drag before routing. Removed/reused/converted scroll
owners, or chrome reparented into an invalid ancestor/content relationship, leave
the chrome inert until the relationship is corrected, reset or relinked.

Preparation failure before frame admission preserves the previous frame for retry.
An admitted callback/command-budget failure discards pending offsets with other
unsealed commands and clears input ownership. Optional profiling scopes under
`ui.input.scroll.*` cover navigation, reveal, wheel, pointer and flush work. Current
frame snapshots and sparse pending offsets use the supplied shared PMR; no jobs,
locks, private thread pool or cross-thread Lua callbacks are added to dispatch.
