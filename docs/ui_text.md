# Viewport text

`atlas::ui_text` shapes UTF-8 labels, measures them in device-independent
pixels (DIPs), and produces grayscale coverage for the ordered UI paint pass.
Its public contracts use owned, SDK-free values.
Windows uses DirectWrite and installed system fonts. The SDK-free profile can
pack and prepare paint from existing masks and reports native shaping as
unavailable.

## Layout and fonts

Call `shape` with a font family, size, width, locale, DPI and paragraph
direction. DirectWrite shapes combining marks and mixed-direction runs,
selects system fallback faces and wraps at the logical width. An unavailable
requested family is replaced by Segoe UI, with that substitution visible in
the result. The layout reports logical dimensions, line count, distinct
native faces, and the count of unresolved glyphs. Use the measured
dimensions as an intrinsic size when composing a UI element. Shape the
painted label with its arranged width to preserve wrapping and paragraph
alignment. Font fallback and coverage depend on the installed font set.

DPI changes mask resolution and pixel alignment without changing logical
wrapping. Coverage uses grayscale glyphs with white RGB and alpha suitable
for the renderer's straight-alpha pipeline. The current backend uses system
font families and monochrome outlines.

## Painting and lifetime

Pack one or more shaped labels with `glyph_atlas::pack`. Identical masks
share a region across labels. Immutable RGBA8 pages have transparent gutters
around every mask. The pack is bounded by page dimensions, page count,
a 64 MiB pixel budget, and 65,536 positioned glyphs. An oversized mask or
exhausted pack fails before a candidate is returned.

On the graphics coordinator, create an `uploaded_atlas` with a reserved
positive texture-key range. Poll `ready()` and borrow its bindings after
all uploads complete. A host can continue using the old atlas while the
replacement uploads. `glyph_quads` translates and clips a label's packed
placements. Insert them into the desired painter order and call
`ui_renderer::paint_renderer::submit_quads` with the viewport and all
referenced texture bindings. At matching viewport scale, one atlas pixel
maps to one output pixel.

The renderer protects page textures and views through the last submitted
GPU fence. Atlas destruction invalidates handles immediately and leaves
physical reclamation to the resource store. If admission exhausts the
resource or descriptor budget partway through an upload, the candidate
releases all handles it acquired. The previous atlas can stay active.

Retain layouts and pages while text, font, width and DPI remain valid.
Shaping and packing are synchronous CPU operations.
Hosts can prepare independent labels as jobs and publish immutable results
on their existing phase boundary. The graphics coordinator owns upload,
submission and retirement.
