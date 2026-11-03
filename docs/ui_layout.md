# UI layout

`atlas::ui::layout_engine` measures and arranges immutable element snapshots without
accessing a graphics backend. The live tree and engine belong to their creating
coordinator thread. Layout consumes committed properties; pending commands must be
applied before capture. See the [library example](../libs/ui/README.md).

## Sizing and placement

Coordinates are logical floating-point units, with the origin at the top left.
Measure traverses each tree bottom-up; arrange assigns rectangles top-down. Both
traversals are iterative. Child order is insertion order, and result items use
ascending live-slot order. Results are identical for sequential and parallel paths
on the same build and inputs; cross-toolchain bitwise equivalence is not promised.

`layout_style` supplies common intrinsic content size, padding, outer margin,
minimum/maximum constraints and per-axis flow alignment. `tree.set_panel()` selects
a typed `panel_layout`; stack alternatives own their spacing. Canvas-specific
child slots and general visual origins have separate APIs and storage.

| Kind | Child sizing |
| --- | --- |
| `leaf_layout` | No children; intrinsic content plus padding |
| `size_box_layout` | At most one child, placed inside padding |
| `overlay_layout` | Children share the content rectangle; natural size is their outer maximum |
| `horizontal_layout` | Natural outer widths are summed; cross-axis height is the maximum |
| `vertical_layout` | Natural outer heights are summed; cross-axis width is the maximum |
| `canvas_layout` | Anchors/offsets with per-axis desired/stretch child placement |

See [canvas placement and visual origins](ui_canvas.md) for slot formulas, content
measurement, sparse ownership and source compatibility.

Default nodes are overlays. Stack spacing appears only between children. Intrinsic
size is a minimum natural content hint, not a fixed request. Width/height commands
accept a finite nonnegative float for fixed sizing, including explicit zero;
`std::monostate{}` resets that axis to automatic sizing. Requested properties expose
`width_fixed` and `height_fixed` so automatic size cannot be confused with zero.

Own minimum/maximum constraints clamp both desired and arranged sizes. Automatic
`stretch` fills the available slot; fixed sizes and `start`, `center` or `end`
alignment use the desired extent capped by available space before constraints.
Minimum sizes may overflow a parent slot. Stacks default to natural major-axis slots
and support explicit [space distribution, weighted fill/shrink and stable child order](ui_stacks.md).
Wrapping is not implemented. Padding reduces content
space, margins surround child borders, and alignment positions the border within
the remaining slot. There is no clipping in this calculation. Visibility remains
a paint property: hidden elements retain layout space.

Styles/panel policies reject negative/nonfinite extents, margins, padding and spacing. Constraint
maxima may be positive infinity; minima must be finite and not exceed maxima.
Viewport positions may be negative but all positions/extents and resulting edges
must be representable finite floats. Invalid styles, incompatible child counts and
numeric overflow fail explicitly.

## Revision and publication

`tree.set_layout()` changes common sizing. `tree.set_panel()` changes the container
policy; `tree.set_canvas_slot()` changes a child's parent-owned placement.
`tree.set_visual()` changes paint-only transform-origin metadata.
`tree.set_viewport()` changes
the available root rectangle. Topology, actual size-request changes, style and
viewport changes advance the layout revision; opacity/visibility do not.
Visual origins do not change that revision either. Reparenting to a different
parent resets explicit canvas placement; converting a canvas to another panel
clears its immediate children's overrides. Failed validation changes neither.

`calculate(tree.snapshot())` returns owned immutable geometry without publishing.
`update(tree, viewport)` sets the viewport, captures, calculates and publishes.
Manual `tree.publish_layout(result)` validates the complete session, revision,
viewport and handle set before changing any computed value. Stale or foreign
results cannot partially publish. A successful publication clears layout dirtiness
and retains existing paint dirtiness; changed geometry also invalidates paint.

`tree.computed(handle)` returns no value while the node or its ancestry has
invalidated geometry. Acknowledging dirty flags does not make old geometry valid.
`result.find(handle)` instead queries the retained capture: later tree mutation or
destruction does not change that result. Publish immutable results to other threads
through normal synchronization and retain them while borrowing `items()` spans.

Failure returns no partial result and preserves the previously committed cache.
Cancellation before `update` leaves its viewport untouched. After calculation starts,
the requested viewport can remain changed on failure, while computed geometry stays
invalid until a successful publication. Cooperative cancellation joins accepted jobs
before returning; it does not interrupt an allocation or arbitrary resource callback.

## Work scheduling and allocation

The application supplies its shared jobs dispatcher, optional shared PMR resource
and diagnostics services. Layout creates no scheduler. Changed independent roots
run through joined `ui.layout.measure` and `ui.layout.arrange` phases when their
total node count reaches `min_parallel_nodes`. Small work and a single changed root
stay on the coordinator; `max_chunks = 1` disables parallel admission.

The engine caches exact geometry inputs per root. An unchanged revision reuses the
complete result; changed input rebuilds preparation metadata and compares roots.
Unchanged roots reuse numeric geometry. A change within one root currently
remeasures/rearranges that entire root. There is no subtree-level incremental
solver or parallel dependency schedule within a root. Limits default to 65,536
nodes, a 4,096-node parallel threshold and 16 chunks. Thresholds are policy choices,
not automatic timing calibration. `statistics()` reports actual work/admission.

Branch buffers, preparation vectors and result vectors use the supplied PMR;
control blocks and implementation metadata retain ordinary allocation. Retained
results retain their resource. Parallel calculations and worker-side release require
a resource safe for concurrent allocation/deallocation. Capacity is not a global
memory budget or a fragmentation guarantee.

Optional CPU scopes cover capture, calculation, measure, arrange and publication,
following `ATLAS_PROFILING=auto|on|off`. Debug lifecycle logs and failed-operation
logs use injected diagnostics; geometry has no per-node success logging or automatic
metrics export. Diagnostic callbacks cannot reenter the engine or live tree.

Layout does not produce paint packets, render widgets, perform hit testing or run
Lua/MVVM. Further delivery is tracked in the [UI issue](https://github.com/ocarizr/atlas-ui/issues/27).
