# Stack sizing, distribution and order

`horizontal_layout` and `vertical_layout` share main-axis allocation rules.
Common element sizing stays in `layout_style`; parent-specific order and weights
use `stack_slot`. Public contracts contain no native SDK types.

```cpp
atlas::ui::vertical_layout column;
column.spacing = 4.f;
column.fill = atlas::ui::stack_fill::weighted;
column.overflow = atlas::ui::stack_overflow::shrink;
column.distribution = atlas::ui::stack_distribution::space_between;
tree.set_panel(panel, column);
tree.set_stack_slot(favorite, {-1, 2.f, 1.f});
```

## Measurement and allocation

Measure computes natural child border sizes, including their intrinsic hint,
padding, requested fixed axes and min/max constraints. The stack's natural
main-axis extent sums those borders, outer margins and fixed gaps between children.
The cross-axis extent uses their outer maximum. Distribution and fill do not add
free space to this bottom-up desired-size calculation.

Arrange receives the actual parent content rectangle after padding. It first
reserves child borders, outer margins and `(count - 1) * spacing`, then applies
these explicit policies:

| Policy | Behavior |
| --- | --- |
| `stack_fill::none` | Preserve measured main-axis border sizes |
| `stack_fill::weighted` | Divide surplus among participating automatic axes, weighted by `grow` |
| `stack_overflow::allow` | Preserve sizes when content does not fit |
| `stack_overflow::shrink` | Divide the required reduction among participating automatic axes, weighted by `shrink` |

Both weights default to one. Zero opts that child out of the corresponding
operation. Weights must be finite and nonnegative; very large and very small
positive weights are accepted. Fixed child width/height requests, including fixed
zero, never participate in parent growth/shrink on that axis. Their own min/max
constraints still clamp the original request.

Growth distributes **additional** space; equal weights do not force differently
sized children to have identical final sizes. Children reaching their maximum stop
growing and remaining space is redistributed. Shrinking respects minima and
redistributes reduction when a child reaches its minimum. Padding inside each
child border, outer margins and the parent's minimum fixed gap are not separate
shrink targets.

If fixed extents, margins, minimums or gaps still exceed the available extent,
overflow remains explicit at the trailing edge. There are no negative border
sizes, automatic gap collapse or hidden clipping. Overflow is not repositioned
with a negative center/end packing offset. Clipping belongs to painting.

The parent controls main-axis allocation; the child's ordinary main-axis alignment
does not undo it. Cross-axis alignment and sizing retain the common layout rules.
Nested panels receive their arranged border rectangle before arranging their own
children. Calculated `desired` sizes and committed requested properties are not
changed by the resulting allocated `bounds`.

A content-sized stack has no artificial surplus. For example, a stack arranged at
its natural extent with automatic main-axis sizing and `start` alignment in its
parent consumes only its desired size, even when fill is enabled. An automatically
stretched root can instead receive the viewport's larger extent. See
[common sizing](ui_layout.md) for the parent-slot alignment contract.

## Remaining-space distribution

After grow/shrink, remaining free space is clamped to zero or greater. Fixed spacing
is a minimum gap; distributed space is added to it. With `n` children and remaining
free space `F`:

| Distribution | Leading space | Additional interior gap |
| --- | --- | --- |
| `start` | 0 | 0 |
| `center` | F / 2 | 0 |
| `end` | F | 0 |
| `space_between` | 0 | F / (n - 1), when n > 1 |
| `space_around` | F / (2n) | F / n |
| `space_evenly` | F / (n + 1) | F / (n + 1) |

An empty stack performs no division or placement. A single child in
`space_between` stays at the leading edge; `space_around` and `space_evenly` center
it. Fill can consume all free space before packing. If all eligible children hit
their maxima, distribution handles the remaining surplus.

Four fixed-height 23 buttons in a 100-unit content region with zero margins/gap
leave 8 units: `space_between` adds 8/3 between them; `space_evenly` adds 1.6 at
both edges and between buttons. Four automatic 23-unit hints with equal fill
weights instead become 25-unit borders. Four 20-unit borders with 2.5 leading and
trailing margins already occupy 100, so no distributed space remains.

## Stable parent-owned order

`stack_slot::order` is an ascending signed integer key, defaulting to zero.
Favorites can use -1 to precede the default group. Equal keys preserve physical
sibling insertion order, even when public slot indices have been recycled.

Ordering changes private numeric sibling links during immutable preparation. It
does not recreate/reparent live elements or change `first_child()`/`next_sibling()`.
Result items remain in ascending live-slot order for lookup. This key determines
layout positions; it does not set paint depth, stacking contexts or input priority.
Those consumers must use their own explicit contracts.

Nondefault metadata uses sparse EnTT components and a separate retained
`element_snapshot::stack_slots()` span. Default weights/order need no component on
every child. `stack_slot_for()` returns the implicit default under a stack, no
value under another panel or for a root, and throws for invalid/foreign handles.
`reset_stack_slot()` removes an override. Changing the same parent's
horizontal/vertical policy preserves compatible slots. Conversion to another panel kind
clears them; reparenting to a different parent resets them. Reparenting to the
current parent and equal metadata assignments are no-ops.

## Determinism, memory and failure

Allocation uses capped weighted solves with double intermediates and checked float
output. Growth uses sorted breakpoints and suffix weights. Shrink computes retained
capacity above minima to avoid losing small available sizes by subtracting nearly
equal huge numbers. Float-conversion residual is offered to the last participating
child in resolved sibling order when that correction respects bounds. Float output
is not a promise of exact real-number partitioning.

Sorting/allocation is O(k log k) for the relevant sibling set. Ordering and weighted
allocation use reusable PMR scratch per prepared/arranged branch; no per-child job
or private scheduler is created. Unchanged default order avoids sorting; default
natural/start/allow policy retains the direct placement path. This does not establish
a whole-process memory budget or eliminate control-block/EnTT allocations.

Slot changes advance layout revision and invalidate the affected branch/ancestry.
Exact root reuse compares slot values, policies and resolved links. Stale results
reject atomically. Allocation, cancellation and kernel failure return no partial
geometry and preserve the previously committed cache. Accepted jobs join before
temporary data is released. Metadata and live tree operations remain owner-affine.

The shared dispatcher, PMR and injected diagnostics services are reused. Mutations
emit optional `ui.tree.set_stack_slot` CPU scopes and guarded failure logs; numeric
work stays inside existing measure/arrange scopes. Profiling follows
`ATLAS_PROFILING=auto|on|off`. There is no automatic telemetry export.

Matching headers and archives must be rebuilt together because panel/snapshot
layouts include these policies. Sibling relationships use the separate
[boundary contract](ui_boundaries.md); optional worker measurement uses the shared
[layout execution policy](ui_layout_execution.md). Grid and wrapping remain absent.
