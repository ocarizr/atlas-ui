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
published result. The requested offset remains authored state. If content shrinks
and later grows, the same request is reevaluated against the new range. Interaction
adapters should start incremental scrolling from the published effective offset.

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
older documents retain their existing versions and remain readable. Old envelopes
cannot introduce a scroll panel.

The existing injected dispatcher, layout thresholds and shared PMR resources are
used. There is no private scheduler. Offset-only reuse avoids numeric layout work,
but immutable input/output capture and paint traversal still visit the retained
tree. Clipping does not virtualize it or bound large-list CPU/memory usage. Scrollbar,
wheel, drag and focus reveal are tracked in [#121](https://github.com/ocarizr/atlas-ui/issues/121);
realization and recycling are tracked in [#122](https://github.com/ocarizr/atlas-ui/issues/122).

Run `atlas_ui_benchmark --scroll` in Release for median/p95 end-to-end offset update
and paint times plus a separate PMR census. `--scroll-smoke` uses fewer samples.
It retains every item and reports requested PMR bytes, excluding process RSS,
allocator caches, tree default-resource allocations and non-PMR ownership blocks.
There are no hardware-dependent timing assertions.
