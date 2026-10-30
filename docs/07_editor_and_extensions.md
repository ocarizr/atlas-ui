# Editor and extensions

## Shared technology

Current entry points perform headless bootstrap only. The managed preview lifecycle and RPC services below are planned contracts, not an implemented editor session.

atlas_editor uses Atlas UI and the Direct3D 12 renderer libraries for its own interface. atlas_game uses those same libraries to generate the preview in a separate process. New compositions and native C++ behavior/types can extend the editor.

The job-system maturity gate precedes production UI implementation. A standalone runtime with manually authored UI follows that gate; hierarchy, inspector and persistence are added afterward so the editor can progressively author its own interface.

## Preview lifecycle

The user starts atlas_editor. The editor manages the atlas_game preview session:

1. Detect relevant C++ changes using a defined build/debounce policy.
2. Preserve the authoring document and required session data.
3. Stop and await the preview process.
4. Run validation/generation/build.
5. On success, start the rebuilt preview.
6. Reconstruct the session and resume preview display.

Keep the document intact on compilation failure and show diagnostics. Changes to code linked into the editor itself may require restarting atlas_editor too; a preview restart is not universal native-code hot reload.

## gRPC and Protocol Buffers

Use gRPC services and Protobuf messages for control/state communication. Proposed contracts include session handshake/version, document revisions, edits/commands, input forwarding, selection/query results, diagnostics, and lifecycle/health messages.

Do not share ECS pointers across processes. Reject stale revisions/sessions and define cancellation, deadlines, disconnect/reconnect and recovery. Use a local endpoint with an explicit session identity; transport/security details remain to be designed.

Image delivery is a separate open contract: D3D12 shared resources, frame streaming, or another mechanism. Choosing gRPC control does not require serializing every GPU frame into messages.

The editor owns authoring state; specify whether animation/simulation state is restored or reset after rebuild. Protocol evolution, capability negotiation and compatible generated-code versions also need contracts.

## Tools

Hierarchy, inspector, animation timeline, asset browser, localization browser, profiler, log window and package manager. Visual authoring includes drag-and-drop, anchors, docking, preview modes and device simulation. Animation authoring includes curves, tracks, event keys, preview and scrubbing.

Package Manager scope, undo/redo, autosave/recovery, multi-document editing, device simulation and live-edit behavior remain to be specified.

## Extension pipeline

Create a composition/type; declare properties, actions and bindings; validate types/references; cook or build native behavior; register a contribution; instantiate it in the editor.

Composition from existing widgets and creation of a new native type are distinct operations. Native types may introduce components, metadata and rendering behavior. Static registration versus plugins/DLLs, extension points for menus/panels/inspectors, API/ABI versioning and persistence remain open. Rebuild/restart is acceptable.

## Debugging

Use Visual Studio or another suitable Windows IDE and attach to atlas_game for breakpoints. Expose the process/session identity. A proposed debug mode suppresses watchdog/restart while a debugger has paused execution; a breakpoint is not a crash.

Test preview lifecycle and state synchronization, rebuild failure, disconnect recovery, and extension integration without corrupting the authoring document.
