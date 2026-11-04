# UI renderer adapter

`atlas::ui_renderer` converts immutable UI paint snapshots into the renderer's
ordered passes. It depends on `atlas::ui`, `atlas::renderer` and
`atlas::render_host`; it has no backend-specific source or file access.

Create `paint_renderer` on the graphics coordinator with existing resource/pass
services. Supply an output texture, physical extent and resident texture-key
bindings to `submit`. Consumers own texture lifetime until submission; renderer
fences retain submitted resources afterward. Borrowed services must outlive the
adapter. `frame_payload` carries only immutable CPU data between threads.

See [painting and clipping](../../docs/ui_paint.md) for stacking, PMR, jobs,
adjacent batching, clip precision, diagnostics and validation. Installed consumers
load prerequisite exports before `ui_renderer.cmake`.
