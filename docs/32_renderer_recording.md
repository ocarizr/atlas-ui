# Parallel renderer recording

The renderer coordinator validates the entire immutable frame before scheduling
CPU recording. It retains resource ownership and submits command lists in frame
order only after all recording work joins. The core library owns no worker pool
or dedicated render thread. `atlas::renderer_jobs` adapts an application-owned
job dispatcher to the joined `recording_executor` contract.

`recording_options` bounds batch count and requires a minimum average draw count
before adding parallel batches. Small workloads record directly on the coordinator.
Each batch owns its allocator, command list and attachment descriptor heaps until
the frame fence completes. Contiguous sections preserve pass/draw order; only the
first section performs attachment loads and only the last performs stores.

## Application usage

```cpp
atlas::jobs::job_system workers{4}; // One pool owned by the application.
atlas::renderer_jobs::executor recording{workers.dispatcher()};
atlas::renderer::pass_renderer renderer{resources};
const auto done = renderer.submit(frame, recording,
    {.min_draws_per_batch = 4096, .max_batches = 4});
```

Create the pool once and share its dispatcher with other consumers. Game and
editor inject this adapter into their shared primitive scene; their triangle
stays sequential because its workload is below the grain threshold. Core users
can link only `atlas::renderer` and call `submit(frame)` without linking jobs.

The default grain is conservative and configurable, not an online timing model.
At runtime the planner uses total draw count and the supplied grain/batch cap;
it does not guess a universal threshold from hardware concurrency. Set
`pass_limits.max_recording_batches` to bound retained native contexts and choose
`recording_options.max_batches` within that capacity. A count of one forces
sequential recording. `last_recording()` reports actual lists, worker batches and
draw counts; failures leave the last successful statistics intact.

Software adapters default to sequential recording even above the grain threshold,
avoiding competition with a CPU rasterizer. Set `allow_software_parallel = true`
only for calibration, correctness testing or a measured beneficial software
workload. Capability is cached at construction; frame submission does not copy
adapter descriptions to make this decision.

## Ownership and synchronization

| Role | Responsibility |
| --- | --- |
| Application composition | Own thread/pool lifetimes, dispatcher policy, window events and frame handoff |
| Graphics coordinator | Validate inputs, reuse contexts, join CPU recording, submit/present, publish fences and collect completed work |
| Recording workers | Read resolved immutable inputs and exclusively record their assigned command lists |

Partitions preserve all original draw/pass order, including clear-only passes.
A large single pass can span several lists. Each section binds complete graphics
state; only the first performs attachment transitions/clears, and only the last
performs stores/discards. Per-draw input transitions come from whole-frame state
simulation. CPU completion order never changes GPU submission order. All lists
enter one ordered queue submission and share one completion token.

Each in-flight frame retains its command lists, allocators and attachment heaps
until its fence completes. Pool metadata uses the device PMR resource; temporary
partitions, resolved packets and task bookkeeping use normal CPU allocation.
Native task status objects are isolated on 64-byte boundaries. Recording borrows
already retained resource/pipeline pointers instead of updating shared reference
counts per draw. This does not guarantee absence of false sharing in caller data
or vendor implementations.

The executor interface is a synchronous lifetime boundary. On success it must run
each task exactly once; on every return/exception it must have joined all work it
accepted. Borrowed task pointers must not escape that scope. Completion verification
rejects omitted/failed work before GPU execution, but cannot make an executor that
returns with outstanding threads safe. The jobs adapter uses capture-free scoped
ranges and joined completion, including failure/cancellation. Dispatcher priority
and stop token are preserved. Cancellation is checked before/after each batch;
it does not interrupt a native recording body or cancel submitted GPU work.

`jobs::require_coordinator_thread()` validates the wait context before admitting
borrowed work. Worker, executing-callback and terminal-observer callers are rejected
before scheduler admission. Workers perform no storage access, presentation, queue
submission or GPU waits.

Coordinator waits may pump origin-thread callbacks. A store transaction guard
rejects reentrant store/pipeline operations while resolved work is recording.
Defer those operations to a safe coordinator point. The guard releases on success
or failure. Destroying the currently submitting renderer violates its lifetime
contract and terminates.

Inherited diagnostics expose `renderer.record.batch` on recording threads and
`renderer.record.join` on the coordinator under `ATLAS_PROFILING=auto|on|off`.
These are CPU scopes, not GPU timestamps. Failures retain operation labels and
the job system's named original-cause diagnostics.

## Measurement and validation

Build `atlas_renderer_benchmark` in Release and run directly for hardware or with
`--warp`. Rows cover 64/1024/4096/16384 draws, one/eight passes and one/two/four
maximum batches. Parallel measurements use an explicit grain of 1024 to explore
the crossover and opt into software parallelism without changing production
defaults. Each row is
the median of seven timed iterations after two warmups. Input construction, shader
compilation and explicit GPU waits are outside the interval; validation, recording,
CPU join and queue submission are inside it. Use an otherwise idle host. Driver,
CPU, pipeline changes and other pool consumers change the crossover; this is not
an FPS or GPU-throughput benchmark.

Illustrative local measurements on Windows x64, Release, four workers, with the
debug layer and capture disabled (2026-11-02):

| Adapter | Draws / passes | One list, CPU microseconds | Four lists, CPU microseconds |
| --- | --- | --- | --- |
| GeForce RTX 5070 Ti | 4096 / 1 | 1092.2 | 875.3 |
| GeForce RTX 5070 Ti | 16384 / 1 | 3971.3 | 3283.2 |
| GeForce RTX 5070 Ti | 16384 / 8 | 4316.3 | 3423.8 |
| WARP | 4096 / 1 | 38539.3 | 37294.0 |
| WARP | 16384 / 1 | 151644.0 | 165819.0 |

These observations motivate configurable batching and the software fallback;
they are not portable performance guarantees or automated acceptance thresholds.

Tests compare full WARP images with a sequential reference, including blending,
cross-pass dependencies and reversed CPU recording. Controlled gates check worker
overlap, capacity and fence-protected reuse. Failure, cancellation, incomplete work,
worker misuse and callback reentrancy are rejected before GPU execution. Profiling
is checked with capture enabled and compiled out. SDK-free builds exercise portable
contracts and the jobs adapter. ASan/static analysis do not replace race detection;
native Windows ThreadSanitizer remains unavailable.

The backend follows Microsoft's [parallel recording ownership](https://learn.microsoft.com/en-us/samples/microsoft/directx-graphics-samples/d3d12-multithreading-sample-win32/)
and [command-list submission contracts](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists).

## Application thread integration gate

A dedicated render thread belongs to application composition: create and destroy
graphics services on that thread, retain window-message ownership on the native
window thread, and define immutable handoff, frame identities, bounded lead,
resize/close coordination and failure propagation. Integrating that window/render
lifecycle is a separate application task before production UI.

Phase six completes application integration with a dedicated render coordinator,
synchronized window presentation leases and bounded immutable CPU packets.
Cross-thread native tests cover resize/suspend/close and multiple windows while
main continues pumping messages. This still uses the original application pool.

The renderer-library recording gate and application composition gate are separate.
Both precede production UI. [Content orchestration](33_renderer_content.md) is phase
five; [thread lifecycle and GPU hardening](34_renderer_readiness.md) complete the
initial phase-six gate. Consumer-specific production budgets remain follow-up work.
