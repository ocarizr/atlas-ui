# Renderer

`atlas::renderer` provides a platform-neutral C++20 device and presentation API.
The current backend is Windows Direct3D 12. It depends on `atlas::window` and `atlas::diagnostics`, with
private native dependencies on D3D12, DXGI, pinned DXC and D3D12 Memory Allocator. It has no UI, jobs,
streamer, editor or gameplay dependency.

Installed consumers must load the `atlas_diagnostics` package before importing the
window/renderer target exports, and load native DXC/D3D12MemoryAllocator packages for Windows builds; build-tree consumers receive these prerequisites
through their declared CMake component dependencies.

```cpp
atlas::window::window target{{"Example", 960, 540, true}};
atlas::renderer::device graphics;
atlas::renderer::presentation_surface surface{graphics, target};
while (true) {
    const auto state = target.poll_events();
    if (state.close_requested) break;
    surface.resize({state.width, state.height});
    const auto result = surface.render_frame();
    if (result == atlas::renderer::frame_result::surface_closed) break;
    if (result == atlas::renderer::frame_result::suspended ||
        result == atlas::renderer::frame_result::occluded) target.wait_for_events();
}
```

Use the creating thread for all calls and destruction. Surfaces retain native
device/window lifetimes and drain GPU work before release. The caller supplies
current physical dimensions; zero-size surfaces suspend submission. Device
removal is terminal; destroy/recreate services rather than
continuing to render after native errors. A live-device drain timeout terminates
teardown to prevent release of resources still in use by the GPU.

Optionally pass a shared `std::pmr::memory_resource` as the second device argument;
the application selects and owns allocator policy. Frame bookkeeping retains it.
CPU PMR is separate from GPU allocation and does not override third-party heaps.

For native pixel tests select `adapter_preference::software` and call
`readback_last_frame()` after a submitted frame. This synchronizes GPU work and
returns tightly packed RGBA8 bytes; it is a diagnostic path. `device::info()`
reports adapter identity and actual debug-layer availability. Tests can request
`debug_validation::required` when the host has Graphics Tools installed.

The original startup triangle remains a diagnostic probe. The generic draw API
uses immutable passes; no resource loading is performed here. See the
[renderer contracts](../../docs/renderer.md) for resources,
passes, job recording, content orchestration and production-readiness gates.

After building, run `atlas_game` or `atlas_editor` for a streamed textured quad, `--warp`
for software presentation, `--graphics-smoke-test` for three hidden WARP frames,
or `--smoke-test` for the existing headless bootstrap. Graphics smoke checks require
a textured frame to reach presentation. There are no widgets yet.

See [library diagnostics and build policy](../../docs/diagnostics_policy.md) for optional destinations, `ATLAS_PROFILING=auto|on|off`, trace export and CPU/async timing semantics.

The resource API provides `resource_store` in `<atlas/renderer/resources.hpp>`. See [GPU resources and transfers](../../docs/renderer_resources.md) for typed handles, byte/slot quotas, descriptor retention, upload readiness and destruction fences.

The pass API provides `pass_renderer` and `frame_plan` in
`<atlas/renderer/passes.hpp>`. See [pipelines and ordered passes](../../docs/renderer_passes.md)
for builtins/custom in-memory shaders, fixed vertex ABI, color/depth targets,
blending, viewport/scissor, transactional validation and presentation. Editor and
game render their streamed textured quad through this shared path.

The recording API provides a joined `recording_executor` interface and bounded contiguous
command batches. The optional `atlas::renderer_jobs` adapter reuses an injected
application dispatcher; core rendering remains independent of jobs. See
[parallel recording](../../docs/renderer_recording.md) for ownership, failure
atomicity, cancellation, profiling, benchmarking and application
main/render-thread ownership.

The separate [renderer content consumer](../renderer_content/README.md) provides
streamer-fed image/HLSL preparation, native buffer decoding, fenced texture ownership
and device-local pipeline caching. Core rendering still performs no file access.

The device API provides `device::status()` and typed fail-stop device-loss reporting,
fence-completed GPU duration counters, visibility probes and cross-thread native
presentation through a synchronized window lease. Application-owned
`atlas::render_host` coordinates the persistent render thread; renderer still owns
no thread or pool. See [readiness](../../docs/render_host.md) and the
[remaining renderer/streaming gaps](https://github.com/ocarizr/atlas-ui/issues/26).
