# Application hosts

`atlas_game` and `atlas_editor` are composition roots for the same reusable
libraries. Both display shared clipped UI panels and textured rows using a native window
and Direct3D 12. Neither implements game-specific UI, gameplay or a visual editor.

## Lifecycle

Bootstrap parses command-line options and constructs shared memory, diagnostics and
one jobs pool. Main owns native event pumping, the window and UI tree/layout/paint preparation. The application-owned
render coordinator creates and destroys graphics/content services on its own
thread. Immutable frame data crosses a bounded handoff with explicit identities
and backpressure. Main continues pumping while waiting for capacity or shutdown.

Main also owns the streamer and UI image cache. Streamer ready jobs decode the
shared demonstration image; frame boundaries poll completion and publish matching
paint/image snapshots. Layout and gray placeholders remain available during
loading. The render coordinator owns bounded GPU residency and records image
last-use fences automatically. See [streamed images](ui_images.md).

Window callbacks enqueue bounded portable primary-pointer and basic key packets.
Main drains and normalizes them into an owner-thread UI input session, then
processes input, tick, late-tick and end-frame before command publication. Shared
thumbnail handlers coalesce hover/press/focus feedback into one opacity demand
per late tick. Appearance-only changes retain layout geometry. Input frames advance
independently of render-queue admission; unchanged paint/image packets remain
reusable. See [input and lifecycle](ui_input.md) for reset/overflow and scope limits.

Orderly exit stops production, requests/drains accepted work, joins the render
coordinator, releases the native window and tears down shared services. Render
failure reaches the host rather than allowing continued use of a broken device.
See [render-host ownership](render_host.md).

## Entry points

| Command | Current behavior |
| --- | --- |
| `atlas_game`, `atlas_editor` | Generic clipped UI panels and streamed textured rows |
| `--warp` | Software graphics adapter |
| `--graphics-smoke-test` | Hidden WARP presentation check requiring textured frames |
| `--smoke-test` | Headless bootstrap check |
| `--help` | Command-line help |
| `atlas_cook`, `atlas_package` | Placeholder tool entry points |

There is no authoring document model, inspector, editable hierarchy, runtime
preview session, native extension contribution pipeline or Lua VM. Existing
gRPC/Protobuf contracts provide bootstrap infrastructure, not preview transport.
Editor work is tracked in [Editor](https://github.com/ocarizr/atlas-ui/issues/28);
optional process isolation is tracked separately in
[IPC](https://github.com/ocarizr/atlas-ui/issues/31).
