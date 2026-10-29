# Assets and content pipelines

## Formats

Serialized application data uses XML, including UI, ViewModels, animation/localization source data and asset metadata. Configuration uses YAML. IPC is the explicit Protocol Buffers/gRPC contract, not XML messages. Binary cooked payloads remain appropriate for prepared runtime data and media.

Tool-mandated JSON files such as CMake presets and GitHub Actions outputs are tooling interfaces, not authored application-data formats.

## Asset types

Textures, materials, fonts, localization, animations, AV1/WebM videos, Opus audio and UI blueprints are required. Opus packets use WebM demuxing; decoded PCM is an in-memory playback representation, not the selected compressed asset format. A UI blueprint means a reusable UI asset; it does not imply a visual programming language.

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
memory / loose files / future bundles -> streamer -> demux/decode jobs -> CPU/GPU resources
```

Streaming infrastructure is the only application-managed filesystem boundary. Codecs and domains receive IDs, byte ranges and owned buffers; they must not open paths. This applies to libwebm, dav1d, libopus and audio/rendering consumers. Future file writes need explicit storage interfaces under the same boundary. Build tooling and isolated test-fixture setup are outside the running application's boundary.

The current `Atlas::Streaming` foundation supports bounded exact-range reads from immutable memory or registered loose files. A persistent dedicated I/O thread performs file reads, keeping blocking disk operations off CPU workers. Completion, in-memory work and downstream decode run as jobs. Priority, cancellation, caches, asynchronous Windows I/O and bundle backends remain planned. See the [streamer contract and implementation plan](15-streamer-and-media-jobs.md).

Assets without GPU state can finish in RAM. Separate reading, demux/decode/preparation, upload/audio handoff and publication. CPU workers must not wait on futures for pending reads; schedule consumers only after their inputs are ready.

Streaming can span frames. Placeholder visuals and preloaded loading screens allow layout/rendering to proceed. Publish only usable resource versions at controlled boundaries; define errors, fallback and any permitted blocking loads.

Ownership, RAM/VRAM budgets, cache/eviction, prefetch, cancellation after upload, reload, resource generations and video-streaming interaction remain open. Track loads, timings, memory and priority/cancellation outcomes.

## Validation

PR validation parses XML, checks mapped asset paths and identities, and resolves declared dependency references. The initial inventory contract is documented in [CI and delivery](12-ci-and-delivery.md); full UI/ViewModel schemas will be added with their implementations.

Test authoring/cooked semantic equivalence, missing references, incompatible versions, cancellation, cleanup, round trips where supported, and packaged standalone loading. The current bootstrap package contains four entry-point executables, their non-system runtime dependencies, documentation and notices. Content cooking and a rendered UI are not implemented yet.
