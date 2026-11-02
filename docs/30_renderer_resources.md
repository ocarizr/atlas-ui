# GPU resources and transfers

Renderer phase two adds `resource_store`, a platform-neutral device-coordinator
API for buffers, single-mip RGBA8 UNORM textures, shader-resource descriptors and
bounded uploads. Native implementation remains selected by
`ATLAS_TARGET_PLATFORM=windows` and `ATLAS_BUILD_WINDOWS_INTEGRATION`. No storage
access, widgets, materials, draw packets or offscreen passes are introduced.

## Ownership and identities

Create a store from an existing `device`. It retains the device implementation,
inherits its diagnostics destinations and uses its retained CPU PMR resource for
fixed registry tables and transfer metadata. Multiple stores on one device share
the same queue/fence timeline and native GPU allocator; each has separate admission
limits, descriptor heap and handle provenance.

Buffer, texture and descriptor handles have distinct C++ types. Each contains a
store identity, slot and generation. They are process-local library identities,
not pointers, serialized asset IDs or IPC values. Foreign, stale, already released
and incorrectly typed identities fail before native mutation. `release` invalidates
immediately, while `collect` advances the slot generation only after safe reclamation.
Exhausted generation slots are never reused. A live descriptor retains its texture
slot/allocation even after the texture handle is released.

All GPU operations, queries and final destruction run on the creating device
coordinator. The store drains queued use before releasing its live/retired objects.
The device facade may be released earlier because the backend remains retained.
CPU workers must not call these operations or wait for GPU completion.

## GPU and CPU allocation

The pinned vcpkg graphics group now includes MIT-licensed D3D12 Memory Allocator
3.2.0. It suballocates native default/upload/readback resources from GPU heaps.
This complements CPU mimalloc/PMR; it does not replace the CPU allocation policy.
Allocator-internal CPU bookkeeping and COM/driver allocations retain their vendor
allocation behavior. Owned fixed registry and transfer-vector storage uses PMR.

`max_allocation_bytes` charges the native aligned requirement queried before each
resource allocation, including retired resources. This is conservative admission
accounting, not a cap on whole heap reservations: D3D12MA can reserve larger blocks
and optimize individual placements. `reserved_heap_bytes` reports its device-wide
heap reservation separately. It excludes memory owned outside this allocator,
including swapchain buffers, and does not prove zero fragmentation.

Other limits bound resource slots, descriptor slots, in-flight command batches and
logical upload footprint bytes. Native allocation failure preserves the original
exception and does not publish a handle or consume the charged allocation budget.
No unbounded queue or silent eviction is used. Call `collect` at coordinator sync
points, then retry admission when appropriate.

## Upload and readiness

```cpp
atlas::renderer::resource_store resources{graphics};
auto texture = resources.create_texture({width, height});
auto view = resources.create_view(texture);
auto uploaded = resources.upload(texture, tight_rgba_bytes);

// In a normal loop, keep a placeholder until this nonblocking query succeeds.
if (resources.ready(texture)) {
    // Phase three will consume the view through render-pass/draw-packet APIs.
}

// At a deliberate coordinator sync point, never inside a CPU worker:
resources.wait(uploaded);
resources.release(view);
resources.release(texture);
resources.collect();
```

New resources have no initialized contents. Uploads must cover the exact entire
buffer or tight RGBA mip; partial writes, extra bytes, mips, arrays and other pixel
formats are outside this increment. The upload snapshots caller bytes into native
staging before returning. Source buffers may then be released; command allocators,
lists, staging and destination references remain pinned through the returned fence.
Texture rows use native copy footprints rather than assuming a tightly packed GPU
row pitch. Completed command objects are recycled only after their protecting fence.

`ready` tracks the latest full-upload fence, separately from the resource's last
GPU-use fence. Later draws/reads must not make initialized textures appear to be
loading again. A replacement upload creates a new readiness fence. `state` reports
scheduled state rather than execution completion. Explicit `transition` calls use
the same ordered queue and preserve initialized contents.

`completion_token` belongs to the device queue and differs from a CPU job handle.
Unknown future values and foreign-device tokens are rejected. `mark_used` extends
resource/descriptor retention through an already issued device submission; a view
also extends its texture's lifetime. Future pass submission must register actual
GPU use through this mechanism. Current phase-two transfers already do so internally.

`collect` never waits. `wait` is an explicit bounded coordinator operation and also
collects completed work. Diagnostic `readback` waits, returns exact buffer bytes or
tight RGBA rows, and restores the scheduled resource state; it is not a frame path.
Device-removal recovery and live-device timeout policy remain as documented in
phase one; no retry on a broken native context is promised.

## Jobs, diagnostics and content boundaries

Job workers prepare/decode/generate ready CPU data through the application's one
scheduler. The coordinator observes CPU completion and then submits the upload.
Resources, descriptors and fence bookkeeping are intentionally not dispatched as
tiny jobs. This phase adds no renderer-owned pool or unnecessary payload-copy jobs;
parallel recording remains phase four and storage/codec orchestration remains phase
five. Consumers obtain source bytes from the streamer, never through renderer I/O.

A native memory-consumer integration test uses two attached, capture-free child
jobs to fill independently cache-line-aligned PMR rows, joins them before upload,
and verifies GPU readback. Its small payload proves ownership, scheduler reuse and
the CPU/GPU boundary; it is not a performance recommendation or threshold benchmark.

Injected diagnostics capture initialization, resource creation, uploads, transitions,
descriptor creation, reclamation, readback and explicit waits. Lifecycle logging
remains separate from per-frame work; failures retain their original exceptions.
`ATLAS_PROFILING=auto|on|off` follows the [shared policy](29_library_diagnostics.md).
These are CPU wall intervals, not GPU timestamp measurements. Admission statistics
are observational counters rather than automatic application analytics.

## Validation and next gate

WARP tests use CPU-signaled GPU queue gates, not sleeps, to prove that in-flight
resources/staging/descriptors cannot be reused. Other cases cover exact buffer and
odd-width texture roundtrips, reuploads, transitions, stale/foreign identities,
descriptor parent retention, byte/slot exhaustion, owner-thread checks and readiness
independent of later GPU use. SDK-free validation covers descriptions and explicit
backend unavailability. Native SDK/vendor headers remain restricted to Windows paths.

Phase three will add pipeline/pass descriptions, immutable draw packets, actual
resource bindings, offscreen/depth targets and generic draw operations. No production
UI starts until the remaining [renderer foundation gates](28_renderer_implementation_plan.md)
are complete.
