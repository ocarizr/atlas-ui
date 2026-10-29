# Streamer and media jobs

## Storage boundary and priority

The Job System and streamer precede production UI/media. Streaming infrastructure is the sole application-managed filesystem boundary: content, configuration, catalogs, codecs and rendering/audio consumers do not independently open files. Composition roots register sources and asset IDs. Build tools and isolated test-fixture setup are outside this running-application contract. Future persistence/log/cooking writes need explicit storage interfaces under the same boundary; this first API exposes reads only.

Consumers request an asset ID, offset and exact byte count. They receive owned immutable bytes or an error, not a physical path. Memory and loose files are the initial backends. Future bundles must preserve the consumer contract and translate asset-relative ranges inside the streamer.

## Implemented foundation

`Atlas::Streaming` provides `AssetStreamer`, an immutable `AssetCatalog` and configurable `StreamerLimits`:

- `MemoryAsset` pins shared immutable bytes; callers must not mutate them through another alias. `LooseFileAsset` registers an authorized local path that media consumers never receive.
- `read(request, completion)` is thread-safe. The returned future observes callback completion/errors; only coordinators may wait. Worker consumers chain ready continuations.
- Memory copies and accepted read completions execute as CPU jobs. One persistent dedicated I/O thread acquires file bytes before dispatching the completion job. No thread is created per request and CPU workers never wait on disk.
- Default limits are 4 MiB per read and 32 pending operations, including callbacks in flight. Oversized requests and exhausted admission budgets fail before callback/I/O dispatch. Split larger assets into ranges.
- Exact ranges must fit the source; empty reads at EOF are valid. Unknown IDs, null memory backing, missing files and invalid ranges return an exception through `ReadResult`. Partial bytes are never published.
- Callback exceptions reach the future. If the scheduler has stopped, the future reports dispatch failure and no callback runs on the wrong thread.
- Creating-coordinator `shutdown()` rejects new requests, drains accepted reads/callbacks, joins I/O and is idempotent. Destroy on that same coordinator and keep the Job System alive until draining finishes. Completion jobs must not shut down their streamer or wait for another read.

Retained output buffers belong to consumers; pending limits do not bound bytes retained after completion. Future cache/pool budgets must account for these references. Shutdown does not aggregate callback exceptions: observe the futures or explicitly route errors through consumer diagnostics.

The first disk backend uses synchronous standard-library reads on its dedicated I/O thread. It is asynchronous to callers and isolates CPU workers, but serializes disk acquisition and cannot cancel a blocked OS operation. Overlapped Windows I/O/IOCP, priorities, cancellation, prefetch and cache eviction remain later milestones.

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

1. **Current base:** memory/loose ranges, persistent I/O, job completions, limits, errors and draining. Test bounds, large offsets, missing/unknown assets, callback failures, concurrent producers, reentrant requests and cleanup. Retain the streamed Opus decode test.
2. **Scheduler/storage concurrency:** dependency graphs, cancellation/generations, priorities, budgets, backpressure and overlapped I/O. Prove slow storage does not occupy CPU workers or starve frame/audio work; measure queued/read/decode latency, bytes, failures and allocations.
3. **Bundles:** versioned cooked index, IDs, entry offsets/lengths, integrity/compression metadata. Translate ranges with overflow-safe bounds and decoded-size budgets. Compare loose, memory and bundle results through the same API/tests. No bundle backend is implemented yet.
4. **Media adapters:** streamer-backed WebM parsing/seek, real AV1/Opus fixtures, corruption, stale completions, packet ordering, decoder errors and buffer lifetimes. Containers/codecs never open files.
5. **Playback/performance:** device-loss/underrun recovery, seek cancellation, consumption-driven PCM reclamation, startup/loop/scrub and measured synchronization. Compare sequential and jobified processing on reference assets/hardware before claiming speedups.

Production UI may use the validated memory/loose streamer before bundles or full playback exist, after the scheduler's [UI-readiness gate](14-job-system-implementation-plan.md). The exclusive storage boundary and job-based consumers apply from the first resource-loading UI.
