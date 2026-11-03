# Job system

`atlas::jobs` is a generic, standard-library-only C++20 scheduler. One
application-owned persistent pool serves independent consumers through injected
dispatch clients. It contains no UI, graphics, storage or codec policy.

## Execution and completion

Direct dispatch returns dependency handles. Executor-created contexts can submit
independent jobs, sequenced continuations or explicitly attached children. An
attached child's completion contributes to its parent's terminal result; ordinary
jobs submitted inside another job do not become children implicitly. Joined ranges
cover independent disjoint work. Sealed DAG submission is available for explicit
batch construction. Both APIs use the same workers and draining lifecycle.

Prerequisites make a task runnable only when their completion conditions permit it.
Waiting dependents do not occupy a worker. Mutex exclusion is distinct from ordered
dependency execution. Worker-side blocking waits are rejected; coordinators use
fences/handles at their own synchronization boundaries. A job handle does not prove
storage, GPU or audio-device completion.

Weighted high/normal/background FIFO queues have documented service bounds.
Cancellation is cooperative; pending suppression and terminal propagation preserve
failure precedence. Completion listeners support outcome filters, caller mailboxes
and originating-worker affinity. Their delivery lifetime and pumping rules are
part of the [callback contract](job_callbacks.md).

## Ownership and safety

Inputs may be explicitly owned; borrowed references must outlive all users.
Scoped scratch has bounded ownership. The optional declared-access checker detects
specified overlapping accesses and scope misuse; it cannot infer every memory
access, prove race freedom or detect all false sharing. ASan and controlled
concurrency tests supplement ownership rules; Windows native TSan is unavailable.

Independent jobs may run in any valid schedule. Deterministic consumer state requires
stable inputs, disjoint writes or ordered merging; barriers cannot repair a race
that occurred before synchronization. The generic phased workload compares full
and sparse execution with a sequential reference, not a production UI frame budget.

## Limits

Task admission and retained completion state do not have a global memory bound.
The scheduler has no work stealing, CPU affinity, suspended fibers/coroutines or
automatic grain calibration. Short work can cost more to dispatch than to execute
serially. Consumer budgets and publication/replay policies require their own evidence.
Outstanding work is tracked in [Jobs](https://github.com/ocarizr/atlas-ui/issues/24).

## Guides

- [Direct dispatch and child/range ownership](job_dispatch.md)
- [Sealed graphs](job_graphs.md)
- [Scheduling and cancellation](job_scheduling.md)
- [Callbacks](job_callbacks.md)
- [Workloads and measurement](job_workloads.md)
- [Declared-access safety and standalone reuse](../libs/jobs/README.md)
- [Diagnostics integration](diagnostics.md)
