# Scope and supported capabilities

Atlas UI is reusable C++20 technology for viewport UI runtime and authoring tools.
Windows Desktop x64 is the only supported and validated target. Public library
contracts are platform-neutral; the native graphics backend is Direct3D 12.
This repository contains the technology and its host applications, not a game.

## Product boundary

The delivery goal is a complete viewport UI workflow: create an external project,
author its interface in an editor built with Atlas UI, add data bindings and Lua
behavior, preview it, then validate and package a standalone UI application.
The editor and player share reusable UI/rendering libraries. Consumer-specific
documents, assets, scripts and extensions belong to the consumer project.

The committed scope includes 2D layout, painting, input, text, images, materials,
animation, audio/video, XML authoring, asset management and cooking/packaging.
Several of these capabilities remain unimplemented; issues own their acceptance
criteria and dependencies.

World-space UI, in-world TV/billboard presentation, world/scene/entity authoring,
scene cameras, gameplay and layout-controlled 3D characters/scenes are outside
this scope. An imported mesh UI element is an uncommitted design candidate;
neither FBX import nor a 3D presentation pipeline is required for product delivery.
Existing generic renderer/resource APIs remain reusable without expanding that
delivery boundary.

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
- Constrained measure/arrange for size boxes, overlays and horizontal/vertical
  containers, with revision-checked geometry publication and root reuse.
- Generated Protobuf/gRPC session contracts and executable bootstrap entry points.
- Owner-thread input routing, clip-aware border hit testing, pointer capture,
  focus/reveal requests and ordered start/enable/input/tick/late/end lifecycle.

The game and editor executables display the same generic clipped UI panels and
textured rows using the shared tree, layout, paint and renderer adapter. They do
not provide an editor interface, Lua execution or media playback. Independent scalar
ViewModels, XML round trips, native factories and UI property/action adapters are
available; see [MVVM](mvvm.md). The cooking and packaging tool entry points are
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

Asset management shares a persistent identity contract across authoring and
packaged content. The editor's loose-source catalog and the runtime bundle
manifest resolve the same asset ID; temporary image/GPU handles have separate
lifetimes. The database, stable-ID mapping, bundle format and transforms are
planned rather than implemented. See [assets and storage](assets.md).

Outstanding delivery scope and acceptance criteria live in
[repository milestones](https://github.com/ocarizr/atlas-ui/milestones).
