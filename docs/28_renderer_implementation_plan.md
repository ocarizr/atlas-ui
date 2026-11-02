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
| 2. Resources and transfers | Typed generation-checked buffer/texture handles, GPU memory allocation, upload staging, descriptor allocation, explicit resource states and fence-based deferred destruction; test exhaustion, reuse and in-flight lifetimes | Planned |
| 3. Pipelines and passes | Shader/pipeline descriptions, immutable draw packets, offscreen targets, depth/blend/scissor, ordered passes and explicit attachments; validate generic 2D/3D primitives without UI semantics | Planned |
| 4. Job recording | One application-owned scheduler, coarse parallel preparation and exclusively owned command allocators/lists, deterministic submission order, sequential small-work path and measured scheduling thresholds | Planned |
| 5. Content integration | Consumer orchestration from streamer buffers through decoding to upload-ready data, shader construction/cache policy, cancellation and retention until upload fences complete; loose assets first | Planned |
| 6. Hardening and readiness | Device-removal reporting/recovery policy, diagnostics integration and CPU/GPU timings, resize/occlusion soak, multi-surface behavior, memory/descriptor pressure and measured budgets on WARP and hardware | Planned |

The production UI gate requires all six phases. A triangle proves presentation,
not readiness for widgets. The resource API and render-pass vocabulary must remain
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
  redirected through that resource. Evaluate D3D12 Memory Allocator in phase two.

## Jobs and streaming boundaries

Phase one records a tiny clear/triangle workload directly on the owner thread;
dispatching it to workers would add scheduling and ownership transfers without a
useful parallel workload. The renderer does not create another scheduler.

Phase four will receive the existing application-owned dispatcher for pure CPU
preparation and coarse recording batches. Each recording task owns its allocator,
command list and scratch data exclusively. Submission remains deterministic and
owner-controlled. CPU workers never wait for storage, swapchain presentation or
GPU fences. Measure a sequential threshold using representative work; do not infer
one from triangle timings or hardware thread count alone.

The streamer is the only application-managed storage boundary. UI/world/media
consumers request bytes, decode via ready jobs and provide upload-ready buffers
to the renderer. The renderer retains those bytes until the appropriate transfer
fence completes. It never opens shader, image or media files. Bundle format and
packaging remain a separate dependency; loose assets suffice for the next stages.

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
