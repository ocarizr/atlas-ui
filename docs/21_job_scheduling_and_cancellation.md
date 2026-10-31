# Job scheduling and cooperative cancellation

Milestone 4 adds generic scheduling policy to the direct-dispatch API. It does not
introduce UI, storage, codec or application concepts into the jobs library.

## Configure a client, keep one pool

`job_dispatcher::with_options(job_options)` returns another lightweight client of
the same pool. Existing calls still use normal priority and no cancellation token.
Options are copied into each accepted task; changing a client does not modify work
already accepted. An invalid priority is rejected before admission.

```cpp
std::vector<int> values(4096, 21);
atlas::jobs::job_system runtime{4};
std::stop_source stop;
auto jobs = runtime.dispatcher().with_options({
    .priority = atlas::jobs::job_priority::background,
    .cancellation = stop.get_token(),
});
auto work = jobs.parallel_for(
    "transform", std::span{values}, 128,
    [](atlas::jobs::job_context& context, std::span<int>& range)
    {
        for (auto& value : range)
        {
            context.throw_if_cancelled();
            value *= 2;
        }
    });

// Any owner of this standard C++ stop_source can request the batch to stop.
stop.request_stop();
try
{
    work.wait();
}
catch (const atlas::jobs::job_error&)
{
    // Inspect work.exception(): cancellation is job_cancelled; other errors remain errors.
}
runtime.shutdown();
```

One token can group multiple independent jobs, clients or pools. Destroying a
source does not request cancellation; tokens retain its stop state. The scheduler
does not register synchronous stop callbacks or execute user cleanup from
`request_stop()`.

## Priorities and fairness

The pool has three FIFO ready queues: `high`, `normal`, and `background`. A weighted
credit round grants 4, 2 and 1 dispatches respectively. Selection prefers the
highest ready class with remaining credit. When no nonempty class has credit,
credits reset; empty queues never idle a worker. Prepared list nodes move between
queues without allocation under the scheduler mutex.

FIFO means ready-publication order within a class. A dependency-bound job becomes
eligible only when all prerequisites are terminal. Running callbacks are never
preempted, and priorities do not modify OS thread priority or worker count. The
legacy `submit` and sealed-graph APIs use the normal queue and retain their existing
callback/error contracts; configurable policy belongs to direct dispatch.

A continuously nonempty ready class receives service within at most 13 successful
dequeues, including the next service. This conservative bound covers previously
spent credit and new arrivals: at most one residual round plus the higher-priority
part of the next round can precede it. With all three queues continuously supplied,
each full round contains 4/2/1 dispatches. This bounds service for a class's head;
older FIFO entries still precede later jobs in that class.

These are dispatch-count bounds, not real-time deadlines, CPU-time shares or finish
order guarantees. A long/nonreturning callback can occupy a worker regardless of
priority. Split CPU work into finite chunks; keep blocking I/O off these workers.
Ready background dependencies still receive service when high-priority dependents
and other high-priority roots coexist. There is no priority donation or affinity.

Admission remains unbounded by default and accepts prepared work while the runtime
is open. Background backlog does not prevent higher-priority ready admission, but
still consumes memory. Cancellation does not turn admission into a drop policy;
even pre-cancelled requests produce observable completion handles and need prepared
storage. Capacity limits/backpressure require a separate measured contract.

## Scope and cancellation outcomes

| Operation | Priority/token policy | Completion ownership |
| --- | --- | --- |
| Configured dispatcher `run`, `after`, `fence`, `parallel_for` | Copy client options | Independent root |
| Context `spawn`, `after`, `parallel_for` | Inherit originating task options | Child joined by its parent |
| Context `continue_with` | Inherit originating task options | Independent successor of parent completion |
| Context `dispatcher().run` | Default normal/no token unless explicitly configured | Independent root |

Cancellation of a source affects tasks carrying its token, not unrelated jobs just
because their submission happened inside another callback. Ordinary sequencing
also does not implicitly share a cancellation token.

- If the token is requested when a worker selects an otherwise runnable task, its
  callback is suppressed and its status becomes `cancelled` after cleanup.
- A running callback checks `context.stop_requested()` or calls
  `context.throw_if_cancelled()`. Throwing `job_cancelled` records cancellation;
  returning normally commits callback success even if a request raced with completion.
  A parent still joins and aggregates its children's outcomes.
  A callback can also explicitly throw `job_cancelled` when its own work is obsolete.
- Built-in range admission checks cancellation between chunk submissions; chunks
  inherit the token and are checked again at dispatch. Application callbacks choose
  checkpoints inside a chunk.
- Existing children always join before parent input/scratch release and completion,
  including cancellation and failure. Throwing `job_cancelled` propagates a completion
  outcome; it does not request the shared token or cancel unrelated siblings.
  Cancellation is not rollback: writes and
  external side effects already performed remain visible.
- Pending tasks still wait for prerequisites to become terminal before cleanup.
  Cancelled prerequisites suppress dependent callbacks as `skipped`, preserving the
  cause. Cancelling a token does not detach dependency edges or release borrowed data
  early. A failed prerequisite takes precedence over a task's pre-dispatch request.
- Non-cancellation failures outrank cancellation when joining children or selecting
  prerequisite errors. Within each kind, registration order remains deterministic.
  A real parent callback failure outranks inherited failures. Sanitizer ownership
  violations are real failures even if the callback cancelled.
- `wait()` preserves the existing `job_error` wrapper and source name;
  `exception()`/`job_error::cause()` expose the original `job_cancelled`. Completed
  handles remain observable after pool destruction; late requests never rewrite
  terminal outcomes. No recovery callback is implicitly run after a failed or
  cancelled prerequisite.

Context policy/cancellation methods require the originating callback on its worker,
just like child creation and scratch access. Worker-side waits remain forbidden.
Default shutdown still drains. Request owned stop sources before `shutdown()` to
drain cancelled work; active contexts may admit children during draining, and those
children inherit their parent's token. Shutdown does not force termination of
callbacks, cancel external I/O or request caller-owned stop sources automatically.

## Diagnostics and evidence

`task_handle::priority()` exposes the immutable class. Bounded task traces include
priority and a pool-local `dispatch_index`, in addition to cancellation/skipped
outcomes. Indices also count legacy dispatches, so gaps in incremental traces are
expected. The benchmark XML exports both fields as additive schema-1 attributes.
Suppressed callbacks have zero execution duration in `task_timing`; trace intervals
still delimit dispatch and bookkeeping. Timestamps and dispatch indices are
diagnostics, not replay inputs or stable ordering of independent effects. For
repeatable cancellation outcomes, publish requests at controlled application
boundaries; concurrent request timing can change which callbacks/checkpoints run.

`job_statistics` adds `cancelled`, `queued_by_priority` and
`dispatched_by_priority`, with array order high/normal/background. Queued counts
exclude pending dependencies and joining parents. Dispatch counts include legacy, ready graph and direct entries. A direct entry
whose callback is suppressed still consumes a dispatch; graph nodes skipped without
reaching a ready queue do not. `completed`
includes succeeded, failed, cancelled and skipped outcomes; `failed` excludes
cancelled outcomes. Rebuild consumers with matching headers and static libraries.

Controlled tests exercise all three queues with 192 direct tasks and 1/2/4 workers,
plus legacy jobs occupying the workers before publication. Trace assertions verify
exactly-once completion, per-class dispatch totals and service gaps: at most 13 for
the initial residual round and 7 thereafter for this fixed cohort. A late-arrival
case exercises the 13-dispatch worst case; a separate 128-entry background backlog
checks urgent admission. Tests also cover pending/running cancellation, ranges,
unrelated roots, continuation suppression, joins, error precedence, cleanup,
shutdown and completed-handle lifetimes. Tests use controlled gates rather than
wall-time performance thresholds. Windows ASan/static analysis and repeated runs
supplement these invariants; native Windows TSan remains unavailable.

Work stealing, pools, priority donation, queue capacity limits, deadlines, automatic
grain tuning and consumer-specific QoS budgets remain separate work. The
[Milestone 3 baseline](20_job_workload_baseline.md) predates this scheduling policy
and is historical performance evidence, not a new performance claim.
