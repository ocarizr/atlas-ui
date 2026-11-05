# Streamed UI images

`atlas::ui` contains only image fitting and paint properties. The optional
`atlas::ui_images` integration library connects those values to the existing
streamer, buffer-only image decoder, shared jobs and generic renderer services.
The streamer remains the only application-managed storage boundary. Neither UI
widgets nor the image renderer open files.

## Acquisition and frame ownership

Create `image_cache` on the application coordinator with its existing streamer,
an injected reentrant `image_decoder`, shared thread-safe PMR resource and optional
diagnostics. `request(read_request, image_limits)` reserves a per-image decoded
ceiling before admitting storage/decode work and returns an opaque nonzero key.
Assign that key to `paint_style::texture`. Reuse it across widgets to share a
single request and resident image; repeated requests deliberately create distinct
identities. Released keys are never reused in the process.

Storage completion feeds decoding through the streamer's existing ready-job
delivery. There is no worker wait on disk, second worker pool or extra decode job
submitted by this library. The decoder receives encoded buffers, not paths.
Windows composition uses the existing WIC decoder factory behind the portable
decoder interface.

At a main-thread frame boundary, call `poll()` and capture `snapshot()`:

```cpp
const auto key = images.request({"menu.icon", 0, encoded_size}, {4096, decoded_limit});
// At subsequent frame boundaries; no I/O wait:
images.poll();
const auto states = images.snapshot();
if (const auto* image = states.find(key))
{
    const auto size = atlas::ui_images::intrinsic_size(*image, {32, 32});
    // Apply size to the appropriate leaf's intrinsic layout and sampling source.
}
// Publish matching paint and image snapshots through the existing render host.
```

States are `pending`, `decoded`, `failed` and `cancelled`. Failed entries retain
their original exception; polling never throws that asynchronous failure into
the frame loop. Layout can use the explicit placeholder size before decoding.
The application decides whether intrinsic-size changes require layout; fixed-size
widgets can retain geometry. A snapshot contains immutable metadata and shared
pixel aliases, not a copy of every decoded buffer or native graphics object.

`cancel(key)` requests real storage cancellation and suppresses future publication,
including an already decoded owner entry. `release(key)` also removes the entry.
Previously published snapshots remain valid values and may still contain pixels;
cancellation does not mutate frames already handed off. Destroying the cache
cancels pending requests without waiting. Accepted callbacks own the decoder,
allocation policy and budget lease, never the destroyed cache facade. The
application subsequently drains the streamer and jobs before destroying services.

## Fitting and clipping

An optional `paint_style::image` specifies source dimensions and `image_fit`:

- `stretch` fills the element border with the selected UV region.
- `contain` centers the complete region without changing its aspect ratio.
- `cover` fills the border and crops UVs symmetrically.

Aspect calculations use the selected atlas region's dimensions, not the complete
texture's aspect. Fitting changes the element's painted background, not layout
bounds or child placement. Existing ancestor clips and painter order apply.
Source dimensions must be finite and positive; geometry and UVs are checked
before mutation. This is rectangle image rendering, without nine-slice,
arbitrary masks, video or animated-image playback.

## Residency and reclamation

The graphics coordinator owns `image_renderer`, with existing `resource_store`
and `pass_renderer` services. Its constructor creates one initialized four-byte
gray fallback; poll `ready()` before the first `submit`. Submit accepts immutable
`frame_payload`, target texture, physical extent and optional shared job recording
executor. It resolves distinct nonzero paint keys, admits uploads and uses the
fallback for unknown, pending, failed, cancelled or quota-deferred images.

Upload completion is observed nonblockingly at collection boundaries. A decoded
image is not sampled until its descriptor is ready. Submission records each
sampled image's last-use fence automatically. Unwanted upload owners retain their
charge until both upload and last sampled frame complete. `collect()` never waits.
Native resource retirement retains the corresponding submitted resources.

Transient native capacity rejection defers admission. Other upload failures retain
a failed slot and log a warning; a fresh request key permits retry. Device loss
propagates through the existing fail-stop renderer contract. Automatic device
recreation and asset-ID deduplication/eviction policies are absent.

## Explicit budgets

| Limit | Default | Meaning |
| --- | --- | --- |
| CPU live entries | 64 | At most 4096 owner-visible requests |
| CPU decoded bytes | 64 MiB | Pending ceilings plus retained pixel capacity |
| Per-image request | 4096 pixels per dimension / 16 MiB | Decoder limits and reservation ceiling |
| GPU image slots | 64 | Active plus retiring upload owners, excluding fallback |
| GPU logical resident bytes | 64 MiB | RGBA payload including fallback and retiring owners |
| Upload attempts per frame | 4 | Bound graphics-coordinator admission work |

CPU admission throws `overflow_error` when entry or decoded capacity is exhausted;
the caller chooses when to retry. There is no hidden pending-admission queue.
After successful decoding the reservation shrinks to `pixels.capacity()` bytes.
An accepted callback or old snapshot keeps its lease charged after cancellation,
entry release or cache destruction. This prevents premature reuse of a budget
while decoded storage still exists.

These are specific accounting contracts, **not process-memory bounds**. CPU
metadata, shared control blocks, codec scratch, allocator caches and encoded
streamer buffers have separate costs. GPU accounting measures logical RGBA
payload; native allocation alignment, upload staging and partially failed native
allocations are governed by `resource_store` limits. Statistics report current
and peak charged bytes, not RSS, heap fragmentation or physical GPU residency.
No cache-wide throughput or memory-footprint benchmark is claimed.

## Diagnostics and validation

Optional scopes cover request/poll/snapshot and GPU prepare/collect/submit.
Acquisition/decode and GPU upload also retain existing streamer/content/renderer
instrumentation. Failures log warnings; repeated successful frames do not log.
Profiling follows the shared `AUTO`/`ON`/`OFF` build policy.

Portable tests cover ready-job decoding, placeholders, retained budgets and
cancel/destruction lifetimes. Windows WARP tests check actual pixels, fallback,
quota deferral and retirement after cancellation. Game/editor graphics smoke tests
require a real streamed image to be drawn and presented. ASan and static analysis
complement these tests; they do not prove absence of races or fragmentation.
