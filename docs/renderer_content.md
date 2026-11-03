# Renderer content integration

The library provides `atlas::renderer_content`, a consumer of renderer and streaming.
The graphics core never opens image or shader files. Application composition
registers logical assets with the streamer, injects one shared job pool and
allocator, and supplies optional diagnostics endpoints to each service.

## Request, decode and upload

1. `content_loader::read_image` admits an exact logical range through the streamer.
2. Streamer I/O completes before it dispatches ready CPU delivery. That existing
   job calls the injected reentrant `image_decoder`; no extra job waits for I/O.
3. A move-only `image_request` exposes nonblocking readiness, cooperative
   cancellation and coordinator-only `get()`. A ready future can contain an error. `valid()` distinguishes consumable requests
   from moved/consumed handles; invalid handles are non-ready and moved-from
   cancellation is harmless.
4. The coordinator constructs `texture_upload` from decoded pixels. It snapshots
   rows into the renderer's fence-retained staging and creates a sampled view.
5. `ready()` polls the GPU fence before exposing the binding and releasing decoded
   pixels. Job completion and GPU completion remain separate events.

```cpp
atlas::renderer_content::content_loader content{
    streamer, atlas::renderer_content::make_image_decoder(), memory, destinations};
auto request = content.read_image({"menu.background", 0, encoded_byte_count});
// Continue rendering a placeholder while !request.ready().
auto pixels = request.get(); // Coordinator only; use after ready() in a frame loop.
atlas::renderer_content::texture_upload upload{resources, pixels, destinations};
// Continue rendering the placeholder while !upload.ready().
auto texture = upload.binding();
// Pass texture.view in a generic draw_packet; no UI semantics enter the renderer.
```

Independent images decode on available application workers. One image remains one
codec invocation; adding jobs around a short decode or partitioning an opaque WIC
operation would add overhead without a valid independent workload. Large assets
and media have their own future chunk/frame policies. CPU workers never submit GPU
work, wait on graphics fences or read files directly.

## Ownership, cancellation and budgets

Requests retain callback state, decoder, diagnostics and allocator independently
of the loader facade. Destroying a request does not block or implicitly cancel
accepted work. The application keeps the streamer and job pool alive until their
accepted work drains. Stop external producers before streamer shutdown.

`cancel()` requests native storage cancellation and checks cooperative decode
boundaries. Cancellation after completion but before `get()` prevents consumption.
A codec call already running cannot be preempted; cancellation is checked before
and after conversion. Acquisition, decoder and scheduler failures propagate through
the completion future. No null image is published as a successful fallback.

Decoded pixels use the supplied thread-safe PMR resource and retain its owner.
Default limits are 16384 pixels per dimension and 64 MiB of RGBA8 output per image;
both are checked before native output allocation. The streamer separately enforces
encoded read/residency/admission limits. These are per-image limits, not an aggregate
decoded-cache budget: consumers must bound retained requests/images. No automatic
image cache or eviction policy is introduced by this library.
The upload helper also enforces the default image envelope; decode limits can be
tightened per request. Renderer staging/allocation quotas are separate and may
reject an otherwise valid image.

`texture_upload` and `pipeline_cache` belong to the graphics coordinator and must
be destroyed before their borrowed resource store/pass renderer. Uploaded bindings
are borrowed until their owner is cancelled/destroyed. Draw submission extends
native last-use fences. Cancellation invalidates public handles immediately while
staging, textures and descriptors remain fence-protected. Decoded pixels can be
released on cancellation because staging already owns its independent snapshot.

## Platform image adapter

`ATLAS_TARGET_PLATFORM=windows` with native SDK integration selects WIC under
`libs/platform/windows/renderer_content/`. Public contracts expose no COM or SDK
types. SDK-free builds report native decoder unavailability; portable tests inject
a buffer decoder instead of pretending to exercise native graphics.

The adapter accepts static PNG, BMP and JPEG, normalizes one frame to tight
top-down straight-alpha RGBA8 and balances COM initialization per call. WIC's
mutable-memory-stream API uses a private encoded copy; the caller's immutable
input is not modified. EXIF orientation, ICC conversion, sRGB texture interpretation,
mip generation, compressed GPU formats and animated image playback are not provided.
This is a byte-preserving unorm rendering foundation, not a complete image pipeline.

See Microsoft's [memory-stream decoder contract](https://learn.microsoft.com/en-us/windows/win32/api/wincodec/nf-wincodec-iwicimagingfactory-createdecoderfromstream)
and [WIC API overview](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-api).

## Shader construction and cache policy

`read_shader` uses the same streamer/ready-job path, limits trusted source to 1 MiB,
rejects empty or embedded-NUL content and removes an optional UTF-8 BOM. The source
is passed to `pipeline_description`; the renderer compiles through its fixed ABI.
File includes remain disabled, preserving the streamer storage boundary.

`pipeline_cache` retains successful PSOs for exact source, entry points, program,
blend/depth state and sampler mode. It compares complete keys rather than relying
on a source hash. Compilation remains coordinator-owned and runs once per distinct
cached description, not per frame. Failed compilation creates no cache entry.
Capacity is explicit (default 32), and exhaustion fails instead of silently evicting
handles retained by callers. Cached handles are borrowed: callers must not release
them independently through `pass_renderer`. `clear()` invalidates handles and retires native state
after its last use. The cache is process/device-local; persistent bytecode formats,
offline compilation and bundle packaging are not implemented.

## Demonstration and diagnostics

Game and editor share the streamed textured-quad scene. A tiny original BMP and
HLSL source are mapped in `assets/manifest.xml`, staged beside each executable in
`content/` and included in application packaging. CMake derives exact request sizes
from those source files. The scene renders its background while content is pending;
smoke checks require a textured frame to reach presentation before reporting success.
There are still no widgets, layout, bundles or world-rendering semantics.

Injected diagnostics provide `content.image.request`, `content.image.decode`,
`content.shader.request`, `content.shader.prepare`, `content.texture.upload`,
`content.texture.retire`, `content.pipeline.get` and `content.pipeline.clear` CPU
scopes. Lifecycle/failure logs accompany the appropriate service boundaries;
there are no per-draw success logs. Profiling follows `ATLAS_PROFILING=auto|on|off`
and needs an injected capture destination. Neither GPU timing nor race freedom is
inferred from CPU traces or ASan.

[Render-host lifecycle](render_host.md) defines application main/render handoff,
device failure and measurement boundaries. Consumer budgets and format extensions
are tracked in [Renderer](https://github.com/ocarizr/atlas-ui/issues/26).
