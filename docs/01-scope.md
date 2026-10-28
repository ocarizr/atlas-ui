# Scope

## Product

Atlas UI is a high-performance, data-driven UI platform in C++20 for Windows Desktop. It includes a runtime, visual editor, asset and packaging pipelines, job system, streaming, logging/profiling, MVVM, serialization, and testing infrastructure.

The runtime presents UI over a black background, solid color, image, or video. It does not currently render a game world. AtlasGame is a host application name, not a commitment to implement gameplay now.

## Required capabilities

- Thousands of widgets and substantial animation workloads.
- Large dynamic lists and grids, drag-and-drop, HUDs, fullscreen menus, popups, notifications, and toasts.
- Textures, materials, custom shaders, video, and localization.
- Retained-mode UI represented through ECS and event-driven MVVM.
- Dependency-driven parallel processing with deterministic frame boundaries.
- Observable performance, cooking, and packaged content.

The long-term direction is a complete frontend stack suitable for production-scale applications and games. Performance goals require measurable budgets rather than an unqualified scale claim.

## Established boundaries

| Area | Contract |
| --- | --- |
| Platform | Windows only; x64 is the initial configured architecture |
| Renderer | Direct3D 12, with no Direct3D 11 or Vulkan backend |
| Editor UI | Atlas UI and the same renderer libraries as the runtime; no separate UI stack |
| ECS | EnTT; project-specific scheduling and UI semantics remain Atlas responsibilities |
| Authored data | XML for UI, ViewModels, and other serialized application data |
| Configuration | YAML |
| IPC | gRPC services with Protocol Buffers |
| Extensions | Native C++ behavior, rebuild/restart permitted |
| Process model | Editor-managed AtlasGame preview process |
| Development | DDD, TDD, modular libraries, English repository content |

## Scope exclusions

Gameplay, physics, world rendering, and a world/entity editor are future possibilities. They are not initial deliverables. Other operating systems and additional rendering backends are outside the supported target.

Audio playback is not required for video. Timeline events may eventually request sound through an external integration; a sound engine is not currently specified.

## Design provenance

The original Atlas UI design describes the broad subsystem scope. The current architecture supersedes its earlier editor-stack, graphics-backend, and serialization-format assumptions. Accepted decisions are documented here; proposals and unresolved contracts are identified explicitly in the remaining documents.
