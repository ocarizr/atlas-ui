# Atlas Jobs

A reusable C++20 CPU-job library with persistent workers, dependency handles,
scoped children, parallel ranges and an optional declared-access sanitizer.
Windows x64 is currently validated. Public APIs and scheduling code are platform
agnostic; there are no Windows SDK, UI, ECS, codec or application dependencies.

Direct tasks share one completion state for status, result and waits. Completed
handle waits avoid the scheduler mutex; pending waits still cooperate with the
caller's notification queue. See the [measured optimization report](../../docs/23_job_scheduler_optimization.md)
for allocation/timing evidence and the limits of the current scheduler.

The [follow-up backlog](../../docs/25_job_system_backlog.md) records outstanding
production contracts, safety/observability limits and deferred optimization gates.

Version 0.2 adds an optional `job_system_options::observer` for terminal direct-task
events outside scheduler locks. `wait_idle()` and `shutdown()` drain these deliveries;
source handles may become observable before delivery finishes. Observer callbacks
are noexcept and cannot wait, reuse saved contexts or perform external I/O. The
[diagnostics adapter](../jobs_diagnostics/README.md) integrates logging and Chrome
traces without adding spdlog to jobs. Rebuild matching clients after this layout change.

## Three explicit dispatch relationships

The application creates one `job_system` and injects copyable `job_dispatcher`
views into consumers. A dispatcher does not create workers. Inside a callback:

```cpp
jobs.run("discover", [](atlas::jobs::job_context& context)
{
    context.dispatcher().run("independent", [](atlas::jobs::job_context&) {});
    context.spawn("child", [](atlas::jobs::job_context&) {});
    context.continue_with("after discover", [](atlas::jobs::job_context&) {});
});
```

- `dispatcher().run`: an independent root; the current job does not join it or
  inherit its failures. Keep its own handle when its result matters.
- `spawn`: an attached child, admitted from the executing parent's callback.
  The parent joins all descendants and retains its input until they finish.
- `continue_with`: a separate root depending on completion of the current job,
  including descendants and input cleanup. It is not joined by that current job.
  If the current job fails, the continuation is skipped.

Only attached children inherit the parent's input lifetime. Independent roots
and continuations must own their data or share storage with sufficient lifetime.
A continuation starts after the prior input has been released: a raw pointer or
span into that input would already dangle. Guards do not repair such ownership.

`context.after(handle, ...)` creates a child with an additional prerequisite;
`dispatcher.after(handle, ...)` creates a separate dependent root. A child cannot
wait for its own parent's completion: that would form a cycle. Context methods
require the originating callback on its executing thread. A saved dispatcher may
be used by other producers while the runtime is alive; a saved context is not a
cross-thread scheduling client. Shutdown rejects new roots, including independent
jobs and continuations, while accepted callback contexts may still spawn children.

## Safety layers

| Layer | Contract |
| --- | --- |
| Compilation, always | Direct callbacks return void and carry no captures/state; input is explicit. `write_access` rejects const spans. Access guards cannot be copied; discarding them produces a nodiscard diagnostic. |
| Runtime, always | Validate handles, runtime ownership, cycles, contexts, names and functions. Reject worker waits/shutdown. Own task inputs and join children before releasing them. Validate declared region descriptors. |
| Job sanitizer | Check simultaneously live declared access scopes; reject overlapping read/write or write/write scopes across jobs. Allow read/read scopes. Retire and report guards escaping the callback. Warn about disjoint scopes sharing an assumed cache line. |
| Compiler tools | Use ASan for instrumented memory errors and static analysis for additional findings. These are separate from the job sanitizer. |

The job sanitizer is enabled when building the library, not by an IDE or a source
scan. CMake's public usage requirements propagate its configuration through every
link to `atlas::jobs`, including imported packages and transitive consumers.
Instrumentation runs on exercised jobs during tests/application execution; it does
not run user workloads at compilation time. Normal C++ constraints are compile-time
checks. Existing independent `submit` and sealed graph callbacks keep their earlier
contracts and have no injected context for declared memory scopes.

## Declare accesses before touching shared memory

```cpp
auto done = jobs.run("update", std::span<int>{values},
    [](atlas::jobs::job_context& context, std::span<int>& data)
    {
        auto access = context.access(atlas::jobs::write_access(data));
        for (auto& value : data) { ++value; }
    });
done.wait();
```

`read_access(span)` permits const or mutable spans. `write_access(span)` requires
mutable elements. `context.access({read_access(a), write_access(b)})` registers a
batch of scopes atomically with respect to other access registrations. Keep the
move-only guard until the last access, and destroy it inside the callback. Guard
moves transfer registration; exceptions release scoped guards through RAII.
An empty span is valid. A guard borrows memory; it does not own or pin storage.

`parallel_for` automatically declares each chunk for the duration of its callback:
const element spans are reads, mutable element spans are conservatively writes.
Use const spans for read-only ranges. Arbitrary payload pointers and globals need
explicit declarations; their targets cannot be inferred from the C++ type.

A detected conflict throws `job_sanitizer_error` before granting the second scope;
normal task failure/skip propagation applies unless the callback catches it.
An escaped guard is invalidated at callback return and fails an otherwise successful
job. If the body already failed, preserve that original exception and still report
the misuse. The checker does not silently serialize conflicting jobs or add edges.

`runtime.sanitizer_report()` returns enabled status and bounded diagnostics with
job names, kind and severity. Conflicts and escaped guards are errors. Possible
false sharing is a warning; it does not fail or delay a job. Review warnings in
integration tests/profiling; they are collected, not printed automatically.
`job_sanitizer_options` configures the assumed cache-line size (default 64 bytes,
a positive power of two) and diagnostic limit (default 128). Extra events increase
`dropped`; reaching the limit does not disable conflict rejection. In validation,
assert that unexpected errors and dropped diagnostics are absent.

These are conservative checks of **declared scopes in one runtime on exercised
paths**, not instrumentation of every C++ load/store. They cannot prove arbitrary
race freedom, ownership, pointer lifetime, determinism or performance. Guards that
are too short, missing declarations, aliasing beyond a span, multiple pools and
unexecuted paths leave gaps. Atomic accesses/external locks are not modeled;
place declarations inside an actual lock's scope or use an appropriate analysis.
False-sharing warnings are cache-line heuristics, not hardware contention samples.
Do not use their absence as proof or insert padding without measurement.

## Scoped scratch memory

`context.scratch()` returns a thread-safe `std::pmr::memory_resource` owned by the
current task. It is created lazily. Containers can use it for temporary buffers:

```cpp
std::pmr::vector<int> temporary{&context.scratch()};
temporary.resize(256);
```

A task's scratch remains alive until its body, descendants and input cleanup
finish. Children may borrow parent-owned buffers or the parent's resource. Keep shared
containers in parent-owned input, not callback stack locals: backing storage
survival does not extend the lifetime of a destroyed container or its elements. Their
own `context.scratch()` provides a separate scope. Independent roots and
continuations do not inherit this lifetime. Destroy nontrivial objects/containers
normally: bulk memory release does not run their destructors. Individual
deallocations do not reclaim capacity; blocks are freed together before completion
is published, even if completed handles remain alive.

`job_system_options.scratch` configures block size, per-task reserved-byte budget
and upstream resource. Defaults are 4 KiB blocks and a 4 MiB budget per used task.
Budget exhaustion throws `std::bad_alloc`, with normal task failure propagation.
The upstream resource must outlive all work using it and support concurrent
allocation/deallocation from different workers when shared by several scopes. The budget counts block
storage, not allocator metadata or the sum of every task in the application.
The application may supply [Atlas memory](../memory/README.md) as this upstream;
jobs retain their standard PMR contract and do not link mimalloc themselves.
`handle.scratch_usage()` becomes available after completion and reports successful
allocation calls, requested bytes, block count and the scope's reserved high-water
mark. Memory is already released when these terminal statistics are read.

## Bounded task tracing

Use `job_system_options` at the composition root when collecting a trace:

```cpp
atlas::jobs::job_system_options options;
options.worker_count = 4;
options.trace_capacity = 4096;
atlas::jobs::job_system runtime{options};
auto done = runtime.dispatcher().run(atlas::jobs::job_label{"transform", "processing"},
    [](atlas::jobs::job_context&) {});
done.wait();
auto report = runtime.trace_report();
```

Existing string names remain valid. Categories are diagnostic tags; they do not
set priority. Children inherit an empty category from their parent, including
parallel chunks. Independent roots/continuations can receive explicit labels.
Handles expose runtime-local IDs and category views, retained as long as the handle.

Tracing is disabled by default. When enabled, the runtime keeps at most the first
`trace_capacity` admitted incremental tasks. Records include IDs, parent joins,
prerequisite IDs, worker index, terminal outcome, scratch usage and submission,
callback-start/end and completion timestamps relative to runtime creation.
`pending` counts admitted records not completed yet; `dropped` exposes capacity
exhaustion. A bounded snapshot is not a complete trace when either is nonzero.
Task records contain no callbacks, borrowed buffers or exception objects.

Collection is opt-in and changes scheduling/allocation overhead. Take snapshots
after a batch where practical; copying live reports holds the scheduler lock.
The legacy independent-submit and sealed-graph APIs retain their existing contracts
and are not included in this incremental-task recorder. The repository's benchmark
exports its own XML trace and reports callback occupancy, not OS CPU utilization.
Scheduling order and timestamp values are observations, not deterministic outputs.
Skipped records describe dispatch/cleanup intervals; their callbacks did not run.
Rebuild consumers with the matching exported headers/library. String-name call
sites remain valid, but mixing previously compiled interfaces is not supported.

## Build, install and reuse

The complete library and checker live in this directory. Copy it or add it as a
subdirectory; no repository-specific CMake helpers or vendor packages are required.

```powershell
cmake -S libs/jobs -B build/jobs -G "Ninja Multi-Config" -DATLAS_JOB_SANITIZER=ON
cmake --build build/jobs --config Debug
cmake --install build/jobs --config Debug --prefix build/jobs-install
```

In the consuming project's CMake:

```cmake
find_package(atlas_jobs 0.2.0 CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE atlas::jobs)
```

Set `CMAKE_PREFIX_PATH` to the installed prefix. C++20, the toolchain Threads target
and the checker definition propagate through `atlas::jobs`. Configure/build the
matching library variants before installing Debug and Release into one prefix.
The same install/export works from the complete Atlas project. The package includes
this guide, public headers, the compiled library and CMake metadata.

`ATLAS_JOB_SANITIZER=AUTO` (default) enables access tracking in Debug only. `ON`
enables it for every configuration; `OFF` disables tracking. Rebuild the library
to change this; overriding its exported macro in a consumer is unsupported.
Release/AUTO and OFF keep contract/descriptor checks but skip active-access tracking
and its per-access allocations. Sanitized measurements include checker locking and
allocation overhead: use OFF when measuring scheduler performance.

Run the complete repository's jobs tests under Debug, Release, ASan and static
analysis. Native Windows ThreadSanitizer is currently unavailable; ASan and the
job checker do not replace it. See the upstream [LLVM sanitizer documentation](https://clang.llvm.org/docs/AddressSanitizer.html)
and [ThreadSanitizer platform list](https://clang.llvm.org/docs/ThreadSanitizer.html).

## Priorities and cooperative cancellation

Configure a client with `dispatcher.with_options(job_options)` to select
`job_priority::high`, `normal` or `background` and an optional `std::stop_token`.
One weighted 4/2/1 ready-queue policy serves the application-owned pool; default
clients and legacy graphs retain normal priority. Children inherit their parent's
policy. `context.dispatcher()` creates an independent default client; explicitly
configure it to share a token or priority with other roots.

A requested token suppresses callbacks selected afterward. Running callbacks use
`context.stop_requested()`/`throw_if_cancelled()`; normal return still commits
success. Cancelled children join before parent payload/scratch cleanup. Dependents
are skipped with the original cause. Real failures outrank cancellation. Handles
expose `job_status::cancelled`, and waits keep the `job_error` wrapper with a
`job_cancelled` cause. Cancellation never rolls back writes or forcibly terminates
threads. Request owned stop sources before shutdown when desired; shutdown drains.

Traces include priority and dispatch order; statistics expose cancelled outcomes
and per-class queued/dispatched counts. See the complete [policy, fairness bounds,
examples and lifetime contract](../../docs/21_job_scheduling_and_cancellation.md).
Rebuild consumers with matching headers and libraries.

## Completion listeners

Use `jobs.callbacks()` or `context.callbacks()` to register `on_complete`,
`on_success`, `on_failure`, `on_cancelled` and `on_skipped` for a direct task handle,
including ranges and fences. Each registration returns a notification handle.
Callbacks receive a scoped context, the immutable completed source handle, and
optional explicit data; capture-free functions follow the same input policy as jobs.

Register on the source's submitting thread and, for scoped children, inside their
submitting job. Independent-root listeners remain independent, including listeners
for `continue_with`; they do not extend the creating job's input lifetime.
Coordinator notifications run through `dispatch_callbacks(limit)`
or cooperative coordinator waits/draining. Child notifications run on the parent's
worker after its body returns, keep the parent's scope alive and join descendants.
`on_complete` observes every terminal result. Specific filters do not run on other
results; their notification handles complete successfully without invoking the function.
Observer errors remain in their own handles and do not overwrite the source.
Cancellation does not suppress delivery. Callbacks must not reenter waits/shutdown.

See [the full callback contract and examples](../../docs/22_job_completion_callbacks.md)
for pumping, affinity, owner-thread exit, payload lifetimes and trace/statistics changes.

```cpp
auto delivered = jobs.callbacks().on_complete(
    source, "observe", [](atlas::jobs::job_context&, const atlas::jobs::task_handle& result)
    {
        // This observer explicitly forwards source errors through its own handle.
        if (auto error = result.exception()) { std::rethrow_exception(error); }
    });
delivered.wait(); // Coordinator only; pumps its mailbox until this notification joins.
```

Keep borrowed coordinator data alive through delivery. Child listeners may borrow
owned parent input, but not a returned function's stack. If the owner thread exits,
undelivered notifications fail instead of running the function on another thread.
