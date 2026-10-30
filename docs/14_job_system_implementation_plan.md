# Job-system implementation plan

## Priority and UI-readiness gate

The scheduler is foundational and client-agnostic. Complete milestones 0-3 with
generic synthetic workloads before implementing client integrations. UI, streaming,
video and audio consumers compose the library without adding domain concepts to it.

The streamer is the next foundation alongside independent jobs, before production UI/media. Its memory/loose backend dispatches completions as jobs and isolates disk reads on a persistent I/O thread. Establish this exclusive boundary now and evolve scheduling with the graph milestones. See the [streamer/media plan](15_streamer_and_media_jobs.md).

## Milestone 0: independent-job foundation (implemented)

- A fixed set of persistent workers and a synchronized ready queue.
- Independent `submit` operations with completion handles and repeatable coordinator-side waits.
- Callback exceptions captured and rethrown to the coordinator without losing a worker.
- Idle completion, statistics snapshots, stop-accepting semantics, draining and worker joins.
- Invalid worker counts, callbacks and foreign handles rejected.
- Worker-side scheduler waits/shutdown rejected rather than silently deadlocking.
- Tests for exact-once execution, worker reuse, concurrent producers, nested submission, failures and lifecycle.

This implementation deliberately uses standard-library synchronization, `std::function`, promises and shared futures. It is a correctness baseline, not a lock-free/zero-allocation scheduler or a finished frame graph.

`wait(handle)` makes the callback's effects visible and rethrows its exception. `wait_idle()` observes an empty queue and no running callbacks; independent concurrent producers can submit again afterward. For this legacy independent API, `shutdown()` rejects new submissions, including nested submissions, drains accepted work and joins workers. Active scoped contexts introduced below can still admit children during draining. It is idempotent and serialized for concurrent coordinator calls. Destruction must happen outside workers and after external users stop accessing the scheduler.

Observe a failed independent callback through its handle: idle waiting and shutdown do not aggregate or rethrow job failures. Submitted graphs now provide aggregate failure reporting and per-node diagnostics.

User callbacks must finish and must not block on scheduler dependencies or long I/O. A scheduler cannot make arbitrary C++ callbacks nonblocking. Controlled gates in tests exercise concurrency and teardown; they are not a recommended production scheduling pattern.

## Milestone 1: dependencies and sealed DAGs (implemented)

Build dependency graphs before dispatch with `job_graph::add()` and `precedes()`.
`job_system::submit_graph()` validates cycles and snapshots callbacks and topology;
the accepted execution is immutable even if its builder is later edited. Pending
prerequisite counters and successor lists publish only ready jobs. Failure skips
transitive dependents while independent branches finish. The execution handle
exposes status/original exceptions; its aggregate wait reports a stable named
failure. Preparation is all-or-nothing, and successor publication reuses prepared
queue nodes and traversal storage. The sealed graph API retains its original callback contract; scoped parent/child
completion is provided by direct dispatch below. See [examples and lifecycle rules](17_job_graph_usage.md).

Acceptance: chains, fan-in/fan-out, zero/many dependencies, invalid/foreign references, cycle rejection, exact-once publication, prerequisite failure propagation and non-execution of blocked/failed successors. Test counter publication under concurrent completion. Allocation and submission failures must not leave an unfinishable graph.

## Milestone 2: ranges, lifetimes and synchronization (dispatch increment implemented)

Direct dispatch supplies `run`, `after` and named fan-in fences through an injected
client of one application-owned pool. Executor-created contexts spawn scoped
children; parent completion joins descendants and releases owned input before
publishing completion. Capture-free callbacks receive explicit, move-capable data.
Range operations use caller-selected grains, a small-range path and disjoint
chunks. Handles expose names, status, original failures and completion timings.
See the [usage and ownership contract](18_job_dispatch_usage.md).

The current error policy drains admitted work, joins children after a parent
failure, skips failed dependents and selects stable failures by registration order.
Shutdown permits active contexts to finish spawning while rejecting new roots.
No cancellation API, arena/scratch allocator, task category policy, automatic grain
tuning or measured optimization is claimed. Parent-owned data supplies scoped
storage today. Evaluate the remaining allocation/diagnostic needs and grain sizes
with milestone 3 measurements; richer cancellation belongs to milestone 4.

Acceptance: empty and uneven ranges, every index exactly once, child completion before parent/group completion, deterministic reductions where required, safe shutdown/cancellation and no dangling captures. Compare single-worker and multi-worker outputs. Keep memory ownership and access declarations explicit.

## Milestone 3: generic phased-workload proof before client integrations

Exercise generic phased dependency workloads, snapshots and deterministic merges
using scheduler-owned synthetic tests. Keep client concepts out of `jobs`.
Measure/Arrange/Geometry/RenderData and streamer/media integration are separate
consumer tasks, rather than requirements to implement those domains inside the
scheduler. A later UI integration will establish its actual frame boundaries.

Acceptance: phase work completes within its owning batch; incremental execution
equals a full sequential reference; outputs remain stable across worker counts and
seeds; no worker waits for prerequisites; cycles cannot silently reopen an earlier
phase. Record reference hardware, task counts, critical path, allocations, wall time
and overhead. Demonstrate benefit on independent work without hiding regressions
on small workloads.

**Scheduler readiness means milestones 0-3 have tests, Windows sanitizer/static-analysis
evidence, traceable ownership and measured generic phased workloads.** Actual UI
frame boundaries, resource acquisition and codec scheduling remain separate
integration tasks. Work stealing, fibers, bundles and full playback are not
prerequisites for the scheduler's dependency API.

## Milestone 4: background-work scheduling

Evaluate generic priorities/QoS, cancellation and fairness for mixed foreground and
background CPU work. Measure starvation and admission behavior without teaching
the scheduler about particular consumers. Overlapped I/O, storage completions,
demux and codec integration belong to the separate [streamer/media plan](15_streamer_and_media_jobs.md).

## Milestone 5: measured scheduler optimization

Evaluate local queues, work stealing, allocation pools, affinity and optional continuations/coroutines only when traces justify them. Preserve the reference semantics/tests. Optimize wall time and critical paths rather than maximizing job count or imposing lock-free machinery without evidence.

## Validation strategy

Use GoogleTest fixtures/parameterized cases and controlled synchronization, with finite CTest timeouts for deadlock diagnostics. Run Debug/Release, ASan, MSVC analysis, clang-tidy and LLVM coverage on Windows. Stress dependency/lifecycle races with reproducible schedules and repeated runs. Native TSan remains unavailable on the selected Windows toolchain; do not claim race freedom from ASan or coverage.
