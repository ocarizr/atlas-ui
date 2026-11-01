# Atlas streaming

`atlas::streaming` is the exclusive running-application storage boundary. Consumers
request logical asset ranges and receive immutable owned bytes or an acquisition
exception. Memory sources and registered loose files are implemented. Bundles,
compression, caches, writes and complete media playback are future increments.

```cpp
atlas::jobs::job_system pool{4};
auto source = std::make_shared<const atlas::streaming::asset_bytes>(
    atlas::streaming::asset_bytes{1, 2, 3, 4});
atlas::streaming::asset_streamer streamer{
    pool, {{"menu", atlas::streaming::memory_asset{source}}}};

auto finished = streamer.read({"menu", 1, 2}, [](atlas::streaming::read_result result)
{
    // Runs on a CPU job. Consume bytes or inspect the original exception here.
    // Do not wait on storage, another job, or this streamer's shutdown.
});
finished.get(); // Only coordinator threads wait.
streamer.shutdown();
```

The shared job pool must outlive streamer draining. Stop external producers before
destruction; shutdown/destruction belongs to the creating coordinator. `read` and
`statistics` support concurrent producers/observers. The completion future reports
callback completion or callback/dispatch failure. Acquisition/cancellation errors
are delivered through `read_result`; they do not independently fail that future.
Callbacks may enqueue another read without waiting, while admission remains open.
Observe exceptions from rejected reads as well as callback futures. Shutdown closes
admission, joins storage and drains accepted delivery; it does not evict consumer
buffers or aggregate callback exceptions.

## Jobs and cancellation

The implementation uses the current `job_dispatcher` and executor-created
`job_context`, explicit retained payloads and capture-free job functions. It creates
no CPU pool. Each read dispatches one ready root; file acquisition takes place on
one persistent dedicated I/O thread. Callbacks remain worker-delivered, not
origin-thread completion listeners. Their work inherits the selected CPU priority.
Calling `read` from another job does not attach the storage request as that job's
child: it creates independent asynchronous work with explicit ownership. Only the
internal copy chunks are attached to their streamer acquisition parent. Existing
job observers/traces see the `stream.read` and `stream.copy` labels in the `streaming`
category without adding a diagnostics dependency or automatic telemetry transport.

Small copies, single-worker pools and visibly busy pools use one copy in the ready
job. Larger memory copies can spawn disjoint children when the pool is otherwise
idle. Grain is rounded to 64-byte boundaries and enlarged to cap children by the
worker count. `parallel_copy_threshold` defaults to zero (no intra-read split).
Set a measured nonzero threshold to enable the adaptive path; grain defaults to
256 KiB. Independent reads remain concurrent CPU jobs even with splitting disabled.
This is a configurable admission heuristic, not online autotuning or a claim that
parallel memcpy always wins. Concurrent assets already distribute work across roots;
extra children are avoided when other work is observed. Submission/load observations
can race with new unrelated work without affecting output correctness.

A local Release hot-memory probe with four workers, five warmups and thirty measured
reads per configuration observed these coordinator round-trip medians in microseconds:

| Bytes | Serial configuration | Opt-in split from 1 MiB |
| --- | ---: | ---: |
| 16 KiB | 10.8 | 9.0 (serial path) |
| 256 KiB | 22.8 | 22.1 (serial path) |
| 1 MiB | 292.7 | 282.5 |
| 4 MiB | 1132.6 | 1246.1 |

This probe includes fresh output allocation/initialization, copy, callback and future
notification; it is not a disk benchmark, a hardware-independent baseline or a
statistical speedup claim. The observed 4 MiB regression motivates the conservative
default. Calibrate on representative assets and hardware before enabling splitting.

`read_options` selects weighted CPU priority (background by default) and an optional
`std::stop_token`. Cancellation is checked before acquisition, by memory-copy
chunks and before callback delivery. A cancelled read delivers `jobs::job_cancelled`
as an exception result; required completion/cleanup jobs are never suppressed by
passing that token to scheduler admission. Cancellation does not roll back writes,
interrupt synchronous OS reads, or undo callbacks that already started. File reads
stay FIFO; CPU priority does not reorder or preempt disk I/O.

## Memory ownership and allocation

`asset_bytes` is now `std::pmr::vector<std::uint8_t>` rather than `std::vector`.
Existing named-alias construction, spans and immutable result handles remain usable;
code explicitly expecting the old vector type or ABI must rebuild/adapt.

The constructor optionally accepts `std::shared_ptr<std::pmr::memory_resource>`
after `streamer_limits`. Null selects `new_delete_resource`. The resource must
support concurrent allocate/deallocate calls. The streamer uses an adapter that
forwards at least 64-byte alignment, and every output pins both its upstream
resource and its quota reservation until the last shared result is destroyed.
Weak result observers do not retain byte reservations after the last strong owner
releases the buffer. An injected resource that itself borrows an upstream resource
must keep that upstream chain valid for its own lifetime.

Buffers may safely outlive the streamer. A source supplied as a `memory_asset` has
its own allocation ownership: keep its allocator alive and never mutate backing
bytes through another alias.

```cpp
auto buffers = std::make_shared<std::pmr::synchronized_pool_resource>();
atlas::streaming::streamer_limits limits;
limits.max_resident_bytes = 32 * 1024 * 1024;
atlas::streaming::asset_streamer streamer{pool, catalog, limits, buffers};
```

Do not call `release()` on an injected pool while live buffers use it. A raw
unsynchronized/monotonic resource is unsuitable for concurrent arbitrary-lifetime
streaming buffers without an external lifetime/synchronization strategy.

Limits default to 4 MiB per request, 32 accepted requests and 64 MiB of reserved or
retained **requested output bytes**. Admission rejects exhausted quotas without
dispatching callbacks or opening files. Invalid ranges and allocation failures
release reservations during delivery cleanup. Releasing an operation does not
release bytes retained by a consumer. Drop its last result reference to unload those
bytes; no implicit cache retains them.

The byte quota excludes source buffers, catalog/request metadata, shared-pointer
control blocks, job state, STL iterator proxies, alignment padding, upstream pool
retention and OS caches. It is not a bound on total process working set or physical
heap reservations. `statistics()` exposes pending/retained/peak byte and copy-path
counters plus completed/failed deliveries; counters are observational rather than
one globally atomic snapshot. Failed includes acquisition, cancellation and callback
failures; it excludes reads rejected before admission.

Fragmentation is a valid allocator/lifetime concern. The streamer controls admission,
buffer ownership, granularity and release timing; reusable pools/arenas/slabs belong
to an allocator policy. This increment provides the integration seam, not a new
general allocator or a guarantee of zero fragmentation. It affects payload storage,
not every metadata allocation in the library. Jobs already expose a separate PMR
upstream for task scratch; scratch lifetimes differ from retained streaming output.

## Platforms and next increments

`ATLAS_TARGET_PLATFORM=windows` selects the current target. The CMake cache enumerates
`linux`, `macos`, `android`, `ios`, `xbox`, `playstation` and `switch` as reserved names;
they fail explicitly until platform support exists. Xbox is not silently treated
as Windows Desktop. Root project support remains Windows-only; the APIs and job
library retain platform-neutral types and the jobs standalone build stays independent.

With `ATLAS_BUILD_WINDOWS_INTEGRATION=ON`, CMake compiles only the native implementation
under `libs/platform/windows/streaming/`. The SDK-free profile selects the standard
implementation under `libs/streaming/src/platform/standard/`. Both implement the same
private interface and run the same behavioral tests. Neither exposes SDK types in
public headers. The native backend currently uses synchronous Windows handles on
the persistent I/O thread: OS latency cannot occupy CPU workers, but disk requests
are still serialized and files are opened per request. IOCP, bounded handle reuse
and interruptible disk operations are the next storage-concurrency increment.

Further work includes bundle indexes/ranges, request generations, cache/prefetch
policy, storage timing/telemetry adapters and a workload-specific performance gate.
See the [design and implementation sequence](../../docs/15_streamer_and_media_jobs.md).

## Validation and remaining gates

Local validation of this increment passed 37 streaming tests in Release, ASan,
MSVC analysis, clang-tidy, the SDK-free profile and LLVM coverage. The full Debug
suite passed 331 tests, and tooling passed 217 pytest cases. Source-style,
architecture/configuration/link checks and check-only Doxygen validation passed.

Fresh LLVM profiles for the six native implementation translation units report
95.46% line and 90.00% branch coverage. This scope excludes headers, tests,
third-party code and the SDK-free implementation, which has separate behavioral
validation. Remaining unexercised paths include low-level Windows metadata/seek/
short-read failures, some allocation-admission failure edges and allocator identity.
The next storage increment needs a controllable fault/latency backend rather than
timing-dependent filesystem races. Coverage/access checks do not prove race freedom
or absence of fragmentation; native Windows ThreadSanitizer remains unavailable.

To run the native contract suite after configuring the development environment:

```powershell
cmake --build --preset windows-debug --target atlas_streaming_tests
ctest --test-dir build/windows-msvc -C Debug -L streaming --output-on-failure --no-tests=error
```

References: [C++ pool-resource contracts](https://eel.is/c++draft/mem.res.pool) and
[Windows synchronous/asynchronous I/O](https://learn.microsoft.com/en-us/windows/win32/fileio/synchronous-and-asynchronous-i-o).
