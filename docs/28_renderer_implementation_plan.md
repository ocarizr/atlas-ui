# Renderer foundation

The renderer is a reusable graphics library shared by game and editor. It owns GPU
resources, command submission and presentation, independently of UI layout, ECS,
gameplay, authoring documents or storage. Windows Desktop x64 with Direct3D 12 is
the only implemented backend. Public APIs use standard C++20 types; native code
lives under `libs/platform/windows/`. `ATLAS_TARGET_PLATFORM` selects the target;
unimplemented platforms fail configuration explicitly rather than falling back to
Windows. The SDK-free validation profile reports native graphics as unavailable.

## Six phases before production UI

| Phase | Deliverable and acceptance gate | Status |
| --- | --- | --- |
| 1. Device and presentation | Independent window/device/surface ownership, hardware or WARP selection, real clear and triangle, fenced frame reuse, resize/minimize, DPI, clean shutdown and pixel readback tests; both applications exercise the same implementation | Implemented |
| 2. Resources and transfers | Typed generation-checked buffer/texture handles, GPU memory allocation, upload staging, descriptor allocation, explicit resource states and fence-based deferred destruction; test exhaustion, reuse and in-flight lifetimes | Implemented |
| 3. Pipelines and passes | Shader/pipeline descriptions, immutable draw packets, offscreen targets, depth/blend/scissor, ordered passes and explicit attachments; validate generic 2D/3D primitives without UI semantics | Implemented |
| 4. Job recording | Optional joined executor, one application-owned scheduler, exclusive bounded command contexts, contiguous pass sections, deterministic submission, sequential small-work path and measured grain policy | Implemented |
| 5. Content integration | Streamer-fed image/shader preparation on ready jobs, bounded native buffer decoding, fenced texture ownership, exact-input device-local PSO cache and shared loose-asset application demonstration | Implemented |
| 6. Hardening and readiness | Typed fail-stop device loss, fenced GPU timestamp counters, bounded main/render handoff, visibility probing, multi-surface lifecycle/resource-pressure checks and WARP/hardware measurements | Implemented for the initial Windows foundation |

All six foundation phases now have their initial implementation. Next is a bounded
UI vertical slice with its own correctness/performance evidence; this is not a
claim that every production workload is ready. The resource API and render-pass vocabulary must remain
general enough for future world rendering. Prefer composition of passes and
backend interfaces over requiring consumers to inherit native device classes.

## Phase-one contracts

- `atlas::window` owns native messages and physical client dimensions. The two
  executables declare PerMonitorV2 DPI awareness; the library does not change
  process-global DPI policy. UI logical coordinates belong to a later layer.
- `atlas::renderer::device` owns one direct queue and one monotonic fence timeline.
  A `presentation_surface` owns a flip-model swapchain and two or three bounded
  command-allocator/backbuffer slots. A device may outlive its facade through
  retained surfaces. Each surface retains the native window implementation too.
- All creation, use and final destruction happen on the creating thread. Public
  calls reject foreign threads. Destruction on a foreign thread is a contract
  violation and terminates; it cannot throw an exception safely.
- A slot's allocator is reset only after its GPU fence completes. Frames do not
  unconditionally flush the entire queue. Resize and destruction drain work before
  releasing GPU resources. Zero dimensions suspend submission; same-size resize
  is a no-op. Native close stops further submission without destroying an HWND
  still retained by a surface.
- GPU waits are bounded diagnostic waits on the coordinator. A job completion
  handle is **not** evidence of GPU completion. Phase one does not recover a
  removed device. Failure requires recreation/termination; if teardown cannot
  drain a still-live device, it terminates rather than freeing in-flight memory.
- Debug-layer policy is disabled, when-available or required. Adapter information
  reports whether validation is actually active. WARP allows deterministic
  rasterization tests without requiring a physical graphics adapter.
- Startup compiles builtin HLSL from memory with pinned DXC, once per device.
  There is no per-frame compilation or renderer file access. Shader content and
  caching are later phases. DXC and its dynamically loaded DXIL validator are
  staged beside native executables and included in application packaging.
- Presentation uses RGBA8 UNORM and `FLIP_SEQUENTIAL`, preserving the previous
  buffer for explicit diagnostic readback. Readback drains, copies to a readback
  heap and removes row-pitch padding. It is deliberately expensive and excluded
  from normal frame submission. A future capture-on-request path can enable
  `FLIP_DISCARD` without promising access to discarded contents.
- PMR frame bookkeeping retains a shared application-selected memory resource.
  The app injects `atlas::memory::make_resource()`. This is CPU allocation only;
  D3D12 heaps, driver memory, COM allocations and diagnostic image vectors are not
  redirected through that resource. Phase two integrates D3D12 Memory Allocator for GPU resources, independently of CPU PMR.

## Jobs and streaming boundaries

Phases one through five used main as graphics coordinator. Phase six moves graphics
to an application-owned persistent render thread through `atlas::render_host`.
The renderer itself still creates no dedicated thread or pool. It requires one
creating-thread coordinator and supports exclusively owned worker recording contexts.

Phase one records a tiny clear/triangle workload directly on the owner thread;
dispatching it to workers would add scheduling and ownership transfers without a
useful parallel workload. The renderer does not create another scheduler.

Phase four accepts the existing application dispatcher through `atlas::renderer_jobs`.
Each coarse task exclusively records its assigned command list and attachment
descriptors. All accepted work joins before deterministic coordinator submission.
Workers never wait for storage, presentation or GPU fences. Small workloads bypass
job admission. Calibrate the configurable grain with representative CPU timings;
see [parallel recording and measurement](32_renderer_recording.md). Core rendering
does not link jobs or create another scheduler.

The streamer is the only application-managed storage boundary. UI/world/media
consumers request bytes, decode via ready jobs and provide upload-ready buffers
to the renderer. The renderer retains those bytes until the appropriate transfer
fence completes. It never opens shader, image or media files. Bundle format and
packaging remain a separate dependency; loose assets suffice for the next stages.

## Application thread integration

Creating and managing a persistent render thread belongs to application composition,
not an implicit thread inside the renderer library. Main retains window events,
input and application-state updates; the render coordinator creates, uses and
destroys graphics services and submits/presents ordered work. Create graphics objects
on their final coordinator rather than moving live native objects between threads.

Main-to-render handoff needs immutable snapshots, explicit frame identities,
bounded queue/backpressure, maximum frame lead and latency, failure propagation,
resize/minimize/close coordination and drain/join before native-window release.
Required CPU work finishes before its frame is submitted; any application/render
frame overlap must be explicit. Legacy same-thread surfaces retain the owner-thread
window backend; cross-thread presentation requires a synchronized
window-state handoff contract. Phase six supplies a `presentation_target`
lease; main retains native window ownership until render drain/join completes.

Game/editor now implement this gate with bounded snapshots and continued message
pumping during backpressure and shutdown. Dedicated coordination and parallel
recording remain distinct and share the one application pool. Controlled tests
and diagnostics distinguish main, render and workers. See [readiness](34_renderer_readiness.md).

## Future rendering strategies

Forward+ is a candidate for a future world-lighting pass: it culls lights into
screen tiles/clusters before forward shading. It is not a requirement for unlit
2D UI and does not by itself determine image quality. No lighting architecture is
implemented in phase one. Choose it later from representative scene/light counts
and measurements, without coupling UI composition to the world-lighting path.

Later UI consumers may target a swapchain, an offscreen texture used on world
geometry, or a layout-controlled region containing 3D draws. A viewport controls
pixel bounds; it does not eliminate camera transforms, visibility work, depth
handling or draw cost. Target/depth/pass composition belongs to phases two and
three. UI anchors, world-UI visibility/LOD, passive animation clocks, media textures
and editor preview lifecycle remain outside this renderer increment.

## Validation

`window` and `renderer` have independent CI component products and checks. Renderer
links window; game/editor link both through renderer. Tools remain headless and do
not acquire a graphics dependency. Vendor graphics products reuse the existing
dependency cache. PRs validate; only the main workflow packages.

Native tests use hidden windows and WARP to verify actual pixels, normalized
clear values, triangle interior/background, row pitch, repeated frame-slot reuse,
resizing, zero-size suspension, invalid inputs, owner-thread guards, close
isolation and retained PMR/device/window lifetimes. SDK-free tests cover public
contracts and explicit backend unavailability, not native rendering. Hardware
performance and recovery cannot be inferred from WARP correctness tests.

See [renderer usage](../libs/renderer/README.md),
[window ownership](../libs/window/README.md) and
[Windows checks](10_windows_development.md).

CPU scopes and lifecycle/failure logging follow the [library diagnostics policy](29_library_diagnostics.md). Phase six adds GPU duration counters and a fail-stop device-loss policy; automatic graphics reconstruction remains separate future work.

See [GPU resources and transfers](30_renderer_resources.md) for phase-two handles, budgets, uploads, retirement, readiness fences and the jobs/PMR integration proof.

See [graphics pipelines and ordered passes](31_renderer_passes.md) for phase-three
shader ABI, immutable snapshots, attachment operations, whole-frame validation,
fence retirement and resource-to-surface presentation. Phase four adds optional
parallel recording; [phase-five content integration](33_renderer_content.md) adds
streamed images/shaders and upload/cache ownership. [Phase six](34_renderer_readiness.md)
completes the initial foundation; [known gaps and optimization candidates](35_renderer_and_streaming_backlog.md)
remain explicit before expanding production scope.
