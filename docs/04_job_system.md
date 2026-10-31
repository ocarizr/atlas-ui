# Job system

## Implementation status

The library provides direct dispatch through injected clients of one application-owned
persistent pool, dependency handles, executor-created child contexts, joined ranges
and coordinator fences. Sealed DAG submission remains available. Both modes share
workers, statistics and draining shutdown. Start with the [direct dispatch guide](18_job_dispatch_usage.md);
see [sealed graphs](17_job_graph_usage.md) and the [implementation plan](14_job_system_implementation_plan.md).
A generic phased reference now validates full and sparse updates; see the
[workload and profiling guide](19_job_workloads_and_profiling.md).

The scheduler is a reusable, client-agnostic library. UI, streaming, video and audio may use it, but their domain concepts and integration code do not belong inside `jobs`. This milestone adds scheduler behavior and generic tests only.

## Execution contract

The job system is a first-class platform capability. Reuse persistent worker threads rather than creating a thread per job. Required capabilities include child jobs, dependencies, ParallelFor, DAGs, synchronization points, and profiling of execution and dependencies.

A job with N prerequisites has a dependency count of N. Completed prerequisites decrement it safely; the transition to zero makes the job eligible for a ready queue. A dependent job must not occupy a worker while waiting for its prerequisites.

A mutex alone does not express A before B: B could acquire it first. Dependency eligibility, child completion, and mutual exclusion are distinct contracts. Establish graph edges before making dependent jobs runnable.

Idle workers may park and wake. Scheduler synchronization is acceptable; long blocking I/O must not consume the CPU pool. The streamer foundation isolates disk reads on a persistent dedicated I/O thread and dispatches ready completions as jobs. Overlapped Windows I/O remains a later milestone. All media read through this infrastructure; see the [streamer/media contract](15_streamer_and_media_jobs.md).

## Scheduling choices

Start with a persistent pool, short runnable jobs, and dependency counters. Work stealing can be added if profiling shows queue contention or imbalance. Fibers and C++20 coroutines are not required just to represent A -> B.

Worker count is configured by the composition root; shutdown drains admitted work.
Weighted priorities, cooperative cancellation and caller/worker-affine notifications
are implemented. General CPU affinity, work stealing and further allocation tuning
remain measurement-driven. The original 16-worker configuration was an example,
not a fixed hardware requirement. Worker configuration uses YAML.

## Frame boundaries

Layout, geometry, painting/render data, and events belonging to the current frame must complete within that frame's defined phases. Do not deliberately pipeline layout into one frame and geometry/painting into later frames.

Streaming may continue across frames. The UI can use a white/empty placeholder or a preloaded loading screen until a menu's assets are ready. Publish completed resources at a controlled boundary.

Proposed frame ordering:

```text
capture input / publish ready external results
    -> independent behavior and action evaluation
    -> synchronize / order events / consolidate ViewModel and ECS changes
    -> Measure / synchronize / Arrange / synchronize
    -> Geometry / synchronize / RenderData / synchronize
    -> submit rendering
```

This is not yet the final API. Define what happens to late events and cycles without silently reopening earlier phases. CPU completion/submission, GPU completion, and display presentation are different milestones; no global GPU wait per frame is assumed.

## Safe parallelism and determinism

Capture input once as a snapshot. Parallel jobs can read it, but must not race to consume the Windows message queue. Small variable writes are not automatically safe: shared state requires ownership, graph dependencies, or deterministic merging of local outputs. A barrier does not repair a race that happened before it.

The required outcome is a reproducible UI. Proposed contract: equal initial state, ordered inputs, time deltas, and external-result publication order produce equal observable state/effects, regardless of worker assignment. Bitwise equality across machines is not yet specified.

Order concurrent events by stable phase/source/local sequence rather than worker arrival time. Record inputs, clock, and external publication points for replay. The recording format is not yet defined.

## Layout graph

Measure, Arrange, Geometry, and RenderData have deterministic synchronization boundaries. Independent inventory/chat/quest subtrees can run concurrently, but internal parent/child dependencies remain. Choose task granularity large enough to amortize overhead.

## Remaining low-level contracts

| Area | Open decisions |
| --- | --- |
| Queues | Weighted FIFO priorities/fairness implemented; local queues and work stealing remain measurement-driven |
| Memory | Explicit owned inputs and borrowed ranges established; bounded scratch implemented; allocator pooling remains measurement-driven |
| Completion | Scoped joins, fences and origin-thread completion listeners implemented; worker waits rejected; suspension remains future work |
| Failure | Stable failure/cancellation propagation and skipped dependents implemented |
| Lifecycle | Fixed pool, cooperative token cancellation and draining implemented |
| Safety | Read/write access rules and structural ECS mutation |
| Diagnostics | Task names/status/timings implemented; bounded worker/dependency traces implemented; exact causal profiling remains future work |

Test exact-once execution, dependencies, children, barriers, failures and shutdown. Stress with controlled schedules/seeds and compare against sequential execution. Measure wall time, overhead, worker utilization and critical paths; the goal is shorter activities through better CPU use, not necessarily less total CPU work.

See the [priority, fairness and cancellation contract](21_job_scheduling_and_cancellation.md).

See [completion callbacks](22_job_completion_callbacks.md) for unconditional and result-specific notifications.

Direct task completion now reuses terminal publication and scheduler signaling,
removing per-task future storage. See the [Milestone 5 optimization report](23_job_scheduler_optimization.md)
for measurements and the criteria for further queue/allocator changes.

An optional terminal observer integrates the [diagnostics package](24_diagnostics_and_packages.md)
outside scheduler locks while retaining jobs' standard-library-only dependencies.
