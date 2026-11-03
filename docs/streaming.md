# Streaming

## Storage boundary and priority

Streaming infrastructure is the sole application-managed filesystem boundary: content, configuration, catalogs, codecs and rendering/audio consumers do not independently open files. Composition roots register sources and asset IDs. Build tools and isolated test-fixture setup are outside this running-application contract. The API exposes reads only; no storage write adapter is implemented.

Consumers request an asset ID, offset and exact byte count. They receive owned immutable bytes or an error, not a physical path. Memory and loose files are the implemented backends.

## Read contract

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
backend. Windows uses bounded overlapped reads through IOCP: four concurrent
file slots by default, configurable with `max_io_requests`. Queued cancellation
avoids file admission; active cancellation requests `CancelIoEx` and retains the
buffer/OVERLAPPED until OS acknowledgment, including completion races. Shutdown
drains; callers request stop explicitly when aborting. The reference backend remains
serial and cannot interrupt a blocked read. Native open/metadata/allocation still
run on the I/O owner, not CPU workers. Per-read CPU priority applies to ready work.

## Backend and lifetime

Windows native builds select an IOCP storage adapter with bounded concurrent
operations and specific-operation `CancelIoEx`. A persistent I/O owner processes
completion; buffers and handles remain alive until native acknowledgment. Queued
cancellation does not require a native slot. CPU workers remain available while
storage waits. SDK-free validation uses a serial reference file backend.

Request, active-operation and retained-output quotas have separate meanings.
Retained payload bytes stay charged until their final owner releases them. PMR
selection does not guarantee zero fragmentation or bound metadata, third-party
heaps, allocator retention or OS caches. Raw read futures remain ordinary future
objects; coordinators must avoid consuming them twice.

There is no bundle backend, storage-priority scheduler, source-generation model,
idle-handle cache or aggregate decoded-media budget. Full-asset renderer requests
currently require caller-provided encoded byte counts. See the
[streaming API](../libs/streaming/README.md) for quotas, cancellation, diagnostics
and grain selection; unresolved work is tracked in
[Streaming](https://github.com/ocarizr/atlas-ui/issues/25).
