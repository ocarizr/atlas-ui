# Renderer foundation readiness

Phase six completes the initial Windows Desktop x64 foundation for a controlled
2D UI implementation. It establishes explicit thread ownership, bounded handoff,
fail-stop device-loss behavior, actual GPU timing and integration/pressure checks.
It does not certify arbitrary production workloads or close every optimization
candidate. Remaining work is tracked in the [renderer/streaming backlog](35_renderer_and_streaming_backlog.md)
and the [job-system backlog](25_job_system_backlog.md).

## Main and render ownership

Game and editor now create an application-owned `render_host::render_loop`.
Main owns the native window, message dispatch, the shared job pool and publication
of frame snapshots. The dedicated render coordinator creates and destroys the
device, surface, resource store, streamer, content loader and scene. CPU recording
and ready decoding reuse that same application job pool; no second pool is created.

`window::share_presentation()` grants a platform-neutral `presentation_target`.
The endpoint caches the native handle on main and reads synchronized published
window state on render. It does not read mutable WNDPROC fields from another
thread. Owner-thread polling, state queries, resizing and close publish snapshots.
The window must outlive all leases and leased surfaces; violating that destruction
contract terminates rather than attempting native destruction on the wrong thread.
Legacy same-thread window/surface construction remains supported.

The host's factory, frame handler, shutdown and handler destruction execute on the
render thread. Accepted snapshots have positive increasing IDs and FIFO order.
`try_submit()` returns false at capacity without blocking main or overwriting
accepted work. Default capacity is two queued snapshots, plus at most one executing
snapshot. This bounds frame lead by count, not total payload bytes or latency.
Capacity is configurable in [1, 64]. Native waits and workload duration still affect
latency; the host does not promise a real-time deadline.

`frame_payload` carries an optional shared immutable CPU packet without coupling
the host to UI/world rendering types. Producers must finish constructing the
snapshot and stop mutating aliases before admission. A const shared pointer does
not freeze external mutable aliases. Packets must not own thread-affine window or
graphics objects; destruction can occur on either handoff thread. Completed and
discarded packets are released before host completion, outside queue locks.

Required CPU work for a frame must finish before its packet is submitted. Any
overlap with subsequent frames is explicit through the bounded queue. Job
completion is not evidence that the GPU has completed the frame.

## Shutdown, backpressure and failure

`request_stop()` closes admission and drains accepted snapshots. Initialization,
render and shutdown failures preserve the original exception; queued unprocessed
snapshots are counted as discarded and released. Observational counters distinguish
accepted, completed, failed and discarded frames. `finished()` publishes only after
handler/graphics destruction; it is not merely a job-body-completed indication.

Call `stop_and_join(pump)` on the main owner. It keeps dispatching messages while
the render coordinator drains, then joins and rethrows any original failure.
The pump runs outside all queue locks, as do factories, handlers, diagnostics and
packet destruction. The game/editor exception path also uses this pumping drain.
The destructor's joining fallback is not a replacement for explicit pumping when
the handler uses native presentation.

The default drain deadline is 15 seconds and is configurable. Exceeding it logs a
critical diagnostic and terminates: the host cannot safely detach a live handler
that borrows windows or application resources. User handlers/observers must return
and cooperate with cancellation; arbitrary callbacks cannot be forcibly preempted.

This ownership matters because [DXGI can synchronously send messages to the window owner](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/dxgi-best-practices).
Main must not block waiting for render initialization, resize or destruction while
withholding that message dispatch.

## Device loss and visibility

`device::status()` returns a platform-neutral `device_status` with the native reason
bits and a healthy/removed/reset/hung/driver-error classification. GPU boundaries
check native health; terminal loss is reported with `device_lost` where a removal
reason is available. Native call failures retain operation context. The current
policy stops the graphics session/application; it does not silently retry a failed
submission or reconstruct a subset of resources on an invalid device.

Removed-device teardown releases invalid native objects without waiting on a live
queue. A timeout on a device still reporting healthy remains fatal rather than
freeing in-flight resources. Fence allocation rejects exhaustion before emitting
the reserved removal sentinel. Readiness checks report loss even for an otherwise
uninitialized resource.

After DXGI reports occlusion, presentation uses `DXGI_PRESENT_TEST` before recording
another surface submission. Repeated occlusion probes do not advance the graphics
submission count. Zero-size/minimized snapshots suspend rendering. Main continues
processing events and applies a modest event-wait backoff while inactive. Another
surface can remain active when one is suspended or closed.

## CPU and GPU diagnostics

The host injects optional logging and CPU scopes for admission, initialization,
frame execution, shutdown and pumping drain. Lifecycle/failure messages identify
the coordinator boundaries; graphics/streaming/content inherit their own services.
There are no per-draw information logs or automatic telemetry transport.

With compiled profiling and an injected profiler, pass submissions lazily allocate
one timestamp-query pair and one native readback buffer per retained frame slot.
The first ordered list starts the sample, and the last ends/resolves it. CPU
recording completion never authorizes reading that data; collection waits for the
protecting GPU fence. Query storage is then reused with its frame context.

`pass_renderer::gpu_timing()` exposes completed sample count and latest duration.
`renderer.gpu.duration_us` is a Chrome counter whose **value is an actual GPU
timestamp interval**. Its event timestamp is the CPU collection time, not a claimed
calibrated GPU/CPU execution alignment. It measures the submitted graphics passes,
not uploads, presentation, display latency or the entire application frame.
Optional measurement failures log a warning without replacing a rendering result.

GPU queries/readbacks obey `ATLAS_PROFILING=auto|on|off`, including compiled-out
Release probes under `auto`. Native profiling allocations are bounded by the pass
frame limit and are separate from the resource store's D3D12MA admission counters.
See Microsoft's [GPU timestamp contract](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing).

## Validation and measurements

The independent `render-host` suite uses controlled gates to validate bounded FIFO
admission, pumping during shutdown, origin-thread rules, payload cleanup and original
failure propagation. `renderer-readiness` adds cross-thread native presentation with
two surfaces, repeated resize/suspend/close and repeated resource/descriptor/upload
retirement under small quotas. Occlusion statuses are injected at the native
presentation boundary for deterministic checks; actual D3D12 recording/fences remain
real. Device-loss tests remove an isolated test device through the D3D12 API.

SDK-free checks validate generic host contracts and explicit native unavailability;
they do not substitute for native presentation tests. ASan, static analysis and
controlled concurrency do not prove race freedom; native Windows TSan is unavailable.

Build `atlas_renderer_readiness_benchmark` and run:

```powershell
atlas_renderer_readiness_benchmark assets/renderer
atlas_renderer_readiness_benchmark assets/renderer --warp
```

This manual probe performs 16 warmups and 64 samples with four shared workers,
a 70-byte loose BMP, ready-job WIC decoding, fenced upload, 64 textured draws and
threaded presentation. It deliberately waits on the render coordinator for each
read to measure round-trip acquisition/decoding; the game/editor frame loop instead
polls content readiness and continues rendering a placeholder. CPU submission
timings exclude source preparation, shader construction, GPU waits and presentation.
GPU samples cover the pass interval only. Report configuration/instrumentation and
use an otherwise idle host. PMR peaks exclude ordinary allocations, codec/driver
internals, allocator caches and process working set. No FPS threshold is inferred.

Example local run on 2026-11-02, Windows x64 Debug, `ATLAS_PROFILING=auto`
(enabled), native debug layer disabled, after build/test processes had finished:

| Adapter | Read + decode P50 / P95 (us) | CPU submission P50 / P95 (us) | GPU passes P50 / P95 (us) |
| --- | --- | --- | --- |
| NVIDIA GeForce RTX 5070 Ti | 4526.9 / 4965.5 | 180.7 / 195.0 | 15.744 / 16.896 |
| WARP / Microsoft Basic Render Driver | 4557.2 / 5027.9 | 627.5 / 671.9 | 2157.6 / 2429.2 |

Both runs observed 31,814 peak requested PMR bytes, 70 peak encoded bytes, one
in-flight I/O and two queued snapshots. This tiny repeated asset is not a cold-disk,
large-media or production UI benchmark. The read/decode interval also includes
coordinator polling/wakeup overhead; it does not isolate decoder cost. Release
with profiling `auto` reports GPU samples as unavailable; use Debug or explicitly
enable profiling for that measurement. These samples establish a reproducible
baseline, not a cross-machine performance guarantee.

The next implementation is a small shared UI runtime: programmatic rectangles and
images, layout/clip/paint, immutable frame publication and deterministic sequential
references before increasing job granularity. Bundles, an editor and world rendering
are not prerequisites for that first bounded UI slice.
