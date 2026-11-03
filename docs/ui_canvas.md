# Canvas placement and visual origins

`canvas_layout` is a typed panel policy selected with `tree.set_panel()`. It places
children inside its arranged content rectangle after its own padding. A child can
be any panel or a leaf. The child's own panel policy arranges its descendants and
does not determine how its parent places it.

Common `layout_style` sizing, `panel_layout` container policy, `canvas_slot`
parent-child placement and `visual_style` transform-origin metadata are distinct
contracts. `horizontal_layout` and `vertical_layout` own fixed spacing; other
panels do not carry unused gap settings. There is no universal string property bag
or per-widget virtual allocation. Grid is not implemented. Dynamic distribution
and explicit order are available for [stack relationships](ui_stacks.md), not canvas slots.

## Canvas axes

Each axis has normalized `anchor_min`, `anchor_max`, `pivot`, signed logical
`offset_min`/`offset_max`, and `desired` or `stretch` sizing. Anchors and pivots
must be finite in [0, 1], and minimum anchors must not exceed maximum anchors.
Offsets must be finite. Negative offsets support intentional overflow.

For a resolved parent content origin `P` and extent `E`:

```text
start = P + E * anchor_min + offset_min
end = P + E * anchor_max - offset_max
available = max(0, end - start - leading_margin - trailing_margin)
size = clamp(desired_or_stretched_extent, child_minimum, child_maximum)
position = start + leading_margin + (available - size) * pivot
```

`desired` uses the measured border extent without shrinking it to available space.
`stretch` uses available space unless an explicit child width/height request is
fixed, including fixed zero. Common min/max constraints still apply. A collapsed
or reversed offset interval has zero available space; it is not a negative size.
Minimums and desired sizes can overflow. No clipping or automatic conflict solver
is applied here.

Coincident anchors describe a point. With zero margins, pivot zero places the
border's start at that point; pivot 0.5 centers it; pivot one places its end there.
Use `offset_min` to translate a point anchor. `offset_max` reduces an interval and
does not independently translate its collapsed start. For separated anchors,
pivot aligns the border inside the margin-reduced interval. Common flow
`horizontal`/`vertical` alignment does not override a canvas slot's pivot policy.

## Desired canvas size

The default `canvas_measure::intrinsic` measures the canvas from its common
intrinsic hint, padding, requested axes and min/max bounds. Children are measured
but contribute no desired size to that canvas. This permits parent-relative
anchors without a circular size equation. Arrangement can still fill the parent's
available slot through ordinary automatic flow alignment.

`canvas_measure::absolute_children` includes child bounds from an origin-zero
measurement, trailing margins and canvas padding. It accepts only zero anchors on
both axes and `desired` slot sizing. Relative anchors and stretch slots reject
before mutation, including conversion of a canvas with existing incompatible
children. Negative child positions may overflow the leading edge; measurement
does not shift the canvas origin to accommodate them. Other panels can freely mix
content sizing and their supported child sizing rules.

## Ownership, capture and invalidation

`tree.canvas_slot_for(child)` returns an implicit default slot under a canvas and
no value under another panel or for a root. Stale/foreign handles throw. Explicit
nondefault overrides use sparse EnTT components; default slots do not require a
component on every node. `reset_canvas_slot()` removes an override through the same
validated mutation contract. A different parent resets placement, even between
two canvases; reparenting to the current parent is a no-op.

Converting a canvas to another panel clears only its immediate children's canvas
overrides. Their own nested canvas relationships remain intact. Arity and slot
validation complete before conversion, so rejection preserves all metadata.

`element_snapshot` copies common layout, typed panel and visual values into records
and retains a separate sparse `canvas_slots()` span in ascending child-slot order.
Those spans borrow the retained snapshot, not the live registry. Layout preparation
copies only geometry-relevant inputs and sparse overrides into branch buffers on
the injected PMR. Exact cache comparison includes both policies and slot values.
Snapshots and calculated geometry require normal synchronization for cross-thread
publication. EnTT pools and control blocks still use ordinary allocations; this is
not a whole-process memory or fragmentation guarantee.

Slot/panel changes invalidate layout and paint, advance the layout revision and
reject old geometry publication. New operations use injected diagnostic failure
logs and optional `ui.tree.set_panel`, `ui.tree.set_canvas_slot` and
`ui.tree.set_visual` CPU scopes under the existing profiling policy. They create no
scheduler, storage access or graphics resources. Current parallelism remains across
independent roots; dependency-aware subtree scheduling is tracked in issue #114.

## General visual origin

Every element supports `visual_style::origin`, defaulting to normalized center.
`tree.set_visual()` validates the origin and invalidates subtree paint without
changing layout revision, desired sizes, arranged rectangles or numeric cache reuse.
The live registry stores only nondefault visual metadata. Snapshots copy its value.

`resolve_transform_origin(bounds, visual)` resolves this origin to a finite logical
point in the untransformed border rectangle. It is independent from canvas
placement pivot and available on flow children and roots as well. Layout applies
no rotation or scale. Future paint and input consumers must explicitly use these
metadata; boundary relationships refer to untransformed layout geometry.

## Source compatibility

Consumers must rebuild headers and archives together. Select panels with
`tree.set_panel(element, vertical_layout{gap})` rather than setting the removed
`layout_style::kind`/`spacing` fields. Common sizing continues through
`tree.set_layout()`. This separation changes public record/archive layouts; there
is no mixed-version ABI guarantee.
