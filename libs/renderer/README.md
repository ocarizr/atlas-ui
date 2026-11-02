# Renderer

`atlas::renderer` provides a platform-neutral C++20 device and presentation API.
The current backend is Windows Direct3D 12. It depends on `atlas::window`, with
private native dependencies on D3D12, DXGI and pinned DXC. It has no UI, jobs,
streamer, editor or gameplay dependency.

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

The startup triangle and builtin in-memory HLSL are foundation probes, not the
future draw API. No resource loading is performed here. See the
[six-phase plan](../../docs/28_renderer_implementation_plan.md) for resources,
passes, job recording, content orchestration and production-readiness gates.

After building, run `atlas_game` or `atlas_editor` for a visible triangle, `--warp`
for software presentation, `--graphics-smoke-test` for three hidden WARP frames,
or `--smoke-test` for the existing headless bootstrap. There are no widgets yet.
