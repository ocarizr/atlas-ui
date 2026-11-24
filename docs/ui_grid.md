# Grid layout

`ui::grid_layout` arranges individually authored children into rows and columns.
Children may be different widgets, panels or nested grids. It does not instantiate
entry templates or populate ViewModel collections. Identical templates also do
not imply identical measured dimensions. Existing horizontal and vertical stacks
remain efficient one-dimensional container policies.

## Sizing

`grid_definition` declares rows, columns and nonnegative gaps. `grid_layout`
retains validated immutable track vectors through shared ownership; snapshots
copy the small policy instead of duplicating vectors per element. Equality uses
actual values with a shared-identity fast path for exact root cache reuse.

| Unit | Natural measurement | Arrangement |
| --- | --- | --- |
| `automatic` / XML `auto` | Child outer desired contributions | Preserve measured extent |
| `fixed` | Nonnegative authored size | Preserve authored size |
| `fraction` | Child outer desired contributions | Add a weighted share of positive spare space |

Fractions specify weighted **growth from natural sizes**. Equal weights do not
guarantee equal final widths when natural contributions differ. Equal fixed
tracks or a separate equal-entry extent contract provide uniform dimensions.
Deficit preserves natural/fixed track extents; ancestor scroll/clip policy governs
overflow. Common child alignment and min/max constraints still apply within cells.

Single-cell contributions precede spans; both passes preserve source child order.
Spans include internal gaps and outer margins. Unmet desired extent is distributed
over nonfixed tracks, using unit weight for auto tracks and authored weights for
fraction tracks. All-fixed spans retain their explicit geometry. Parent intrinsic
hints, padding, requested sizes and constraints apply after content measurement.

## Placement and admission

`grid_slot` is parent-owned metadata: an optional complete zero-based row/column
position and positive spans defaulting to one. Absent positions use automatic
placement. Completely default slots allocate no live sparse component.

All explicit cells are reserved first. Automatic entries then scan forward in
stable sparse order without backfilling earlier holes. Overlap is rejected; an
overlay within a cell provides intentional layered content. Grid placement does
not change live hierarchy order or painter stacking contexts.

Both explicit axes define fixed topology, including empty tracks. One empty axis
grows automatic tracks: implicit rows use row-major order over declared columns;
implicit columns use column-major order over declared rows. Both cannot be empty.
A default grid has one weighted column and implicit automatic rows. Empty implicit
tracks have no intrinsic extent of their own.

Admission is bounded to 65,536 logical cells per grid, with checked spans and
coordinates. The occupancy map requests at most 8 KiB of bit storage. Pathological
sparse search is limited to 4,194,304 occupied-cell tests. These are explicit
numeric limits, not whole-process memory guarantees. Widgets and assets retain
their independent tree/input/resource budgets.

`validate_grid_slot` checks local spans/coordinates. `resolve_grid_slots` resolves
full occupancy without a live tree, accepts aliased input/output spans and changes
output only after successful validation. Tree setters validate local metadata;
layout preparation checks complete occupancy. Failures preserve the previous
immutable layout result/cache. Reparenting and nongrid conversion clear
incompatible slots; generation reuse starts with default placement.

## Authoring

```xml
<element id="properties" panel="grid">
  <grid column-gap="8" row-gap="4">
    <columns>
      <track unit="fixed" value="160"/>
      <track unit="fraction" value="1"/>
    </columns>
    <rows/>
  </grid>
</element>
<element id="label" parent="properties" panel="leaf" text="Width"/>
<element id="field" parent="properties" panel="leaf">
  <grid-slot row="0" column="1" row-span="1" column-span="1"/>
</element>
```

The `grid` section requires a grid panel. Empty axis elements preserve implicit
growth; an absent section uses the default definition. Unknown/duplicate sections,
invalid tracks, incomplete coordinate pairs, incompatible slots and collisions
fail content validation before runtime publication. Runtime asset reads retain
the streamer boundary.

Prepared UI version 7 stores grids and optional child slots. Panel tags are
appended without renumbering existing kinds; versions 2–6 remain readable.
Documents without grid features retain their earlier version-selection rules.
XML, native and prepared construction use the same portable placement contract.

## Execution and measurement

The owner prepares sparse immutable cell records. Measure/arrange kernels use
exclusive PMR axis scratch and stable reductions, with cooperative cancellation.
Natural track scratch is reconstructed for arrangement. Existing exact root
reuse remains authoritative; no extra persistent topology/track cache is assumed.

Independent roots and bounded subtree measure ranges use the application-injected
dispatcher. No per-cell jobs or private pool are created. Cheap work stays inline
under existing thresholds. Grid kernels belong to normal `ui.layout.prepare`,
measure/arrange and subtree diagnostics; sparse edits expose
`ui.tree.set_grid_slot`. Global profiling AUTO/ON/OFF remains authoritative.

Tests cover sizing/spans, both implicit orientations, empty topology, ordering,
admission, lifetimes, exact caching, joined jobs, cancellation, PMR failure,
scroll/clip/input composition, virtual row transactions and XML/prepared parity.

`atlas_ui_benchmark --grid` rotates serial and independent-root execution over
16x16 and 32x32 grids, including four dirty roots. Each frame checks exact numeric
layout and ordered paint equivalence. Timing includes mutation, capture, layout,
paint and prior packet retirement, excluding startup, storage, Lua, font shaping,
input dispatch and GPU work. PMR live/peak bytes and separate untimed C++ allocation
requests are not RSS and must not be added together. `--grid-smoke` checks
equivalence without hardware-dependent timing gates.
