# Application hosts

`atlas_game` and `atlas_editor` are composition roots for the same reusable
libraries. Both currently display a streamed textured quad using a native window
and Direct3D 12. Neither implements game-specific UI, gameplay or a visual editor.

## Lifecycle

Bootstrap parses command-line options and constructs shared memory, diagnostics and
one jobs pool. Main owns native event pumping and the window. The application-owned
render coordinator creates and destroys graphics/content services on its own
thread. Immutable frame data crosses a bounded handoff with explicit identities
and backpressure. Main continues pumping while waiting for capacity or shutdown.

Orderly exit stops production, requests/drains accepted work, joins the render
coordinator, releases the native window and tears down shared services. Render
failure reaches the host rather than allowing continued use of a broken device.
See [render-host ownership](render_host.md).

## Entry points

| Command | Current behavior |
| --- | --- |
| `atlas_game`, `atlas_editor` | Generic streamed textured rendering demonstration |
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
