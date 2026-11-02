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
removal is terminal for this increment; destroy/recreate services rather than
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
now uses immutable passes; no resource loading is performed here. See the
[six-phase plan](../../docs/28_renderer_implementation_plan.md) for resources,
passes, job recording, content orchestration and production-readiness gates.

After building, run `atlas_game` or `atlas_editor` for a streamed textured quad, `--warp`
for software presentation, `--graphics-smoke-test` for three hidden WARP frames,
or `--smoke-test` for the existing headless bootstrap. Graphics smoke checks require
a textured frame to reach presentation. There are no widgets yet.

See [library diagnostics and build policy](../../docs/29_library_diagnostics.md) for optional destinations, `ATLAS_PROFILING=auto|on|off`, trace export and CPU/async timing semantics.

Phase two provides `resource_store` in `<atlas/renderer/resources.hpp>`. See [GPU resources and transfers](../../docs/30_renderer_resources.md) for typed handles, byte/slot quotas, descriptor retention, upload readiness and destruction fences.

Phase three provides `pass_renderer` and `frame_plan` in
`<atlas/renderer/passes.hpp>`. See [pipelines and ordered passes](../../docs/31_renderer_passes.md)
for builtins/custom in-memory shaders, fixed vertex ABI, color/depth targets,
blending, viewport/scissor, transactional validation and presentation. Editor and
game render their foundation triangle through this shared path. Content orchestration and hardening remain later phases.

Phase four adds a joined `recording_executor` interface and bounded contiguous
command batches. The optional `atlas::renderer_jobs` adapter reuses an injected
application dispatcher; core rendering remains independent of jobs. See
[parallel recording](../../docs/32_renderer_recording.md) for ownership, failure
atomicity, cancellation, profiling, benchmarking and the separate application
main/render-thread integration gate.

Phase five adds the separate [renderer content consumer](../renderer_content/README.md):
streamer-fed image/HLSL preparation, native buffer decoding, fenced texture ownership
and device-local pipeline caching. Core rendering still performs no file access.
