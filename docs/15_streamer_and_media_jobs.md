# Streamer and media jobs

## Storage boundary and priority

The Job System and streamer precede production UI/media. Streaming infrastructure is the sole application-managed filesystem boundary: content, configuration, catalogs, codecs and rendering/audio consumers do not independently open files. Composition roots register sources and asset IDs. Build tools and isolated test-fixture setup are outside this running-application contract. Future persistence/log/cooking writes need explicit storage interfaces under the same boundary; this first API exposes reads only.

Consumers request an asset ID, offset and exact byte count. They receive owned immutable bytes or an error, not a physical path. Memory and loose files are the initial backends. Future bundles must preserve the consumer contract and translate asset-relative ranges inside the streamer.

## Implemented foundation

`atlas::streaming` provides `asset_streamer`, an immutable `asset_catalog` and configurable `streamer_limits`:

- `memory_asset` pins shared immutable bytes; callers must not mutate them through another alias. `loose_file_asset` registers an authorized local path that media consumers never receive.
- `read(request, completion)` is thread-safe. The returned future observes callback completion/errors; only coordinators may wait. Worker consumers chain ready continuations.
- Memory copies and accepted read completions use the current direct job API with explicit payload ownership. Small/single-worker/busy-pool copies use one root; substantial memory copies may use disjoint attached children with a bounded grain policy. One persistent dedicated I/O thread acquires file bytes before dispatching ready CPU work. No thread is created per request and CPU workers never wait on disk.
- Default limits are 4 MiB per read, 32 pending operations and 64 MiB of requested output bytes, including retained consumer results. Oversized requests and exhausted admission budgets fail before callback/I/O dispatch. Split larger assets into ranges.
- Exact ranges must fit the source; empty reads at EOF are valid. Unknown IDs, null memory backing, missing files and invalid ranges return an exception through `read_result`. Partial bytes are never published.
- Callback exceptions reach the future. If the scheduler has stopped, the future reports dispatch failure and no callback runs on the wrong thread.
- Creating-coordinator `shutdown()` rejects new requests, drains accepted reads/callbacks, joins I/O and is idempotent. Destroy on that same coordinator and keep the Job System alive until draining finishes. Completion jobs must not shut down their streamer or wait for another read.

Retained output buffers belong to consumers and remain charged until their final owner releases them. A shared PMR resource can replace default payload allocation without changing storage logic; outputs retain allocator ownership beyond streamer destruction. The quota excludes metadata, source bytes, alignment overhead, allocator cache retention and OS caches. It is not a process-wide memory bound or a guarantee against fragmentation. Shutdown does not aggregate callback exceptions: observe futures or explicitly route errors through consumer diagnostics.

CMake selects one platform implementation behind a fixed private file interface.
`ATLAS_TARGET_PLATFORM=windows` is supported; other enumerated targets are reserved
and rejected until implemented. Native Windows file reads live under the Windows
boundary; the SDK-free validation profile selects a standard-library reference
backend. Windows now uses bounded overlapped reads through IOCP: four concurrent
file slots by default, configurable with `max_io_requests`. Queued cancellation
avoids file admission; active cancellation requests `CancelIoEx` and retains the
buffer/OVERLAPPED until OS acknowledgment, including completion races. Shutdown
drains; callers request stop explicitly when aborting. The reference backend remains
serial and cannot interrupt a blocked read. Native open/metadata/allocation still
run on the I/O owner, not CPU workers. Per-read CPU priority applies to ready work.
Storage priorities, generations, prefetch and cache eviction remain later milestones. See the
[streaming API and memory contracts](../libs/streaming/README.md).

## Ready-work pipeline

```text
asset ID + range
    -> streamer memory / loose file / future bundle
    -> ready bytes -> demux job -> ready packets
    -> ordered decode jobs per stream
    -> AV1 frames -> conversion/upload jobs -> controlled publication
    -> Opus PCM -> ready audio queue -> XAudio2 submission
```

Each stage becomes runnable when its inputs exist. CPU jobs must not poll storage or block on futures/dependencies. Scheduler DAGs/continuations will express these edges; the base already dispatches callbacks as jobs and tests Opus decoding from streamed bytes on a worker. Independent assets/streams may run concurrently. Preserve exclusive ordered access to each stateful decoder. Measure batching/grain size instead of creating one tiny job per sample.

## Media adapters

**libwebm:** provide a custom memory/range-backed `IMkvReader` over streamer-prefetched bytes; never use file-opening `MkvReader`. On a cache miss, request data and resume/retry from a valid parse state after completion, without waiting inside a CPU job. Validate the selected incremental parser API; a bounded small clip may initially be prefetched before parsing.

**dav1d:** feed AV1 packets from the demux stage. Pin input bytes until the decoder releases them. Serialize each context and start with a decoder thread budget of one; profile internal parallelism before enabling additional pools. Independent contexts may run concurrently. Bound conversion/upload queues and check resource generations.

**libopus:** feed demuxed packets in order to a separate decoder per stream. The [Opus contract](https://opus-codec.org/docs/opus_api-1.6/group__opus__decoder.html) requires exclusive state access. Decode PCM in CPU jobs; dependent packets from one stream are not independent jobs. WebM is the initial container for AV1/Opus and audio-only Opus. WAV may be an offline input; runtime compressed audio is Opus.

**XAudio2:** jobs prepare PCM and enqueue commands for an audio owner. XAudio2 retains its own processing thread/mixer. Pin PCM until consumption notification, not merely until the preparing job finishes. [Real-time callbacks](https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-callbacks) only signal bounded work/consumption: no storage, decoding, waits, scheduler locks or allocating/locking job submission. A control-side stage consumes notifications and schedules work. Voice destruction and device lifecycle stay with the audio owner outside frame-critical jobs.

Combined playback additionally needs a common clock, timestamps, buffering/underrun policy, Opus pre-skip/gain/padding and seek/preroll rules. Background streaming/decode may span frames. Ready visual generations publish at controlled frame boundaries; audio follows its device timeline.

## Implementation sequence and acceptance

1. **Current base:** memory/loose ranges, selected native/reference backends, persistent I/O, current direct jobs and child-copy grain policy, CPU priorities, cooperative cancellation, retained-byte budgets, shared PMR payload allocation, counters, errors and draining. Tests cover bounds, missing/unknown assets, callback/allocation failures, concurrent producers, reentrant requests, allocator/output lifetime and cleanup. The streamed Opus decode test remains.
2. **Storage concurrency implemented; performance gate remains:** bounded Windows IOCP, specific-operation `CancelIoEx`, safe completion/drain ownership, active/peak/cancellation counters and controlled latency/fault fixtures. Tests prove pending I/O leaves CPU workers available and queued cancellation does not need a native slot. Handles are bounded per active request and closed after completion; idle reuse remains deferred until loose-file identity/invalidation is defined. Storage priority, generations and explicit retry policy remain open. Measure queued/read/decode latency, bytes, failures and allocations on representative assets before claiming speedups; preserve SDK-free tests. Bundle implementation is deferred until its format contract is defined; continue with loose assets in the meantime.
3. **Bundles (blocked by format design):** first define and review the bundle format; only then implement a versioned cooked index, IDs, entry offsets/lengths and integrity/compression metadata. Translate ranges with overflow-safe bounds and decoded-size budgets. Compare loose, memory and bundle results through the same API/tests. No bundle backend is implemented yet.
4. **Media adapters:** streamer-backed WebM parsing/seek, real AV1/Opus fixtures, corruption, stale completions, packet ordering, decoder errors and buffer lifetimes. Containers/codecs never open files.
5. **Playback/performance:** device-loss/underrun recovery, seek cancellation, consumption-driven PCM reclamation, startup/loop/scrub and measured synchronization. Compare sequential and jobified processing on reference assets/hardware before claiming speedups.

Production UI may use the validated memory/loose streamer before bundles or full playback exist, after the scheduler's [UI-readiness gate](14_job_system_implementation_plan.md). The exclusive storage boundary and job-based consumers apply from the first resource-loading UI.

## Bundle dependency and interim scope

The bundle format is not defined. The items above describe future requirements,
not an agreed on-disk specification. Bundle reading, bundle-aware cooking and bundle
assembly depend on that format contract and remain deferred. The current executable
packaging pipeline does not establish an asset-bundle format.

Continue developing and validating with registered loose assets. Memory sources
remain useful for tests and preloaded bytes. Consumers keep using logical asset IDs,
exact ranges and immutable buffers through the streamer; they must not introduce
filesystem access or assume a provisional bundle layout.

Before implementing bundles, record and review these decisions:

- Container header, format identification, versioning, byte order and compatibility rules.
- Stable asset identity, index layout, entry offsets/lengths, alignment and overflow-safe bounds.
- Integrity checks, corrupt/truncated input behavior and optional compression/chunking policy,
  including random-range access and decoded-memory budgets.
- Deterministic writer/cooker output, reader/writer ownership and validation fixtures.

After that dependency is resolved, implement the writer/index and streamer adapter
against the same contract. Require loose/memory/bundle result equivalence, malformed
index and range rejection, cancellation, memory bounds and cleanup tests before
accepting bundle loading. The consumer-facing read API should remain independent
of physical storage layout.

Work that does not require bundles can continue on loose assets: representative
queued/read/decode latency and allocation measurements, storage priority, resource
generations, explicit retry/backpressure policy and timing/telemetry adapters.
Handle caching additionally depends on a loose-file identity/invalidation policy.
These follow-ups can be planned independently while bundle implementation remains deferred.
