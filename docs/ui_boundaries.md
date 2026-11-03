# Sibling layout boundaries

`layout_boundaries` places a dependent element before or after a sibling's
untransformed outer margin edge. Use ordinary stacks, overlays and canvas anchors
when they express the layout; these relationships address directional dependencies
between otherwise separate sibling subtrees.

## Scope and authoring

Only direct children of the same `canvas_layout` with
`canvas_measure::intrinsic` can participate. The children can be leaves or panels
with arbitrarily nested descendants. References include session, slot and generation.
Roots, ancestors, descendants, other canvases and foreign/stale identities reject
with `std::invalid_argument` before mutation.

Each dependent has at most one optional relationship per axis. The source may
differ between axes. A related axis must have a default `canvas_axis`; setting a
nondefault anchor, pivot, offset or sizing on that axis rejects in either authoring
order. The unrelated axis retains ordinary canvas placement. Common margins,
padding, min/max constraints and explicit width/height requests remain active.

```cpp
const auto canvas = tree.create();
tree.set_panel(canvas, atlas::ui::canvas_layout{});
const auto list = tree.create(canvas);
tree.set_panel(list, atlas::ui::vertical_layout{4.f});
// Add list entries with their desired sizes.
const auto panel = tree.create(canvas);
atlas::ui::canvas_slot placement;
placement.horizontal.anchor_max = 1.f;
placement.horizontal.sizing = atlas::ui::canvas_sizing::stretch;
tree.set_canvas_slot(panel, placement);

atlas::ui::layout_boundaries relations;
relations.vertical = atlas::ui::boundary_relation{
    list, atlas::ui::boundary_side::after, 8.f,
    atlas::ui::boundary_sizing::remaining,
    atlas::ui::boundary_overflow::reject};
tree.set_boundaries(panel, relations);
const auto result = layout.update(tree, {0.f, 0.f, 1280.f, 720.f});
```

Here list measurement determines the panel's top edge. The panel fills the
remaining content height, subject to its own constraints. This does not anchor
the list to the panel or infer any reverse dependency.

`tree.boundaries(element)` returns committed values by copy, or an empty value
when none exist. `reset_boundaries(element)` removes both relations. Repeated
identical assignments and resets are no-ops. Empty values are allowed on any live
element and do not require a canvas parent. Only nonempty values use sparse EnTT
components; there is no property bag or per-node relationship allocation.

## Axis geometry

Let `P` and `E` be the resolved parent content origin and extent after padding.
Let `S0` and `S1` be the source's border start minus leading margin and border end
plus trailing margin. Target leading/trailing margins are `M0` and `M1`.

```text
after:  boundary = S1 + gap; available = max(0, P + E - boundary - M0 - M1)
before: boundary = S0 - gap; available = max(0, boundary - P - M0 - M1)
size = clamp(natural, target_minimum, target_maximum)
after:  target_start = boundary + M0
before: target_start = boundary - M1 - size
```

The finite nonnegative `gap` separates outer margin edges. With `desired` sizing,
`natural` is the measured border size. With `remaining`, it is `available` unless
the axis has an explicit fixed request, including zero. Fixed requests use their
measured constrained size. No opposite-edge equality is promised after maximum
clamping or a fixed request. Alignment and canvas pivots do not change a related
axis. Desired sizes are not rewritten by arrangement.

`boundary_overflow::allow` preserves constraints even when the target overflows
the parent content rectangle. `reject` fails the entire layout if either outer
target edge falls outside that interval. Remaining-space allocation can reduce
an automatic axis to zero, but never below its minimum. Margins and gaps do not
shrink. There is no clipping, source relocation, overlap correction or global
constraint solver. An impossible minimum cannot both fit and be preserved.
Unrelated axes and source rectangles keep their own overflow policies.

Double intermediates precede checked float output. Nonrepresentable coordinates
fail instead of publishing infinities. Unused ordinary canvas calculations are
skipped on replaced axes. No cross-toolchain bitwise identity is guaranteed.

## Dependencies and lifetime

Both axes participate in one acyclic graph. A self-reference or a path returning
to the dependent rejects, including a cycle spanning horizontal and vertical
relations. Authoring validates the complete replacement before changing either
axis. A source must be reset out of its dependents before it can be independently
removed or reparented. Reparenting an unreferenced dependent clears its outgoing
relations; reparenting to its current parent preserves them.

Removing a whole domain/subtree removes all its internal relations. Nested domains
move together without losing their internal relationships. Conversion of a domain
to a noncanvas or `absolute_children` policy rejects while direct children retain
relations; reset them explicitly first. These rules prevent dangling edges and
content-measure/parent-relative size equations, even when the hierarchy is a tree.
They intentionally exclude general cross-level or content-driven relational solving.

## Execution, ownership and failure

Snapshots retain a sparse immutable `boundaries()` span in ascending live-slot
order. Capture does not acknowledge dirty state. Numeric preparation resolves
references to local indices and retains the edges in immutable per-root source
buffers. It validates scope and computes a stable topological order using sibling
insertion priority among ready children. Only private layout sibling links change;
live hierarchy identity, traversal and future paint/input ordering remain separate.

Natural measurement remains bottom-up. The intrinsic canvas excludes child
placement from its own desired size. Arrangement starts with resolved parent
content; sources precede dependents before each child's descendants use its final
rectangle. Relationships do not require iterative geometry correction.

The application supplies the existing jobs dispatcher and thread-safe PMR.
Independent dirty roots may run in joined phases; small/single-root work stays
serial. Relationship processing creates no worker pool or per-child jobs.
Dependency-aware intra-root job scheduling remains separate work in issue #114.

For `N` nodes, `R <= 2N` edges and maximum canvas arity `K`, graph preparation uses
`O(N + R)` PMR scratch and `O(N + R + N log K)` time. Canvas placement uses binary
lookup over sparse edges. Scratch is per changed root, not per job/element.
Authoring cycle validation uses bounded node visitation and existing reserved
traversal storage. EnTT components and ownership control blocks still use ordinary
allocation; this is not a fragmentation guarantee or a measured performance budget.

Relationship edits and source input edits conservatively invalidate layout/paint
through the entire containing relational canvas domain and ancestors. Unrelated
roots remain reusable. Exact cache comparison includes resolved edges and policies.
Revision/session checks reject stale geometry. Validation, allocation, cancellation
or overflow failures publish no partial geometry and retain the previous engine
cache for retry. Retained snapshots/results remain independently readable.

`ui.tree.set_boundaries` uses injected guarded diagnostic failure logging and an
optional CPU scope. Existing capture/calculate/measure/arrange scopes include graph
preparation and placement under `ATLAS_PROFILING=auto|on|off`. No storage access,
metrics collection or telemetry transport is introduced. Rebuild consumers with
matching headers and archives; mixed-version ABI compatibility is not promised.
