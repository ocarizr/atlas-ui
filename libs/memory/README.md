# Atlas memory

`atlas::memory` provides an application-owned concurrent `std::pmr::memory_resource`.
It is reusable infrastructure for any allocator-aware library. Jobs and streaming
remain independent of mimalloc and consume the standard PMR interface.

The application chooses the resource and owns its lifetime. A library may borrow
or retain it according to its public contract; it must not replace the global heap
or install a process-wide PMR default as a hidden side effect.

## Backend and scope

The pinned vcpkg manifest currently supplies mimalloc 3.5.3 under the MIT license.
The overlay keeps the upstream allocator source/version/checksum and disables
Windows CRT redirection, including the separate `MI_WIN_REDIRECT` switch. Explicit
`mi_malloc_aligned`/`mi_free_size_aligned` calls coexist with other heaps. No per-thread private
heap is exposed or managed by consumers.

`ATLAS_MEMORY_BACKEND` selects `auto`, `mimalloc` or `system`:

- `auto` uses mimalloc in normal builds and the system backend with native ASan.
- `system` uses `std::pmr::new_delete_resource` for comparison and sanitizer checks.
- Explicit mimalloc with native ASan is rejected until that vendor/toolchain
  combination has a separately validated instrumentation contract.

The backend is visible through `backend_name()`. ASan validation of the system
configuration does not imply that mimalloc-managed memory is instrumented.

Only allocations explicitly routed through this resource use this policy. Ordinary
`new`, `malloc`, STL containers with their default allocators, job metadata,
third-party DLL heaps and factory/shared-pointer metadata keep their existing
allocation paths. This increment does not replace allocation throughout the process.
Future systems can use the same PMR resource without adding a mimalloc dependency.
Current application bootstrap code injects it into job scratch; the streamer accepts
the same resource when composed with an asset catalog.

## Composition and lifetime

```cpp
auto allocations = atlas::memory::make_resource();
atlas::jobs::job_system_options options;
options.scratch.upstream = allocations.get();
atlas::jobs::job_system jobs{options};

atlas::streaming::asset_streamer streamer{jobs, catalog, {}, allocations};
// Request bytes and schedule ready consumers here.
streamer.shutdown();
jobs.shutdown();
```

Keep `allocations` alive until all borrowing scratch scopes and PMR containers
finish. Job scratch retains its existing block/budget/bulk-release policy; this
resource supplies its upstream blocks. It does not change child-job lifetimes.

The streamer retains shared allocator ownership in its buffers. Outputs may outlive
both the streamer and the original owner; releasing the last output releases that
ownership. General PMR containers borrow a raw resource pointer and do not obtain
that shared-lifetime guarantee automatically.

Allocation/deallocation may run concurrently and on different threads. Return each
pointer through its original resource with the original size/alignment. Resources
compare equal only by identity. Never mix a PMR allocation with CRT `free`, raw
`delete` or another resource's accounting. Empty allocations remain paired, and
requests exceeding `PTRDIFF_MAX` fail with `std::bad_alloc` before backend admission.

## Observation and performance

`make_resource({true})` enables successful allocation/release counts, live requested
bytes and their high-water mark. Counters default to disabled to avoid shared
atomic updates on every allocation. Snapshots are observational, not globally atomic.
Failed requests do not change the counters.

These counters exclude allocator metadata/caches, virtual reservations, committed
pages, alignment overhead and unrelated allocations. They measure ownership of
requested bytes, not total memory pressure or fragmentation. Streamer admission
budgets and job scratch budgets remain separate responsibilities.

The Release `atlas_memory_benchmark` compares the system baseline with the configured
resource, with counters disabled. It measures batches of sixteen allocations/frees,
touching only endpoints, at 64 B, 4 KiB, 64 KiB and 4 MiB. After warmup it reports
the median of five samples of 64 batches. `--smoke` shortens the run for CTest and
asserts usable storage/checksums, never a timing threshold. This is allocation churn,
not disk throughput, full-buffer initialization or a fragmentation/working-set study.

```powershell
cmake --build --preset windows-release --target atlas_memory_benchmark
./build/windows-msvc/benchmarks/memory/Release/atlas_memory_benchmark.exe
```

One local Windows x64 / MSVC Release run observed these median nanoseconds per
allocation/free pair with counters disabled:

| Requested bytes | System baseline | Mimalloc resource |
| --- | ---: | ---: |
| 64 B | 24.0 | 6.0 |
| 4 KiB | 21.4 | 26.5 |
| 64 KiB | 2422.6 | 49.9 |
| 4 MiB | 9510.2 | 267.2 |

This warmed batch workload favors allocator reuse, touches only endpoints and is
not a controlled project-wide speedup study. Mimalloc was slower at 4 KiB in this
run. Cached pages can reduce allocation latency while retaining memory; these
numbers do not establish lower committed memory, less fragmentation or faster asset
loading. Reproduce on representative workloads before tuning policy.

## Validation boundaries

The independent `memory` CI component needs only the memory/testing vendor products.
`memory-integration` separately tests job scratch, loose-file buffers, cancellation,
callback failures and allocator lifetime with imported consumer prerequisites.
Native Windows tests assert that no process-wide redirector is loaded. Shared
contract tests cover aligned/empty allocations, size rejection, identity, optional
accounting, concurrent callers and cross-thread release. Run normal mimalloc and
system/ASan configurations; Windows ThreadSanitizer remains unavailable.

Allocator selection does not guarantee zero fragmentation or solve false sharing
between logically independent data. Representative retained-buffer, workload latency
and process commit/working-set measurements remain necessary before claiming a
project-wide performance or memory improvement.

References: [mimalloc integration and licensing](https://github.com/microsoft/mimalloc),
[PMR resource contract](https://eel.is/c++draft/mem.res), and
[job scratch ownership](../jobs/README.md#scoped-scratch-memory).
