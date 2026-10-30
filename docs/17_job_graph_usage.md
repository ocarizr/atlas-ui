# Job graphs

The `atlas::jobs` library schedules generic CPU callbacks. Its graph API has no UI,
streaming, codec, frame, ECS or platform-specific concepts. Link `atlas::jobs` and
include `<atlas/jobs/job_system.hpp>`.

For incremental submission without a graph builder, start with the
[direct dispatch API](18_job_dispatch_usage.md). Both APIs use the same pool.

## First graph

```cpp
#include <atlas/jobs/job_system.hpp>
#include <iostream>

int main()
{
    atlas::jobs::job_system scheduler{4};
    atlas::jobs::job_graph graph;
    int value = 0;

    const auto produce = graph.add("produce", [&] { value = 21; });
    const auto transform = graph.add("transform", [&] { value *= 2; });
    const auto consume = graph.add("consume", [&] { std::cout << value << '\n'; });

    graph.precedes(produce, transform);
    graph.precedes(transform, consume);

    const auto execution = scheduler.submit_graph(graph);
    scheduler.wait(execution); // Prints 42. Only the coordinator waits.
}
```

`precedes(a, b)` means that `b` may start only after `a` has succeeded. Declare all
edges before submitting. Jobs with no prerequisites are ready immediately. A
pending job occupies no worker; the last completed prerequisite makes it ready.
An edge also publishes the prerequisite's writes to its successor. It does not
serialize unrelated jobs or protect data shared by sibling jobs.

For fan-out, make multiple jobs depend on the same prerequisite. For fan-in, make
one job depend on multiple prerequisites:

```cpp
graph.precedes(produce, transform_left);
graph.precedes(produce, transform_right);
graph.precedes(transform_left, combine);
graph.precedes(transform_right, combine);
```

Here `transform_left`, `transform_right` and `combine` are IDs returned by `add()`.
The transforms can run concurrently; `combine` waits for both results.

## Errors and diagnostics

Each job needs a nonempty diagnostic name and a callback. Names need not be unique;
IDs, rather than names, identify jobs. Default IDs and IDs from another graph are
rejected. Duplicate edges are ignored. Self-dependencies and longer cycles are
rejected at submission, with a diagnostic naming a blocked job. A rejected graph
publishes no callbacks and changes no scheduler counters.

A callback exception marks that job `failed`. Its transitive dependents become
`skipped` once their prerequisites have all reached terminal states. Their
callbacks never run. Independent branches continue, and graph completion waits
for every branch. The scheduler remains usable.

```cpp
try
{
    scheduler.wait(execution);
}
catch (const atlas::jobs::graph_error& error)
{
    std::cerr << error.what() << '\n'; // Includes the failed job's name.
    std::rethrow_exception(error.cause()); // Original exception and type.
}
```

When several callbacks fail, `wait(execution)` reports the first failed job in
builder insertion order, independent of completion order. Inspect other failures
with `execution.exception(id)`; it returns an empty pointer for jobs that did not
directly fail. `execution.status(id)` safely reports `pending`, `ready`, `running`,
`succeeded`, `failed` or `skipped`. Polling diagnostics is optional; it is not a
dependency mechanism. Waiting on the same execution again repeats its outcome.

## Ownership and lifecycle

- A builder is edited by one coordinator, with externally synchronized access.
  `submit_graph()` copies its callbacks and topology into a separate execution.
  Later edits, moving or destroying the builder do not change accepted work.
- IDs survive builder moves. A moved-from builder rejects operations. IDs added
  after submission are invalid for that earlier execution.
- A builder can be submitted again. Each execution has independent dependency
  counters and outcomes, but reference/shared-pointer captures may still share
  application data. The caller must make concurrent executions safe.
- Captures and referenced objects must remain valid until their consumers finish.
  The scheduler does not infer ownership or resolve conflicting writes.
- Dropping a handle does not cancel accepted work. Completed handles retain their
  statuses and exceptions even after scheduler destruction. Scheduler waits require
  a live originating scheduler; diagnostic reads do not.
- Workers may submit independent jobs or entire graphs. They must not wait,
  shut down or destroy a scheduler. A graph does not implicitly wait for jobs
  created by its callbacks; injected child contexts belong to the direct API.
- `wait_idle()` observes all currently accepted jobs, including pending graph
  nodes, but does not report callback failures. Concurrent producers can submit
  again afterward. Use execution handles to observe failures.
- `shutdown()` rejects new submissions and drains accepted graphs, including their
  pending nodes, before joining workers. It does not cancel callbacks. Callbacks
  must finish without blocking workers on dependencies or long I/O.
- Callback captures for executed and skipped jobs are released outside scheduler
  locks. `shutdown()` joins workers and finishes this cleanup. Graph/idle waits
  establish callback completion, not necessarily the destruction of all captures.

Preparation may allocate and copy callbacks. If preparation throws, no part of
that execution is accepted. Ready-queue links and failure-propagation storage are
prepared before publication; making successors ready or skipped does not require
new scheduler queue allocations. This is not a zero-allocation scheduler: callback
code, exception machinery and diagnostic exception construction may allocate.

## Statistics and limits

Statistics are snapshots under the scheduler lock. `queued` counts ready jobs;
`waiting` counts unfinished prerequisites and, for direct tasks, descendant joins
and cleanup. `completed` counts all terminal jobs, including `failed` and `skipped`. At each snapshot:

```text
submitted = queued + running + waiting + completed
```

The queue remains shared and mutex-protected. There are no priorities, cancellation,
dynamic dependency edits, cross-graph edges, work stealing or timing traces.
Direct dispatch supplies child joins, parallel ranges and per-task timings. Graph
results are deterministic only when the application's data access and callbacks are deterministic; worker execution order
is deliberately unspecified. See the [implementation plan](14_job_system_implementation_plan.md).
