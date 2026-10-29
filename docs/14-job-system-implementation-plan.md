# Job-system implementation plan

## Priority and UI-readiness gate

The scheduler is foundational. Do not begin production UI systems against a sequential placeholder and retrofit jobs later. Complete milestones 0-3 below before building jobified UI layout; use synthetic UI-shaped data to validate the scheduler first.

## Milestone 0: independent-job foundation (this delivery)

- A fixed set of persistent workers and a synchronized ready queue.
- Independent `submit` operations with completion handles and repeatable coordinator-side waits.
- Callback exceptions captured and rethrown to the coordinator without losing a worker.
- Idle completion, statistics snapshots, stop-accepting semantics, draining and worker joins.
- Invalid worker counts, callbacks and foreign handles rejected.
- Worker-side scheduler waits/shutdown rejected rather than silently deadlocking.
- Tests for exact-once execution, worker reuse, concurrent producers, nested submission, failures and lifecycle.

This implementation deliberately uses standard-library synchronization, `std::function`, packaged tasks and shared futures. It is a correctness baseline, not a lock-free/zero-allocation scheduler or a finished frame graph.

`wait(handle)` makes the callback's effects visible and rethrows its exception. `wait_idle()` observes an empty queue and no running callbacks; independent concurrent producers can submit again afterward. `shutdown()` rejects all new submissions, including nested submissions, drains accepted work and joins workers. It is idempotent and serialized for concurrent coordinator calls. Destruction must happen outside workers and after external users stop accessing the scheduler.

Observe a failed callback through its handle: idle waiting and shutdown do not aggregate or rethrow all job failures. Diagnostics and graph-wide failure propagation belong to the following milestones.

User callbacks must finish and must not block on scheduler dependencies or long I/O. A scheduler cannot make arbitrary C++ callbacks nonblocking. Controlled gates in tests exercise concurrency and teardown; they are not a recommended production scheduling pattern.

## Milestone 1: dependencies and sealed DAGs

Build dependency graphs before dispatch. Add job IDs/handles and immutable graph submission, pending-prerequisite counters and successor lists. Only ready jobs enter worker queues. Define parent/child completion separately from dependencies.

Acceptance: chains, fan-in/fan-out, zero/many dependencies, invalid/foreign references, cycle rejection, exact-once publication, prerequisite failure propagation and non-execution of blocked/failed successors. Test counter publication under concurrent completion. Allocation and submission failures must not leave an unfinishable graph.

## Milestone 2: ranges, lifetimes and synchronization

Add chunked ParallelFor, child groups, coordinator fences/barriers, graph-local scratch lifetime and explicit error/cancellation policy. Choose grain sizes by measurement, with a sequential path for small ranges. Introduce category/name metadata and queue/execution/barrier timing.

Acceptance: empty and uneven ranges, every index exactly once, child completion before parent/group completion, deterministic reductions where required, safe shutdown/cancellation and no dangling captures. Compare single-worker and multi-worker outputs. Keep memory ownership and access declarations explicit.

## Milestone 3: frame-graph proof before UI

Build synthetic Measure/Arrange/Geometry/RenderData stages over dirty subtrees with parent/child dependencies. Capture input/model snapshots and merge event/property writes deterministically at phase boundaries. Separate background resource publication from current-frame work.

Acceptance: no visual-stage work leaks into later frames; dirty-only execution equals a full sequential reference; stable ordered outputs across worker counts/seeds; no worker blocks on prerequisites; no event cycle silently reopens an earlier phase. Record reference hardware, task counts, critical path, allocations, wall time and overhead. Demonstrate benefit on independent subtrees without hiding regressions on small workloads.

**UI-ready means milestones 0-3 have tests, Windows sanitizer/static-analysis evidence, traceable ownership and a measured synthetic frame graph.** It does not require work stealing, a fiber runtime or the full streaming system.

## Milestone 4: streaming workloads

Add priority/QoS, cancellation, bounded queues, resource budgets and async/dedicated blocking-I/O integration. Background operations may span frames; only usable resource generations are published at controlled boundaries. Prevent streaming/decode from starving frame-critical work.

## Milestone 5: measured scheduler optimization

Evaluate local queues, work stealing, allocation pools, affinity and optional continuations/coroutines only when traces justify them. Preserve the reference semantics/tests. Optimize wall time and critical paths rather than maximizing job count or imposing lock-free machinery without evidence.

## Validation strategy

Use GoogleTest fixtures/parameterized cases and controlled synchronization, with finite CTest timeouts for deadlock diagnostics. Run Debug/Release, ASan, MSVC analysis, clang-tidy and LLVM coverage on Windows. Stress dependency/lifecycle races with reproducible schedules and repeated runs. Native TSan remains unavailable on the selected Windows toolchain; do not claim race freedom from ASan or coverage.
