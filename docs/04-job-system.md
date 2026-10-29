# Job system

## Implementation status

The current library implements independent jobs, persistent workers, completion handles, error propagation, idle observation, statistics and draining shutdown. Dependency counters, DAGs, child groups, ParallelFor and frame barriers below describe the target contract, not features already implemented. See the [implementation plan and UI-readiness gate](14-job-system-implementation-plan.md).

## Execution contract

The job system is a first-class platform capability. Reuse persistent worker threads rather than creating a thread per job. Required capabilities include child jobs, dependencies, ParallelFor, DAGs, synchronization points, and profiling of execution and dependencies.

A job with N prerequisites has a dependency count of N. Completed prerequisites decrement it safely; the transition to zero makes the job eligible for a ready queue. A dependent job must not occupy a worker while waiting for its prerequisites.

A mutex alone does not express A before B: B could acquire it first. Dependency eligibility, child completion, and mutual exclusion are distinct contracts. Establish graph edges before making dependent jobs runnable.

Idle workers may park and wake. Scheduler synchronization is acceptable; long blocking I/O must not consume the CPU pool. Async I/O or a dedicated blocking-I/O integration remains to be designed.

## Scheduling choices

Start with a persistent pool, short runnable jobs, and dependency counters. Work stealing can be added if profiling shows queue contention or imbalance. Fibers and C++20 coroutines are not required just to represent A -> B.

Thread count, affinity, main-thread participation, priorities, fairness, cancellation, allocation policy and shutdown behavior remain open. The original 16-worker configuration was an example, not a fixed hardware requirement. Worker configuration uses YAML.

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
| Queues | Local/global queues, work stealing, priorities and starvation prevention |
| Memory | Payload/capture lifetime, handles, allocators, publication ordering |
| Completion | Parent/child completion, continuations, fences and nested waits |
| Failure | Error/cancellation propagation and dependent-job outcomes |
| Lifecycle | Startup, shutdown, drain/cancel policy |
| Safety | Read/write access rules and structural ECS mutation |
| Diagnostics | Identity, queue/execution times, graph links and critical paths |

Test exact-once execution, dependencies, children, barriers, failures and shutdown. Stress with controlled schedules/seeds and compare against sequential execution. Measure wall time, overhead, worker utilization and critical paths; the goal is shorter activities through better CPU use, not necessarily less total CPU work.
