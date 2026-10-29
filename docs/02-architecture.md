# Architecture

## Domain boundaries

Use Domain-Driven Design to define vocabulary, invariants, and boundaries. Proposed product contexts are runtime/presentation, authoring, and content/build. Jobs, ECS integration, and observability are supporting capabilities, not automatically separate bounded contexts.

ECS and data-oriented structures are implementation tools inside those contexts. DDD does not require an object-heavy model in performance-sensitive storage. Apply SOLID, DRY, KISS, and YAGNI; justify abstractions, dynamic dispatch, and allocations with concrete needs.

## Libraries and composition roots

```text
Engine/<Library>/include/atlas/<context>/
Engine/<Library>/src/
Apps/AtlasGame/
Apps/AtlasEditor/
Tools/<Tool>/
Protocols/
Assets/
Tests/<Context>/<Scenario>/
Tests/<Context>/Data/
Tests/Support/
ThirdParty/
cmake/
scripts/
```

Each shared library has a CMake target, a narrow public API, and declared dependencies. AtlasGame and AtlasEditor compose libraries. Libraries do not depend on application implementation details. Static versus dynamic linkage remains an implementation decision; modularity does not require one DLL per module.

The editor owns the authoring document. Preview state is a synchronized projection in another process. Sharing libraries means sharing code and contracts, not directly sharing ECS pointers between processes.

## Subsystems

| Library area | Responsibility |
| --- | --- |
| Foundation / Core | Common types, containers, hashing, delegates/events, lifecycle, allocation, memory tracking, metadata |
| Config | YAML startup configuration; reload where supported |
| Logging / Profiling | Structured diagnostics, frame/job timelines, memory and streaming metrics |
| JobSystem | Persistent workers, dependencies, child jobs, ParallelFor, DAGs, synchronization |
| ECS | EnTT integration and Atlas access/structural-mutation policies |
| UI | Widget hierarchy, layout, invalidation, interaction, render-data preparation |
| MVVM | ViewModel state, named authored bindings, notifications, actions, prepared wiring |
| AssetSystem / Streaming | Identity, lifetime, dependency loading, asynchronous CPU/GPU preparation |
| Serialization | XML authoring, validation, cooked data and generated constructors |
| Animation | Keyframes, curves, tracks, property animation and events |
| Video | Public AV1/WebM decoding with dav1d/libwebm and timeline-controlled playback |
| Audio | XAudio2 UI sound effects, short-clip assets and bounded voices |
| Localization | String tables, language switching, fallback and plurals |
| Renderer | Direct3D 12 resources, materials, shaders and UI submission |
| Application | Windows host composition and execution lifecycle |
| IPC integration | Generated Protobuf/gRPC contracts and process-session adapters |

The physical split between Foundation and Core should follow actual responsibilities. Do not create a library per class or reimplement the standard library without measured benefit.

## ECS and concurrency

Widgets are entities with components such as Transform, Layout, Render, Animation, Text, Video, Binding, Visibility, and Interaction. XML interpretation and generated/cooked construction must produce semantically equivalent ECS state.

EnTT provides storage/query infrastructure. Atlas defines hierarchy semantics, access ownership, dependency scheduling, and safe points for structural changes. A registry is not permission to mutate arbitrary components concurrently.

## Memory and build

Support custom allocators and memory tracking. Frame, job, asset, and persistent lifetimes need explicit ownership. Metadata for inspection/bindings must be reconciled with prepared construction and the cooked-runtime reflection goals.

Use C++20 and CMake, out-of-source builds, multi-config generators, Visual Studio 2022 and Ninja. Modules are optional; concepts, constexpr, and noexcept should serve concrete contracts. External dependencies are pinned through the vcpkg baseline in `vcpkg.json`; native graphics, text, image and audio APIs come from the Windows SDK. See [dependencies and media](13-dependencies-and-media.md).
