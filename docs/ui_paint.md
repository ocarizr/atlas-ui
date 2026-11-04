# Ordered UI painting and clipping

`atlas::ui` prepares immutable rectangle geometry from an `element_snapshot` and
its matching `layout_snapshot`. Session, layout revision, viewport and element
identities must match. Appearance-only changes can reuse calculated geometry;
the geometry revision does not version appearance. Paint captures the supplied
appearance, never reads the live tree and publishes no partial packet.

## Appearance and stacking

`element_tree::set_paint` sets an optional sparse `paint_style`, separate from
sizing and parent-owned layout slots:

- `fill` draws the border rectangle; containers need no background.
- `color` is normalized linear straight-alpha RGBA. Ancestor opacity multiplies
  alpha; an invisible ancestor suppresses descendants.
- `texture` is a consumer-resolved resident key; zero selects solid color.
  `uv` selects a normalized region. No file or graphics API is exposed.
- `translation` offsets this element and descendants without changing layout.
- `clip` intersects the translated border with the inherited rectangular clip.
- `layer` orders sibling groups: **smaller values are foreground**.

Every element establishes an isolated stacking context. Its background precedes
its children; children visit larger layers before smaller layers. Equal-layer
children retain hierarchy insertion order, independent of stack layout order.
Root groups use layer then stable captured slot order. A background root and all
its descendants stay behind a foreground sibling root, regardless of descendant
layers. Tree depth and worker completion order do not determine paint depth.

Packets store back-to-front quads. Reverse iteration gives front-to-back paint
candidates for future hit testing, not a complete interactive-element index:
nonpainting and invisible elements have no quad. Oversized descendants are not
clipped to the parent's border unless that parent explicitly enables clipping.

## Rectangular masks

The logical viewport is the initial clip. Nested clips intersect through
`intersect_clip`; empty coverage emits no rectangle. Oversized translated content
retains its original geometry and UVs while its parent's mask stays fixed.
The intersection contract can serve future pointer hit testing. This implements
neither scrolling/focus reveal nor virtualization. Arbitrary shape/alpha masks,
scale and rotation are outside this rectangle foundation.

## Jobs and ownership

```cpp
atlas::ui::paint_limits policy;
policy.execution = atlas::ui::paint_execution::chunks;
policy.min_parallel_quads = 4096;
policy.max_chunks = 8;
atlas::ui::paint_engine paint{shared_jobs, policy, shared_memory, diagnostics};
const auto source = tree.snapshot();
const auto geometry = layout.calculate(source);
const auto packet = paint.calculate(source, geometry);
```

`sequential` is the default. `roots` batches independent nonempty root groups;
one root stays inline. `chunks` divides the resolved painter sequence into
contiguous tessellation ranges. Both respect the quad threshold and chunk cap.
This is runtime construction policy, not a CMake switch or automatic timing
controller. Layout scheduling remains independently configured.

The coordinator resolves stacking, inherited state and clips iteratively before
dispatch. Workers run the same six-vertex rectangle kernel as the reference,
reading immutable ranges and writing exclusive slices. One attached root admits
at most `max_chunks` children and returns without waiting. The coordinator joins
the complete attached scope before publishing. Limits are 65,536 captured nodes
and 64 chunks. There is no per-widget job allocation, private pool, recursive C++
traversal or merge ordered by worker arrival.

Input, scratch and output use the injected shared PMR resource; it must support
destruction on every packet-consuming thread. Shared control blocks, EnTT and
scheduler allocations remain separate. Every calculation rebuilds the paint
sequence; reusable scratch and appearance caching are absent. Cancellation,
allocation failure and rejected job admission return no packet; previous packets
remain valid. Statistics are owner-thread observations. Access declarations and
ASan do not prove race freedom or eliminate false sharing.

Optional scopes are `ui.paint.calculate`, `ui.paint.order_clip`,
`ui.paint.tessellate`, `ui.paint.chunk` and `ui.paint.join`. Initialization/release
use debug logs; failed preparation emits one warning. There are no per-quad
success logs. Profiling follows `ATLAS_PROFILING=auto|on|off`.

## Renderer handoff

`atlas::ui_renderer` depends on UI, renderer and render-host; core UI depends on
none of those graphics/native libraries. `ui_renderer::frame_payload` retains a
CPU snapshot for the existing immutable main/render-thread handoff.
`paint_renderer` is created, submitted and destroyed on the GPU coordinator.

The consumer resolves positive texture keys to resident store-local descriptors.
Missing/duplicate keys fail before GPU allocation. The adapter performs no image
loading, decoding or streaming. Keep descriptors/textures valid through submit;
the renderer then retains their resources through queue completion.

Submission projects logical coordinates into the physical attachment, uploads one
contiguous vertex buffer and uses effective clips as scissor rectangles. Physical
clip edges round outward to cover intersecting pixels; this is not fractional
analytical mask coverage. Only adjacent draws with identical pipeline, texture
and scissor are batched. Per-vertex colors preserve transparency and painter
order. Two reusable pipelines implement solid and textured straight-alpha fills.

The temporary geometry handle is released after submission; native storage is
reclaimed only after fences complete. Renderer resource, staging, draw and
in-flight limits apply independently. Consumers bound frame lead and collect
completed resources. Per-frame upload and CPU vertex conversion remain
optimization candidates; there is no persistent GPU geometry cache.

Both applications demonstrate shared clipped panels/textured rows. Main owns
authoring/layout/paint; the render thread owns graphics and resident images.
UI placeholders and asset-request binding belong to the streamed-image contract.

## Measurement

`atlas_ui_paint_benchmark` compares sequential, root-batched and contiguous-chunk
preparation using actual canvas/overlay/stack trees with nested clips,
translations, textures and transparency. There is no grid layout algorithm in
this benchmark or the current container set.

One four-worker pool and one numeric layout reference serve all policies. Policy
order rotates, with three warmups and 20 retained samples per policy. Timed work
includes input capture, complete paint calculation and prior-packet retirement;
output comparison is outside timing. XML reports raw samples, P50/P95, separate
capture/calculation/retirement times, compiler/instrumentation identity and a
separate untimed requested-PMR census. CPU timings exclude GPU upload/recording,
rendering, streaming and decoding. PMR bytes exclude other heaps and allocator
caches; they are not process RSS.
The census covers the paint engine's resource and retained old/new paint packets;
the tree, captured inputs and calculated layout use separate resources.
Tree capacity matches each scenario's node count rather than scanning 65,536
reserved slots for a tiny template. The separate C++ census records cumulative
allocation requests/bytes, including overlapping PMR upstream requests; do not
sum those counters or interpret them as peak live heap. Benchmark-only allocation
wrappers remain installed but counting is disabled during timed samples. The PMR
probe forwards to the standard new/delete resource; it does not compare allocators.
Forced policies compare up to eight root groups and four versus sixteen numeric
chunks on four workers. Their admission threshold is one quad, so the probe also
exposes scheduling overhead on tiny work; the API's default threshold is 4,096.

`--smoke` checks exact reference output with fewer samples and no speed assertion.
D3D12 WARP pixel/readback tests cover painter order, clipping, resident textures,
alpha composition, adjacent batching and fence retirement.

### Observed baseline

Two local Windows x64 Release runs used MSVC 19.44.35229.0, four workers on a
20-hardware-thread host, profiling/checker disabled and no concurrent local
build/test workload. CPU affinity was not pinned. These are observations for
this probe, not portable frame budgets or a comparison with other UI products.
Run `atlas_ui_paint_benchmark` without arguments to reproduce the report.

First-run total CPU preparation P50/P95, in microseconds:

| Template | Nodes / visible quads | Serial | Root groups | 4 chunks | 16 chunks |
| --- | ---: | ---: | ---: | ---: | ---: |
| Small | 21 / 16 | 3.4 / 5.9 | 3.1 / 5.3 | 28.9 / 61.8 | 64.2 / 86.0 |
| Nested cards | 4,129 / 2,600 | 1,047.3 / 1,227.4 | 1,102.3 / 1,355.5 | 1,111.2 / 1,360.0 | 1,149.5 / 1,281.1 |
| Large canvas | 16,449 / 7,892 | 4,085.6 / 4,768.7 | 3,992.2 / 4,187.3 | 4,060.0 / 4,436.8 | 4,226.4 / 4,598.5 |
| Eight canvases | 8,264 / 4,544 | 2,128.0 / 2,485.2 | 2,113.5 / 2,533.5 | 2,172.3 / 2,368.6 | 2,158.2 / 2,519.7 |

There was no stable overall scheduling win. The second large-canvas run had
serial/4-chunk P50 of 4,105.5/3,935.0 microseconds, but its single-root policy also
measured 3,910.2 while admitting **zero jobs**. Such variation cannot be attributed
to parallelism. Eight-canvas root scheduling changed from 2,128.0/2,113.5
serial/root P50 to 2,090.1/2,128.3 in the repeat. Small forced chunks regressed
substantially. These observations retain the sequential default; they do not rule
out parallel clipping/dependency preparation or heavier future paint kernels.

Large-canvas untimed allocation census:

| Policy | Paint PMR peak bytes | Paint PMR retained bytes | PMR calls | C++ requests / cumulative bytes | Admitted jobs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Serial | 5,756,720 | 1,957,216 | 9 | 1 / 96 | 0 |
| Root groups | 5,756,736 | 1,957,216 | 10 | 1 / 96 | 0 |
| 4 chunks | 5,756,784 | 1,957,216 | 10 | 66 / 8,680 | 5 |
| 16 chunks | 5,756,976 | 1,957,216 | 10 | 222 / 29,320 | 17 |

One old consumer packet remains alive during the next calculation. Retained bytes
describe one finished packet; peak includes transient paint buffers and old/new
outputs. The C++ census does not observe all DLL/CRT allocation paths. Scheduler
reserved queues/pools, other heaps, GPU storage and whole-process fragmentation
remain outside these counters. The root policy admits no jobs for a single canvas.
