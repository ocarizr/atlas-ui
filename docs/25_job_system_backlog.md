# Job-system gaps and follow-up work

## Status and scope

Milestones 0-4 and completion notifications have their planned foundation;
milestone 5 has its first measured optimization pass. The library is ready for
controlled consumer integration, not a claim that every production requirement
or optimization is complete. See the [implementation plan](14_job_system_implementation_plan.md)
and [measured optimization report](23_job_scheduler_optimization.md).

This backlog includes the optional diagnostics integration added with jobs 0.2.
It records known limitations and open decisions, not newly discovered failures.
Keep the scheduler generic: UI phases, asset publication, codec ownership and
device behavior belong to consumer libraries and composition roots.

Priorities express recommended order, not release commitments:

- **P1:** establish before relying on the corresponding production workload.
- **P2:** improve validation, observability or reuse as consumers mature.
- **Measured:** implement only after evidence justifies the added machinery.

## Production contracts and integration gates

### JOB-01: admission limits and backpressure — P1

Ready-queue fairness is implemented, but admitted task/dependency storage is
unbounded. Per-task scratch budgets and bounded diagnostic captures do not bound
the pool's total memory. A producer can enqueue work faster than workers finish it.

Define admission accounting, capacity configuration, overload results and how roots,
children, continuations and notifications share capacity. A worker must never block
waiting for capacity that only its own descendants can release. Preserve cleanup
and draining for already accepted work; do not silently drop required jobs.

Completion evidence: controlled saturation/recovery tests, concurrent producers,
nested fan-out, failure/cancellation/shutdown at capacity, and measured memory bounds.

### JOB-02: representative consumer budgets — P1

Synthetic phased workloads establish scheduler semantics and a performance baseline.
They do not establish production UI frame budgets, streaming throughput, decoder
latency or audio deadlines. Weighted 4/2/1 dispatch is not a real-time guarantee;
running callbacks are not preempted and must cooperate with cancellation.

Each consumer must define ownership, grain sizes, synchronization/publication
boundaries, cancellation checkpoints and acceptable latency/memory budgets. Compare
against a sequential reference where applicable. Exercise mixed short/long work,
late high-priority arrivals and background load, including failure and cleanup.

Completion evidence: representative consumer reports with hardware/configuration,
latency distributions, throughput, allocations, correctness and instrumentation cost.
Do not run long I/O or device callbacks as blocking CPU jobs. Streamer/codec work
follows its [own integration plan](15_streamer_and_media_jobs.md).

### JOB-03: deterministic publication and replay — P1 for deterministic consumers

Generic ordered merges are tested across seeds and worker counts. The scheduler
does not make arbitrary shared writes, callback arrival order or external results
deterministic. Dispatch IDs and timestamps are observations, not replay inputs.

Consumers must define stable event ordering, clock/input snapshots, external-result
publication and late-event/cycle policies. The replay recording format and bitwise
equality requirements across machines remain undecided. UI frame boundaries belong
to UI integration, not to scheduler internals.

Completion evidence: recorded scenarios reproduce the agreed observable state and
effects across worker counts, including late assets, failures and cancellation.

## Safety and validation limits

### JOB-04: declared-access coverage — P1 for each adopting consumer

The optional job sanitizer checks simultaneously live declared scopes in one
runtime. It does not instrument every C++ access or prove pointer lifetime,
determinism or race freedom. Undeclared globals/pointers, undersized or short-lived
guards, separate pools and unexecuted paths remain outside that evidence.

Integrations must audit borrowed data and declarations, use const ranges for reads,
and check unexpected diagnostics and dropped reports. Atomic operations and external
locks have no dedicated access model. False-sharing warnings use an assumed cache
line; they are not measured hardware contention or a reason to pad automatically.

Completion evidence: each consumer has ownership/access tests and sanitizer-on
failure-path coverage. Any checker extension needs an explicit model and positive/
negative tests; document unsupported cases rather than claiming universal safety.
See the [safety contract](../libs/jobs/README.md#safety-layers).

### JOB-05: concurrency and failure-path evidence — P2, ongoing

Windows ASan, static analysis, controlled stress and coverage are available. Native
Windows TSan is unavailable; these checks are not equivalent dynamic race detection.
The architecture is portable, but Windows x64 is the validated platform today.

Review uncovered branches and add meaningful tests for reachable failures, especially
allocation/admission cleanup and lifecycle interleavings as those mechanisms change.
Use deterministic fault injection where a concrete path needs it, not timing sleeps
or automatic retries that conceal a scheduler defect. No numeric coverage minimum
or proof of race freedom has been established.

Completion evidence: reproducible regression cases, justified coverage exclusions,
stress seeds and actual tool/platform results. Additional platform/race-detector
validation requires an explicit supported toolchain and CI plan.

## Observability and API coverage

### JOB-06: causal profiling and instrumentation overhead — P2

The optional terminal observer exports direct-task execution/completion events,
parent IDs and timing metadata through the diagnostics adapter. The bounded native
task recorder also retains prerequisite IDs. Neither currently provides a complete
causal Chrome trace, lock-contention samples or an exact scheduler critical path.
The benchmark's work-only critical-path lower bound is not that profiler.

Design dependency-flow correlation, batch/runtime identity for combined captures,
queue/lock measurements and loss reporting before adding more hot-path hooks.
Measure disabled/enabled logging, capture and sanitizer overhead separately, with
complete and saturated captures. Terminal observations cannot visualize an
in-progress callback until it completes; arbitrary observers must return and can
otherwise prevent draining.

Completion evidence: known chains/fan-in/joins produce interpretable causal traces,
capture loss remains visible, and repeatable overhead/memory measurements accompany
the chosen implementation. Live editor views and physical trace/log persistence
are separate diagnostics/storage integrations, not scheduler features.
See the [adapter contract](../libs/jobs_diagnostics/README.md).

### JOB-07: legacy API policy — P2

Legacy independent `submit` and sealed DAG callbacks retain their earlier contracts:
future-based completion, no injected context/access scopes, and no coverage by the
incremental-task recorder or new terminal observer. Direct dispatch, ranges,
children and notifications have the newer integration.

Choose whether to retain/document that split, provide an adapter, or migrate APIs
with a compatibility policy. Do not silently change callback ownership or pretend
all API families already offer identical diagnostics and safety guarantees.

Completion evidence: documented feature matrix and tests for the chosen supported
paths, preserving graph cycle rejection, failure propagation and existing lifetimes.

### JOB-08: package release and compatibility policy — P2

Jobs and diagnostics are independently buildable/installable, with version checks.
The monorepo's normal build and owned CI artifacts still use one matching revision.
There is no package registry/update workflow or supported mixed-release matrix yet;
compiled public-layout changes require rebuilding matching consumers.

Define distribution, dependency locks, compatible compiler/CRT variants and upgrade
tests before promising delayed internal upgrades or extracting repositories.
Completion evidence: external consumers build against the supported version matrix
and incompatible combinations fail clearly. See [package strategy](24_diagnostics_and_packages.md).

## Optimizations requiring evidence

| ID | Candidate and current limitation | Evidence required before implementation |
| --- | --- | --- |
| JOB-09 | Task/link allocation pools; remaining allocations and the shared scheduler mutex still exist | Allocation/lock measurements show a material cost; retained handles, cleanup outside locks and shutdown semantics remain correct |
| JOB-10 | Worker-local queues/work stealing; dispatch currently uses shared weighted FIFO queues | Measured contention/imbalance, with tests preserving fairness, dependencies and origin-worker notifications |
| JOB-11 | General worker/core affinity; only notification affinity is established | A consumer locality requirement and measured benefit across representative hardware |
| JOB-12 | Fibers/coroutines; arbitrary callback suspension/resumption is unavailable | A workload cannot reasonably use dependencies/children/continuations; define stack/context lifetime, cancellation and shutdown first |
| JOB-13 | Automatic grain selection; callers currently choose range grains | Representative crossover measurements and a predictable policy that retains explicit caller control |

These candidates are not missing milestone acceptance requirements. Small work may
remain faster as a direct loop. Do not advertise lock-free, allocation-free or
real-time execution without implementing and validating those contracts.

## Recommended next increment

Prioritize JOB-01 if production producers can create sustained or bursty fan-out.
Use JOB-02/JOB-04 as adoption gates for the next consumer; JOB-03 applies when
deterministic publication is introduced. Extend JOB-06 to obtain evidence before
choosing JOB-09 through JOB-13. Keep each change independently testable and update
this backlog with its completion evidence.
