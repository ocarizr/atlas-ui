# Graphics pipelines and ordered passes

The renderer exposes `<atlas/renderer/passes.hpp>`. The public API is
platform-neutral; the selected Windows implementation compiles HLSL with DXC and
records Direct3D 12 commands under `libs/platform/windows/renderer/`. Backend
selection still follows `ATLAS_TARGET_PLATFORM` and native integration settings.
SDK-free builds validate data contracts and explicitly report the missing backend.

## Minimal submission

The application owns a device and a `resource_store`. Create one `pass_renderer`
over that store; it shares the device, queue, CPU PMR resource and diagnostics.
All renderer calls and final destruction belong to the device coordinator thread.

```cpp
using namespace atlas::renderer;
resource_store resources{graphics};
pass_renderer renderer{resources};
const auto output = resources.create_texture({640, 360, texture_role::color_target});
const std::array<vertex, 3> triangle{{
    {{0, 0.7F, 0}, {1, 0, 0, 1}, {}},
    {{0.7F, -0.7F, 0}, {0, 1, 0, 1}, {}},
    {{-0.7F, -0.7F, 0}, {0, 0, 1, 1}, {}}}};
const auto geometry = resources.create_buffer({sizeof(triangle)});
resources.upload(geometry, std::as_bytes(std::span{triangle}));

draw_packet draw;
draw.pipeline = renderer.create_pipeline({});
draw.vertices = geometry;
draw.vertex_count = 3;
pass_description pass;
pass.color.target = output;
pass.draws.push_back(draw);
const std::array passes{pass};
const frame_plan frame{passes};
const auto completion = renderer.submit(frame);
surface.present(resources, output); // Surface extent must match the output.
```

Uploads and passes use the same ordered queue, so submission does not require a
CPU wait between them. `completion` describes GPU execution, not job completion.
Use `resources.wait(completion)` for an explicit diagnostic sync point. Normal
applications limit outstanding frames, collect completed work and wait only when
reusing bounded frame slots. Editor and game use this same generic pass path.

## Fixed pipeline and vertex ABI

The initial primitive is a non-indexed triangle list. `vertex` contains a float3
position, float4 color and float2 UV, with a checked 36-byte interleaved layout.
Positions are transformed by the draw's row-major 4x4 matrix; clip-space Z is in
the D3D zero-to-one interval. The vertex color is multiplied by a float4 tint.
The same input can express 2D primitives or transformed 3D geometry.

Pipelines select colored or textured builtins, opaque/straight-alpha/premultiplied
blending, optional depth writes/testing and a linear or nearest clamp sampler.
Rasterization uses solid fill with no face culling and one sample. Color targets
are RGBA8 UNORM; depth targets are D32 float. Depth uses less-or-equal comparison.
The alpha equation is source alpha plus destination alpha times one minus source
alpha. Premultiplied inputs must already have RGB multiplied by alpha.

Custom shaders receive trusted, in-memory UTF-8 HLSL and explicit entry identifiers.
The renderer does not install an include handler or open shader files. Content
resolution and preprocessing belong above the renderer/streamer boundary. The
supported shader resource ABI is at most 80 bytes of `b0` constants (matrix and
tint), one float Texture2D at pixel `t0`, and an ordinary pixel sampler at `s0`,
all in space zero. Reflection rejects other bindings, arrays and oversized
constant buffers. Only a shader actually consuming `t0` requires an SRV in its
draw packet. This is a compatibility check, not a sandbox for hostile shaders.
Custom vertex inputs and outputs must match the fixed pipeline ABI. Compilation
or pipeline creation errors publish no handle or GPU work. Device-local shader/pipeline caching and
asset orchestration belong to [renderer content](renderer_content.md).

## Attachment and ordering contracts

- A pass has one color target and optional depth target with matching dimensions.
  A depth-enabled pipeline requires that depth attachment; a depth-disabled
  pipeline requires a pass without it. Use a later color-load pass for overlays.
- `clear` initializes the entire attachment; `load` requires initialized stored
  contents. `store` publishes its result; `discard` invalidates those contents
  for future loads, sampling, readiness and readback until reinitialized.
- Texture creation alone does not initialize data. Depth targets are initialized
  by passes, have no SRVs and do not accept raw uploads. Color targets can also
  receive a normal RGBA upload. The explicit state API rejects incompatible roles.
- Draws have optional pixel viewport and scissor rectangles. Defaults cover the
  attachment. Rectangles and vertex ranges are checked before submission; empty
  scissor rectangles deliberately draw no pixels. Matrices and tints must be finite.
- `frame_plan` deep-copies pass values and nested draw vectors. Subsequent caller
  mutation cannot alter the snapshot. Handles describe identities, not ownership:
  releasing a referenced handle before submission makes the snapshot invalid.
- Validation resolves the entire snapshot and simulates resource states and
  initialization in stable pass order before recording. A later pass can sample
  a stored earlier target without a CPU wait. Sampling the active color attachment
  through any of its SRVs is rejected. An invalid later pass submits no earlier
  work and changes no live resource metadata.

The frame is an ordered sequence, not an optimizing render graph. State changes
are derived from validated use and emitted on the shared direct queue. Attachment
outputs stay in render/depth state until their next use; sampled inputs become
shader-readable. Diagnostic readback restores the scheduled state.

## Ownership, capacity and presentation

Pipeline handles have renderer-local provenance and generation checks. Release
invalidates them immediately; collection reclaims their slots only after the last
GPU fence. Resource/descriptor fences extend automatically for every submitted
draw, attachment and presentation copy. A pass renderer retains the store state
even after its facade is destroyed, and drains before releasing native state.

`pass_limits` bounds pipelines, passes and draws per frame, and outstanding command
batches. Capacity includes retired objects. Admission failure throws without
blocking or submitting a partial frame. Each command allocator/list and its RTV/DSV
heaps are exclusively owned and reused only after completion. Metadata capacity is
reserved before execution, so retention does not allocate after queue submission.
Persistent registries and command pools use the injected PMR resource; immutable
snapshots, validation scratch, strings and DXC/driver memory use their normal
allocators. No global allocation replacement is installed.

`surface.present(resources, output)` copies initialized RGBA data from the same
device into a matching swapchain, restores source state and protects its lifetime
through the copy fence. It does not stage through CPU memory. Closed or suspended
surfaces submit no copy. Frame slots retain store ownership until safe reuse;
completed resources can still be collected independently. The extra offscreen
copy is an explicit foundation choice, not a claim of optimal presentation cost.

## Diagnostics and jobs

Pipeline creation, validation, submission, presentation and shutdown expose CPU
scopes through inherited diagnostics and `ATLAS_PROFILING=auto|on|off`. Lifecycle
and failure logging remain independent of capture. There is no per-draw info log. Separate fenced GPU duration counters
cover pass execution; see [timing semantics](render_host.md).

Jobs can prepare exclusively owned CPU geometry and immutable pass descriptions.
Wait for CPU readiness before coordinator submission; do not call the GPU API or
wait for GPU fences from a CPU worker. The integration test samples an asset
prepared with attached jobs and uploaded through the normal resource store.
The default submission stays sequential. [Parallel recording](renderer_recording.md)
uses optional joined command recording through the existing application scheduler.

WARP tests inspect pixels for color, textures, depth, blending, viewport/scissor,
custom shaders and swapchain copies. Queue gates exercise capacity and retirement
without sleeps. Invalid-input tests check that fences and initialization remain
unchanged. These checks do not establish hardware throughput or device recovery.

[Content preparation](renderer_content.md) and [render-host lifecycle](render_host.md)
are separate consumers. The fixed primitive ABI does not implement indexed/instanced
geometry, additional formats/MSAA, general material layouts, lighting or UI semantics.
