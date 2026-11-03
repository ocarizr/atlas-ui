# Renderer contracts

`atlas::renderer` exposes SDK-free C++20 device, resource, pass and presentation
contracts. The implemented native backend is Direct3D 12 on Windows Desktop x64.
CMake selects native sources under `libs/platform/windows/`. SDK-free builds reject
native creation explicitly; no other platform backend is implemented.

## Ownership and synchronization

One creating-thread coordinator owns a device's direct queue and monotonic fence
timeline. Surfaces retain native device/window lifetimes and bounded frame slots.
Allocator reuse waits for the slot's fence, rather than flushing the whole queue
each frame. Resize/destruction drain affected work. Zero dimensions suspend
submission. Native objects are not transferred between coordinator threads.
Foreign public calls are rejected; foreign final destruction terminates.

GPU waits have diagnostic bounds. CPU job completion does not establish GPU
completion. Device loss is typed and fail-stop; automatic reconstruction is not
implemented. If a live device cannot drain safely, teardown terminates rather than
freeing in-flight resources. Debug-layer policy can be disabled, optional or required;
adapter information reports actual validation. WARP provides native pixel tests.

## Resources and drawing

Typed generation-bearing handles identify buffers, textures and views. D3D12MA
allocates GPU resources independently of shared CPU PMR bookkeeping. Staging,
descriptors and resource slots have explicit budgets and fence retirement.
Immutable passes describe fixed-ABI vertices, shaders, color/depth attachments,
blend state, viewport/scissor and ordered draws. Whole-frame validation precedes
submission. Shader compilation consumes in-memory HLSL through pinned DXC.

Presentation copies initialized RGBA8 UNORM offscreen output into a matching
`FLIP_SEQUENTIAL` swapchain. Diagnostic readback drains and removes row-pitch
padding; it is excluded from normal submission. These choices have measurable
copy/retention costs and do not imply optimal batching.

## Consumer boundaries

The core renderer opens no files, creates no job pool and creates no render thread.
`renderer_jobs` adapts the application's existing dispatcher for joined recording.
Each worker has exclusive recording context ownership; accepted work joins before
deterministic coordinator submission. Small workloads stay sequential.

`renderer_content` consumes streamer bytes, prepares images/shaders through ready
jobs, retains fenced uploads and caches device-local pipelines. The application
owns main/render lifecycle through `render_host`: main pumps window messages while
the render coordinator creates/uses/destroys graphics services. Immutable snapshots,
bounded lead, backpressure and drain/join precede window release.

No widget semantics, layout, world simulation or lighting/Forward+ implementation
is embedded in rendering. Generic passes and targets are composition interfaces,
not a requirement to inherit native device classes. Current limitations and
optimization evidence are tracked in
[Renderer](https://github.com/ocarizr/atlas-ui/issues/26).

## Reference guides

- [GPU resources](renderer_resources.md)
- [Pipelines and passes](renderer_passes.md)
- [Parallel recording](renderer_recording.md)
- [Content preparation](renderer_content.md)
- [Render host and readiness](render_host.md)
- [Diagnostics policy](diagnostics_policy.md)
- [Renderer API usage](../libs/renderer/README.md)
