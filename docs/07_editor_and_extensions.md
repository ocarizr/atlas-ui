# Editor and extensions

## Shared technology and current state

Game/editor currently demonstrate shared threaded rendering and loose-asset content.
There is no implemented UI editor session yet. `atlas_game` is a reusable runtime
host, not a game-specific interface. The editor will use Atlas UI for its own
interface and progressively author that interface with its own tools.

The [UI implementation plan](36_ui_implementation_plan.md) replaces the earlier
mandatory two-process preview and C++-only behavior design. Lua is planned for UI
behavior; native C++ widget/types and operations remain extensible. Separate the
authoring document, editor UI session and preview runtime state in every mode.

## Initial local preview

The initial preview is an independent runtime session inside the editor process.
The editor applies validated document changes through library APIs, rather than
sharing arbitrary mutable tree/ECS ownership with preview execution. Selection,
undo/redo, inspectors and persistence belong to authoring. The standalone runtime
does not link editor implementation details.

One application-owned worker pool serves sessions; each session has explicit event
dispatch/VM ownership and publication boundaries. A viewport is a rendering region,
not another process or implicit scheduler. Session destruction drains work and
releases borrowed resources before their owners disappear.

Bootstrap the editor interface programmatically, then migrate panels to authored
documents. Preserve a minimal recovery interface when an editor document is invalid.
An authored extension needs metadata/registration, validated properties/actions
and a contribution point before the editor can instantiate it.

## Optional isolated preview

The existing gRPC/Protobuf integration remains available for a future process
adapter; current bootstrap linkage is migrated with local host integration.
Isolated execution can preserve editor state when native preview code fails and
support an independently restarted/debugged preview. It adds lifecycle, image
transport, session/versioning and failure handling requirements.

Before enabling it, define handshake/capabilities, document revisions, edits/input,
selection/results, diagnostics, deadlines, cancellation and reconnect behavior.
Do not pass ECS pointers across processes. D3D12 shared resources or frame streaming
are separate image transport choices; gRPC control does not imply copying each
frame into Protobuf.

Native rebuild/restart must preserve the authoring document, report compilation
failure without losing edits, and specify preview-state reconstruction. Changes
linked into the editor may require restarting the editor too. Debugger pauses must
not be mistaken for crashed sessions. These contracts are deferred until isolation
has a concrete consumer.

## Authoring scope

Begin with hierarchy, inspector, selection and undo/redo. Asset/localization
browsers, device simulation, animation timeline, profiler/log panels and package
management follow focused use cases. Animation playback and timeline authoring
are separate capabilities. Reload, native plugins/DLLs, API/ABI evolution and
extension contribution/version policies remain to be specified.
