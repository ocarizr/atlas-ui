# Application hosts

`atlas_game` and `atlas_editor` are composition roots for reusable libraries. The runtime host
exercises clipped viewport UI, streamed thumbnails and shared Direct3D 12 rendering; it contains
no game-specific UI or gameplay. The [editor](editor.md) provides document authoring, assets,
Flow tools and isolated runtime previews using the same UI/rendering technology.

## Lifecycle

Bootstrap parses options and constructs diagnostics, shared memory and one jobs pool. Main owns
native event pumping and serial input/UI production. The render coordinator owns native graphics
and consumes immutable CPU frame packets through a bounded handoff. Storage and image decoding
use the streamer and shared ready jobs. See [render ownership](render_host.md) and
[streamed images](ui_images.md).

Each supported native window attaches the shared [accessibility adapter](accessibility.md).
Main drains assistive requests into active UI input phases, publishes immutable semantic values
and notifies the native adapter. Screen origin is refreshed even when only window position
changes. Minimized windows expose no active semantic nodes. Provider queries cannot read live
UI/Lua state or execute application handlers on a reader thread.

Shutdown stops production, drains/joins rendering, cancels assistive requests, disconnects native
providers before releasing the window, and tears down shared services. Render failures reach
the composition root; the main owner continues pumping messages during joined teardown.

## Entry points

| Command | Current behavior |
| --- | --- |
| `atlas_game` | Generic clipped viewport UI and streamed thumbnail rendering |
| `atlas_editor` | UI document, asset, authored-tool and Flow workspace |
| `--warp` | Software graphics adapter |
| `--graphics-smoke-test` | Hidden WARP presentation check |
| `--smoke-test` | Headless bootstrap check |
| `--help` | Executable-specific command-line help |
| `atlas_cook`, `atlas_package` | Tool entry points; content preparation also has reusable APIs |

The runtime host is a technology exercise. Editor documents and Lua tools remain separate from
project-specific gameplay. Existing gRPC/Protobuf contracts do not require ordinary editor UI
to run in another process; optional preview-process isolation is tracked in
[IPC](https://github.com/ocarizr/atlas-ui/issues/31).
