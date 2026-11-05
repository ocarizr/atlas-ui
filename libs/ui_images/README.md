# Streamed UI images

`atlas::ui_images` connects immutable UI paint packets to streamer-fed image
decoding and bounded GPU residency. It depends on `atlas::ui_renderer` and
`atlas::renderer_content`; it introduces no native SDK types or private worker pool.

The application owns an `image_cache` on its main coordinator, injects the existing
streamer, buffer-only decoder, thread-safe shared PMR resource and diagnostics,
and calls `request` once per shared image identity. At frame boundaries it calls
`poll`, then `snapshot`. Pending, failed and cancelled entries remain explicit.
`intrinsic_size` provides decoded dimensions or a caller-selected placeholder.

The graphics coordinator owns an `image_renderer` backed by existing resources
and passes. Once its tiny gray fallback is ready, `submit` accepts a
`frame_payload`, output target and physical extent. It admits bounded uploads,
draws placeholders until upload completion and records last-use fences itself.
Borrowed graphics services outlive the adapter. Destroy it on its owner thread.

See [image ownership and budgets](../../docs/ui_images.md) for acquisition,
fitting, cancellation, retirement, memory accounting and integration limitations.
Installed consumers load prerequisite exports before `ui_images.cmake`.
