# Scope and supported capabilities

Atlas UI is reusable C++20 infrastructure for UI runtime and authoring tools.
Windows Desktop x64 is the only supported and validated target. Public library
contracts are platform-neutral; the native graphics backend is Direct3D 12.
This repository contains the technology and its host applications, not a game.

## Available capabilities

- One application-owned persistent CPU job pool with direct dispatch, dependency
  handles, attached children, joined ranges, sealed graphs, cancellation and
  origin-affine completion callbacks.
- Bounded exact-range reads from memory and loose assets. Windows storage uses
  IOCP and operation-specific cancellation; completions run as ready CPU jobs.
- Explicit shared PMR allocation through the memory library, with mimalloc and
  system backends. This does not replace every allocation in the process.
- Injected logging, numeric metrics and bounded Chrome trace capture.
- Native windows, generic graphics passes/resources, optional parallel recording,
  streamed image/shader preparation and bounded main/render handoff.
- SDK-free EnTT-backed elements, transactional requested properties, selective
  invalidation, immutable snapshots/batches and joined phases on injected jobs.
- Generated Protobuf/gRPC session contracts and executable bootstrap entry points.

The game and editor executables display the same streamed textured quad. They do
not provide a widget tree, layout engine, editor interface, MVVM bindings, Lua
execution or media playback. The cooking and packaging tool entry points are
placeholders; executable CPack packaging is separate from asset cooking/bundles.
EnTT, XML, YAML and codec dependencies are integrated, but linking a dependency
does not establish an implemented domain system.

## Boundaries

Applications compose libraries and own boot, event loops and shutdown. Shared
libraries do not depend on editor or game implementation details. The streamer
is the sole application-managed storage read boundary; consumers receive bytes.
Gameplay, physics and a world/entity editor are outside the implementation scope.
No additional OS, graphics backend or production-scale performance guarantee is
advertised by the current Windows validation.

Outstanding delivery scope and acceptance criteria live in
[repository milestones](https://github.com/ocarizr/atlas-ui/milestones).
