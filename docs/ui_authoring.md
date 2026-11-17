# UI authoring documents

`atlas::ui_content` reads and writes viewport UI XML v1 and constructs an
owner-thread UI instance from a typed asset catalog. `parse_xml` returns
editable data. `instance::build` validates the native layout rules and
compiles bindings and behaviors into a private candidate. `write_xml` returns
canonical UTF-8 bytes for the host's persistence boundary.

```xml
<ui version="1">
  <model name="hud" asset="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000001"/>
  <element id="root" panel="canvas"/>
  <element id="status" parent="root" panel="leaf" width="0">
    <canvas-slot x-offset-min="12" y-offset-min="12"/>
    <paint fill="true" r="0.1" g="0.2" b="0.4"/>
    <bind model="hud" field="enabled" property="visible" mode="one-way"/>
    <action model="hud" name="gain" event="pointer-up">
      <arg type="integer" value="17"/>
    </action>
    <behavior model="hud" asset="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000002"/>
  </element>
</ui>
```

## Identity and validation

Each document has one root. Declare parents before children. Declaration
order preserves sibling order. Element IDs and context names are
case-sensitive and use 1–64 ASCII letters, digits, underscores or hyphens.
`find` resolves a stable authored ID into a live handle for the current
instance. A replacement has new handles and keeps the authored names.

A document can contain up to 4 MiB of XML, 4,096 elements, 64 named models
and 64 levels of ancestry. Parsing rejects unknown fields, malformed values,
unsupported versions, duplicate IDs and missing local references. Syntax
errors identify the source and byte offset. Construction checks property
ranges, panel arity, slot ownership and sibling boundary cycles using the
native UI validators.

| Location | V1 data |
| --- | --- |
| Element | Overlay, leaf, size box, horizontal or vertical stack, or canvas. An omitted width or height is automatic. An explicit zero is fixed. The optional `text` caption is presentation metadata consumed by editor tools. |
| `layout` | Intrinsic size, margins, padding, min/max and alignment. An omitted maximum is unbounded. |
| `paint` | Rectangle fill, clip, layer, RGBA, UV, translation, scale, rotation, transform origin and optional persistent texture or material asset. |
| `canvas-slot` or `stack-slot` | Anchors, pivots, offsets and sizing, or child order and grow/shrink weights. |
| `horizontal-boundary` or `vertical-boundary` | Stable sibling source, side, gap, sizing and overflow. |
| `interaction` | Enabled, hit-test and focusable flags. |
| `bind` | Model, field, property and one-way or explicit two-way mode. One binding per target property. |
| `action` | Model and action, event, route, semantic input filter where applicable, and exact scalar arguments. |
| `behavior` | Script asset and model context. One returned handler table per element in the shared Lua domain. |

Model, script, texture and material use sites fix the expected
[asset type](assets.md#persistent-identity). The same references resolve
against loose files or prepared backing ranges. The host receives the
persistent texture reference for image residency.

## Construction and frames

### Menu navigation

`<atlas/ui_content/flow.hpp>` defines the Flow graph, its versioned XML and the
viewport owner that publishes screen transitions. A flow asset uses the normal
typed catalog. A screen points to a UI document by persistent asset ID. The graph
supplies navigation controls alongside regular UI content.

```xml
<flow version="1">
  <region id="viewport" initial="home"/>
  <region id="tabs" owner="settings" initial="display"/>
  <screen id="home" region="viewport" focus="settings_button"
          asset="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000001"/>
  <screen id="settings" region="viewport"
          asset="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000002"/>
  <screen id="display" region="tabs"
          asset="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000003"/>
  <parameter id="can_open" type="boolean" value="true"/>
  <transition id="open_settings" from="home" to="settings" operation="push"
              control="settings_button">
    <guard parameter="can_open" type="boolean" value="true"/>
  </transition>
  <transition id="go_back" from="display" operation="back"/>
</flow>
```

A transition can bind a source element to pointer-up or to a performed semantic
input action using its numeric `action` attribute. A matching target-phase
control queues the first admitted action and consumes that route before
MVVM/Lua handlers. The host can also submit a transition ID. The source must
own input, and an enabled edge with an exact typed guard must still pass when
its candidate is ready. Parameters hold Boolean, integer, number or text values.

Each region has a stack capped at 16 entries. `push` adds history, `replace` changes its top,
and `pop` closes its top. `back` selects the nearest history in the source
region or an active ancestor. `overlay` keeps the underlying views in their
painter order and suspends their input, clearing focus, hover and capture.
`reload` rebuilds the current screen at its existing history position. Hidden
history entries retain authored IDs and focus bookmarks. Their view instances
are reconstructed on return. Child regions start at their defaults when their
owner screen is shown again after retirement. A transaction can present up to 32
screens at once. Keep persistent application state
outside those view instances.

Create a manager with `load_flow` and `make_flow_loader` on the viewport
coordinator and call `start()`. Call `advance(frame)` after closing previous UI
input frames and before beginning the next one. Continue presenting the
published views while it returns `loading`. The loader gathers UI, ViewModel,
Lua, material and shader bootstrap data through nonblocking typed streamer
reads. The owner composes each ready view. Texture GPU residency follows the
normal host path. A failed or canceled candidate leaves the current history and
presentation intact.

Use `presentation()` in back-to-front order. Give input only to entries marked
`input`, routing each packet to the appropriate viewport or tab region. Call
`restore_focus(view)` after that view's input begin-frame and before dispatching
input. Retiring a view delivers its terminal input lifecycle, then releases
its subscriptions and pending scripting work. `failure()` exposes load and
lifecycle errors. Call `shutdown(frame)` on the owner before releasing the mount.

`validate_flow` checks typed references, region ownership, and structural
reachability. `flow_dead_ends` reports screens with no enabled authored exit.
`plan_flow_path` returns a shortest forward sequence under the declared initial
guard values, including activation of nested regions. A traversal test can
submit each action and observe `committed` before its next step. Back and pop
depend on runtime history and are exercised separately.

#### Prepared menu navigation

Use `prepare_flow` on an authored mount and `encode_prepared` to produce a
versioned `AFLW` v1 record. Preparation resolves every distinct screen through
the UI cooker, checks the authored focus and control IDs, and records each
screen's exact typed dependency revisions. `flow_dependencies` returns the
sorted union for validation or later packaging.

Store the Flow record and each separately encoded prepared UI record under
their original catalog IDs and authored revisions. A shared screen has one
UI payload even when several menu IDs reference it. At runtime use
`load_prepared_flow` and `build_prepared_flow` with the active mount. Each
screen must match its recorded dependency contract before its normal owner
publication. The prepared loader stages Lua and shader bytes through the
streamer. Embedded ViewModel and material data are composed from the
prepared UI record.

The Flow codec uses the shared little-endian version, length and payload
checksum envelope. It preserves declaration order, typed guard values and
asset identities. Native ViewModel factories remain optional. Flow manager
construction uses the validated graph directly.

The Release benchmark includes 32 menus sharing one UI asset. On Windows x64
with clang-cl and a Core Ultra 7 265KF, local median graph decode was 82.4 µs
for XML and 29.7 µs for prepared Flow. Decode plus manager construction had
a median of 104.8 µs for XML and 76.3 µs for prepared Flow. The prepared
measurement includes mounted revision checks. Both measurements stop before
screen streaming and composition.

### Prepared UI and materials

`<atlas/ui_content/prepared.hpp>` exposes the typed preparation and construction
path. A cooker calls `prepare_view(ui_id, options)` on a typed XML mount and
`encode_prepared` to obtain bytes. The resulting record includes the ordered
UI descriptors, the material templates and overrides they use, one validated
schema per referenced ViewModel asset, and a sorted
table of required asset IDs, types and authored revisions. The table includes
models, scripts, textures and shaders referenced by the view or its materials.

A runtime can use `decode_prepared` with owned bytes or `load_prepared` to read
the prepared representation through the streamer. `load_prepared` expects the
UI asset's normal typed identity and the exact binary extent in the mount.
`build_prepared` checks all recorded revisions against the active mount before
building the private runtime instance. It feeds the embedded material descriptors
and ViewModel schemas to the existing composition path. Separate named model
contexts share immutable schema metadata and keep independent values. A matching
generated factory can still supply a fresh ViewModel. Lua, textures and shaders
use their existing source and residency contracts.

Version 2 is little-endian with an `AUIP` signature, 16-bit version, 32-bit payload
length and 32-bit FNV-1a payload checksum. The reader checks the complete envelope
before allocating typed records. It bounds the file at 16 MiB and validates
counts, references and the exact dependency closure. The checksum detects
accidental corruption. Asset identity and integrity at distribution boundaries
belong to the host. The format stores no native handles, paths or C++ object
layouts. Version 2 adds the ViewModel table. Reprepare bytes produced by the
earlier UI/material-only development format.

Run `atlas_ui_prepared_benchmark` in Release for warmed decode and decode-plus-build
P50/P95 measurements on the same 512-element viewport and shared material.
It measures CPU construction from in-memory inputs. The smoke configuration
checks XML equivalence and material results without enforcing a timing threshold.

The UI/material-only development format was measured in a local Windows x64
clang-cl Release run on a Core Ultra 7 265KF with
40 samples per path after three warmups. The view used canonical XML, one
shared shape material, and two job workers. Times exclude GPU work and disk I/O.

| Path | P50 | P95 |
| --- | ---: | ---: |
| XML decode | 3.77 ms | 4.03 ms |
| Prepared decode | 0.47 ms | 0.57 ms |
| XML decode and build | 4.65 ms | 4.88 ms |
| Prepared decode and build | 1.23 ms | 1.40 ms |

The canonical XML view used 306,540 bytes. The prepared record, including its
material and dependency table, used 103,004 bytes.

### Owner-thread composition

`load` reads an authored UI from the streamer. The same constructor accepts
a C++ `document` from a prepared source. Bootstrap reads wait on the owner
during explicit construction. Streamer jobs deliver buffers and the owner
checks their mount stamps before use. Each named context receives its own
ViewModel. A host can supply a [native factory](mvvm.md) for the matching
asset revision. Both paths compile the same bindings and actions. The UI
Lua domain registers each script source once and creates an isolated handler
table per element.

Bootstrap reserves command frame 1 for initial scalar values. Begin owner
frames at 2 or a larger host-selected frame number. Call `begin_models`
before the matching input frame. `synchronize` flushes models in authored
order, applies pending Lua subscription changes, and queues model-to-UI
writes into the open command frame. The host seals and applies those
commands and controls Lua work/result delivery. `write_back` queues an
explicit reverse update. Input dispatch enqueues matching MVVM actions
in declaration order before calling the element's Lua handler.

Build the replacement before retiring the active instance. To deliver
lifecycle notifications at retirement, begin a newer matching model/Lua
frame, shut down input, and synchronize. Destruction releases registrations,
subscriptions and pending Lua work on the owner.

## Animation playback

The host can start an `animation_clip` on a composed instance. Each clip
contains up to 256 tracks. A track names an authored element and controls
one numeric property with up to 1,024 ordered keys. Each key
selects step, linear, or smoothstep interpolation to the next key. The
first and last values hold outside their keyed interval. A new valid clip
replaces the current clip and starts at zero. Stopping leaves the already
applied values in the tree.

Use `advance_animation` with host elapsed time, even while a view is
inactive. It advances an integral nanosecond clock without opening a UI
frame or evaluating properties. `seek_animation` selects an absolute
position. Once playback clamps to the end and reports natural completion
once through `take_animation_completion`. Seeking never emits completion
and clears any unread completion. Loop playback wraps at the duration and
never emits completion.

During an active frame, call `publish_animation` after the final
`synchronize` and other property producers, before sealing the command
batch and updating layout or paint. The player samples the current pose
only when its clock changes or it receives a new clip or seek. Removed
targets are skipped even if their slots have been reused. A clip is
rejected if it repeats a target property or targets any MVVM-bound
property. Other competing writes use the existing frame conflict records.
When the animation is published last, its sample wins that batch.

The playback API accepts C++ clip values. The UI XML schema currently
stores the elements that tracks address.

### Visual motion and layout

Use `translate_x` and `translate_y` for a drawer that slides without
moving its neighbors. Translation uses signed logical units. Place the
drawer under a stationary clipped container and target the drawer
itself. Publish the pose before capturing the snapshot used for the next
input pass. The renderer and hit testing consume the same pose.

`scale_x`, `scale_y` and `rotate` apply scale, then clockwise rotation
in degrees, then translation. Scale and rotation use the normalized
border origin. Descendants inherit the complete transform. Paint and
input retain the original layout geometry. Pointer positions are
inverse-mapped into the receiver's original border coordinates. Focus
reveal uses the viewport bounding rectangle. An element collapsed by
zero scale has no paint or pointer hit area. An existing capture still
receives events, with a zero local position while the transform is singular.

A clip node must resolve to a viewport-axis-aligned rectangle. The
renderer uses scissor rectangles, so a mask that would become a rotated
polygon fails during paint or input preparation. A rotated child inside
a stationary mask is supported.

Use explicit layout tracks for reflow. Width and height set requested
size. Margin, padding and stack spacing take nonnegative logical units.
Canvas offsets take signed logical units and anchors take normalized
values from zero to one. Canvas fields require a canvas parent. Anchor
minima and maxima are validated on the combined command batch, together
with canvas measurement and sibling-boundary rules.

### Interruptible transitions and clip groups

Use `start_animation` for a typed playback handle or register a reusable
definition with `register_animation` and start it by name.
`tween_animation` takes the committed scalar pose as the first key and
replaces concurrent single-track playback on that property.
`reverse_animation` changes direction at the current clock position.
Each player supports pause, silent seek, delay, subranges, playback rate
and looping or ping-pong with optional traversal counts. Cubic easing
uses two normalized value controls at fixed time fractions of one third
and two thirds. Overshooting curves need a separate property policy.

A group schedules clips from one time origin or staggers each once-only,
normal-rate clip after the preceding clip's declared end. Group offsets
are fixed at admission. Later pause, seek or reverse operations affect
the individual handles. Use `bind_animation_action` at setup to start a
clip from a ViewModel action. Authored semantic input routes and Lua
`model.invoke` can reach the same action.

Up to 16 concurrent layers compose per instance. They apply in ascending
priority and creation order, with a warning for equal-priority writers
to the same property. Track selection serves as the layer mask. Blend
weight interpolates over the lower result, and a host can update it
during a cross-fade. Additive playback uses the difference from the first
key and supports translation and rotation. MVVM-owned properties retain
exclusive ownership. Use the legacy single-player API or concurrent
playback on an instance at a given time.

Completion holds the terminal sample for its publication unless restore
is selected. Cancellation holds the last committed value for an exclusive
layer, or reveals remaining layers. Restore reveals other layers or the
captured base, including automatic width or height. An overlapping live
layer continues to control the property after retirement.

Drain `take_animation_events` on the owner. Events are grouped by player
creation order and then by traversal order. Equal-time keys preserve
declaration order in both directions. A traversal excludes its starting
endpoint and includes its destination. Seeks neither enqueue nor replay
markers. A finite clip under reduced motion jumps to its terminal pose
on advancement and emits completion. Infinite decorative loops freeze.
Elapsed time and undrained event limits fail atomically so the owner can
retry with a smaller interval.

### Named UI states

Register a group with `register_animation_states`. Give it a fixed list
of numeric channels, one complete pose per state, and a shared duration
and curve. A group owns its channels for the lifetime of the instance.
Groups on different channels can transition concurrently. Targets must
have explicit numeric source values when the transition starts.

`set_animation_state({group, pose})` captures the last committed pose
and starts a cross-fade whose weight comes from the runtime clock. A
repeated request for the same destination continues the existing blend.
A different request starts from the last visible values, even if the
previous destination was never reached. Reduced motion publishes the
requested destination directly. Completed poses remain applied.

Connect state selection to a typed model action with
`bind_animation_state({context, action}, {group, pose})`. Existing
authored focus, pointer, and semantic input routes, or a Lua action
invocation, can select that pose. The subscription retains resolved
targets and pose indices. Animation never writes a model value. Use
separate unbound presentation channels for effects around model-owned
properties. Timeline and state authoring use the native descriptors as
the runtime boundary.

`spring` is a fixed critically damped easing curve normalized to the
segment endpoints. It is evaluated from the current time without
integrating previous samples, and stays in range for bounded properties
such as opacity.

### Material templates and overrides

A material asset can be a directly usable template or a sparse override of one
template. Reference either by its typed catalog ID in the element's paint section.

```xml
<paint fill="true" material-asset="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000011"/>
```

The template pairs a shader family with typed defaults. A shape template can
select the built-in signed-distance program by declaring `kind="shape"`. A
texture template declares `kind="texture"` and a persistent `texture` ID for its
default image. Either can also identify a custom shader asset with `shader`.

```xml
<material-template version="1" kind="shape" material-radius-tl="0.5"
                   material-radius-tr="0.5" material-alpha="0.8"/>
<material-override version="1" template="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000011"
                   material-alpha="1"/>
```

Use one root per material asset. A texture override can also provide a different
`texture` ID. An override targets a template directly and inherits its shader.
Absent fields inherit, and explicitly supplied values survive serialization even
when they match a built-in default. Vector and color components are independently
addressable numeric slots. An element that references a material asset takes its
material parameters and texture pairing from that asset.

The composition resolves each referenced descriptor once per build, copies its
resolved numeric values into each element and exposes its texture through
`instance::texture`. `construction::materials` also accepts typed prepared
descriptors at the exact mounted revision. This lets a prepared/native source
skip XML parsing while using the same type and slot validation.

The host checks `instance::materials_current(active_mount)` after a catalog
replacement. Material, template, shader and texture stamps participate, including
the mount generation. Build the replacement composition privately and register
its custom shader keys on the graphics coordinator before publishing it. For
each entry in `instance::material_shaders()`, call
`paint_renderer::register_shader({entry.key, entry.kind, entry.hlsl})`. The host
copies the source payload if it crosses threads and publishes texture residency
using the existing texture-binding path. Release superseded shader keys after
the last packet using them has been submitted. GPU fences retain submitted
pipelines and texture resources. Host preview and editor authoring controls can
consume the same descriptor and invalidation APIs.

### Material channels and geometry

A rectangle can opt into `<paint material="shape" fill="true"
material-radius-tl="0.5"/>`. Use `material="texture"` with a texture
asset and optional tint or UV slots. Native hosts can use a
`material_style` as shared defaults, resolve sparse typed overrides
with `resolve_material`, then copy the result to each element. The
runtime animates that copy; authored values and other instances are
independent.

Clips and state groups address material slots through `ui::property`.
Tint interpolates component-wise in linear RGBA with straight alpha.
Texture UV transforms use scale and offset before clamp sampling.
Shape channels control per-corner radii, bevel mix, a rectangle-to-circle
distance morph, a bounded union with one offset circle, local scale,
outline, glow, gradient, and reveal. Shape coordinates are normalized
to the existing quad, so circles become ellipses in non-square quads.
The quad and rectangular hit region stay fixed, including where the
shader produces transparent pixels. Ancestor clips and painter order
still apply.

Animate `material_phase` over zero to one, looping on the same clip
clock as other tracks, to drive the GPU glow pulse. The shader uses
the sampled phase instead of wall time, so pause, seek and replay use
the same visual time. A host can also submit a cached paint list with
a shared cosmetic phase, avoiding a new UI sample for each element.
Material changes dirty paint without invalidating
layout. Equal adjacent material constants still batch. Distinct
constants split draws. Geometry upload currently resubmits six ordinary
vertices per visible quad; the GPU applies analytic coverage inside it.

### Material animation measurements

Build the `ui-renderer` component's `atlas_ui_material_benchmark` target
with native Direct3D 12 enabled. Use Release with `ATLAS_PROFILING=on` for
fence-completed pass timestamps. The executable prefers hardware and reports
the selected adapter. Pass `--warp` to measure the software adapter or
`--smoke` to run the smaller WARP correctness workload used by CTest.

The benchmark animates up to 192 translucent SDF cards in a 1920×1080
offscreen target. It compares a grid of 192 cards, the same grid with
staggered phases, and four layers over 48 cells against a single layer on
those 48 cells. It alternates between sampling every card on the CPU and submitting
a cached paint snapshot with one shared clock value. The shader evaluates
that value plus each card's phase offset. Full-frame pixel comparisons cover
cycle wrap and seeking back to an earlier time. One RGBA8 code value of
rounding variation is allowed between the two paths.

The report gives P50/P95/P99 for CPU packet preparation, CPU adapter
submission and the fenced graphics pass. It also counts submitted draws,
vertex upload bytes and rectangle coverage. Coverage is calculated from
the verified aligned quads before shader evaluation. It counts repeated
coverage over the same pixels even where the SDF produces transparent
output. The GPU pass times include target clear and draws. Upload execution,
readbacks, swap-chain presentation and synchronization waits are outside
the reported GPU interval.

Two clang-cl 19.1.5 Release runs on an RTX 5070 Ti used 64 measured
frames per policy after six warm-up frames. The ranges show the P95 from
each run in microseconds.

| Workload | Draws | Vertex upload | Sampled CPU preparation | Shared CPU preparation | Shared GPU pass |
| --- | ---: | ---: | ---: | ---: | ---: |
| Uniform grid, 192 cards | 1 | 41,472 B | 56.9–58.7 | 0.1 | 8.54–8.90 |
| Staggered grid, 192 cards | 192 | 41,472 B | 58.1 | 0.1 | 11.81–11.84 |
| Four layers on 48 cells | 1 | 41,472 B | 54.0–58.5 | 0.1 | 8.48–8.61 |
| Single layer on 48 cells | 1 | 10,368 B | 26.3–27.0 | 0.1 | 4.90–5.02 |

Sharing the clock reuses the CPU paint snapshot. Both paths still upload
the same vertices and split batches at the same material boundaries. In
the uniform 192-card grid, shared GPU P50 matched sampled GPU P50 at
8.51 µs. On WARP, shared GPU P95 rose from 1.14 ms for one layer to 2.28 ms
for four layers over the same cells. The coverage count excludes any GPU
work outside the submitted rectangle fragments and the timing includes
the fixed target clear.
