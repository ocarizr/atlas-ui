# Assets and content pipelines

## Formats

Serialized application data uses XML, including UI, ViewModels, animation/localization source data and asset metadata. Configuration uses YAML. IPC is the explicit Protocol Buffers/gRPC contract, not XML messages. Binary cooked payloads remain appropriate for prepared runtime data and media.

Tool-mandated JSON files such as CMake presets and GitHub Actions outputs are tooling interfaces, not authored application-data formats.

## Asset types

Textures, materials, fonts, localization, animations, videos, and UI blueprints are required. A UI blueprint means a reusable UI asset; it does not imply a visual programming language.

Assets need stable identity, references, dependency validation, and diagnostic errors. Storage layout, ID scheme, migration and compatibility policies remain open.

## Build flow

```text
XML content + source assets
    -> validate / resolve names and references / cook
    -> generated C++ constructors + prepared binary content
    -> compile/link constructors + package assets
    -> runtime construction without interpreting original XML
```

Keep large media as content rather than embedding it into generated source by default. Cooked data should be versioned, cache-friendly and fast to load. Define which runtime reflection is eliminated and which editor metadata remains necessary.

Proposed stages: source validation, dependency/import resolution, constructor/data generation, package assembly, compatibility checks, and standalone loading. Incremental cooking, compression, package granularity and deterministic-output requirements need contracts.

## Streaming

```text
disk -> RAM -> GPU
```

Streaming uses jobs, is asynchronous by default, and supports priority, cancellation, and background loading. Assets without GPU state can finish in RAM. Separate reading, decode/preparation, upload and publication.

Streaming can span frames. Placeholder visuals and preloaded loading screens allow layout/rendering to proceed. Publish only usable resource versions at controlled boundaries; define errors, fallback and any permitted blocking loads.

Ownership, RAM/VRAM budgets, cache/eviction, prefetch, cancellation after upload, reload, resource generations and video-streaming interaction remain open. Track loads, timings, memory and priority/cancellation outcomes.

## Validation

PR validation parses XML, checks mapped asset paths and identities, and resolves declared dependency references. The initial inventory contract is documented in [CI and delivery](12-ci-and-delivery.md); full UI/ViewModel schemas will be added with their implementations.

Test authoring/cooked semantic equivalence, missing references, incompatible versions, cancellation, cleanup, round trips where supported, and packaged standalone loading. The current foundation package contains documentation; runtime installation rules will be added with real targets.
