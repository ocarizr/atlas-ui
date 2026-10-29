# UI and rendering

## Retained-mode UI

Widgets persist across frames, and changes invalidate the work needed to update them. Runtime UI systems use ECS. The editor and preview share UI and renderer libraries, with independent state in separate processes.

Containers: Canvas, Overlay, Grid, Horizontal Box, Vertical Box, Uniform Grid, Scroll Box, Dock Panel, and Stack Panel.

Primitives: Image, Text, Border, Shape, Video, Button, Progress Bar, Slider, and Checkbox.

Large lists/grids, drag-and-drop, fullscreen menus, HUDs, popups, notifications, and toasts are intended capabilities. Virtualization and item recycling are implementation proposals to evaluate against actual workloads.

## Layout and invalidation

```text
Measure -> synchronization -> Arrange -> synchronization
        -> Geometry -> synchronization -> RenderData
```

Independent subtrees may execute concurrently. Parent/child sizing, text metrics, and space distribution still create dependencies. More entities do not automatically mean more independent work.

| Change | Required invalidation |
| --- | --- |
| Color | RenderDirty |
| Text | LayoutDirty and RenderDirty |
| Hierarchy | StructureDirty, LayoutDirty and RenderDirty |

Process dirty regions without an unnecessary whole-tree traversal. Propagation, caching, and stable region snapshots need explicit contracts. A separate geometry-dirty category is a possible refinement, not an established requirement.

Logical units, DPI, anchors, sizing, margins, padding, clipping, scrolling, hit testing, focus, keyboard navigation, and pointer capture remain to be specified.

## Direct3D 12

Direct3D 12 is the sole graphics backend. Support textures, materials, custom shaders, and image/video backgrounds. Widgets should produce renderer-facing data rather than expose native D3D objects in their public UI contracts.

Command-list recording, allocator ownership, descriptors, resource states, fences, uploads, batching, transparency, clipping and draw ordering require a concrete design. Parallel recording must respect D3D12 ownership and synchronization rules.

The editor draws its own interface; the AtlasGame preview process produces the preview image. gRPC carries control and state messages. The image/frame transport remains open: gRPC control does not imply copying every frame through Protobuf.

## Animation

Animate transforms, opacity, color, visibility, material parameters, video controls, and custom properties. Timelines provide keyframes, curves, tracks, and synchronized event tracks, addressed by time or frame.

Clocks, interpolation, blending, conflicting property writers, seek/loop/scrub event behavior, and binding precedence remain open. Events may show tooltips, enable buttons or enqueue UI sound-effect commands. Audio device work must not block the frame's CPU jobs.

## Video and localization

AV1/WebM video uses the public dav1d decoder and libwebm demuxer. Looping, seeking, scrubbing, bounded frame queues, keyframe policy and timeline events remain required. Decoder CPU/thread budgets and color/upload behavior require implementation and benchmarks; integration alone does not prove playback performance. See [media contracts](13-dependencies-and-media.md).

UI sounds use Opus/libopus decoded to PCM for XAudio2 2.9: button, hover/focus and selection feedback plus event-track commands. Prefetch through the streamer, decode through jobs and use bounded reusable voices with explicit gain/device handling. Codecs never open files. Per-stream decoding is ordered; independent streams may run concurrently. Synchronization and real-time callback restrictions follow the [streamer/media contract](15-streamer-and-media-jobs.md).

Localization uses string tables, language switching, fallback languages and plural forms. Reload is desirable. Fonts are assets; Unicode, shaping, fallback fonts, RTL support, and rasterization are not yet specified. Changes to text metrics or language must invalidate affected layout regions.
