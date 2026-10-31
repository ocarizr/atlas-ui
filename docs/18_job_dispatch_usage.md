# Direct job dispatch

Link `atlas::jobs` and include `<atlas/jobs/job_system.hpp>`. Direct dispatch is
the recommended starting point. The [sealed graph API](17_job_graph_usage.md)
remains available when an entire topology is known before submission.

## One pool, injected clients

Create one `job_system` at the application composition root. Pass its lightweight,
copyable `job_dispatcher` to consumers; copying a dispatcher creates no workers.
There is no global singleton. Separate runtime instances remain possible for tests
and explicit isolation, but each consumer should reuse its injected client.

```cpp
#include <atlas/jobs/job_system.hpp>
#include <memory>

atlas::jobs::task_handle calculate(atlas::jobs::job_dispatcher jobs,
                                  std::shared_ptr<int> result)
{
    using atlas::jobs::job_context;
    auto produce = jobs.run("produce", result,
                            [](job_context&, auto& value) { *value = 21; });
    return jobs.after(produce, "double", std::move(result),
                      [](job_context&, auto& value) { *value *= 2; });
}

int main()
{
    atlas::jobs::job_system runtime{4};
    auto result = std::make_shared<int>(0);
    auto done = calculate(runtime.dispatcher(), result);
    done.wait(); // Coordinator sync point; *result == 42.
}
```

`run(name, function)` also accepts a context-only callback when no input data is
needed. Jobs can start before submission returns. `after(handle, ...)` registers
a dependency immediately; an initializer list or `std::span<const task_handle>`
provides fan-in. A completed prerequisite is valid; repeated prerequisites count
once. Default handles and handles from another runtime are rejected before
publication. An empty prerequisite list behaves like `run`.

```cpp
auto joined = jobs.fence("batch complete", {left, right});
auto next = jobs.after(joined, "next phase", [](atlas::jobs::job_context&) {});
next.wait();
```

The fence is a no-op job joining the specified handles, not a global worker
barrier. Other independent jobs may continue. Each returned handle has its own
completion counter. These counters represent prerequisites and descendant joins;
they are not shared mutexes whose acquisition order establishes sequencing.

## Executor-owned child context

Every callback receives a `job_context` from the executor. `spawn` creates an
attached child immediately. `context.after` creates an attached child that also
has explicit prerequisites. A parent becomes terminal only after its body and
all descendants finish, even on a single worker. No worker waits for that join.
Use `context.after(child, ...)` for a continuation inside the parent scope.

```cpp
auto parent = jobs.run("parent", [](atlas::jobs::job_context& context)
{
    auto first = context.spawn("first", [](atlas::jobs::job_context&) {});
    context.after(first, "second", [](atlas::jobs::job_context&) {});
});
parent.wait(); // Both children and their cleanup have finished.
```

A context can be used only by its originating callback on its executing thread.
Retaining or copying one does not grant another thread a child-submission capability.
Child dependencies that would create a completion cycle are rejected, including
cycles through another parent's children. Use `context.dispatcher().run(...)` for independent work and
`context.continue_with(...)` for a separate job after the current job and descendants
finish. Both return normal handles; neither is joined by the current job or inherits its
input lifetime. Pass owned/shared data rather than borrowing the current job's
input, which is released before a continuation begins. A failed
current job skips its continuation. Root dispatch through a dispatcher
from inside a callback is independent work: use the supplied context when the
work must participate in the parent's lifetime and outcome.

## Explicit data and ranges

Direct callbacks must be capture-free lambdas, empty callable objects or non-null
function pointers returning `void`. Input is supplied separately by value and
owned by the task; move-only input is supported. The executor releases that input
outside scheduler locks, after descendants finish and before publishing task
completion. Child data may therefore borrow parent-owned storage. Stack locals
inside the parent callback do not gain that lifetime guarantee.

Pointers, shared pointers and spans still refer to externally shared storage.
The API does not infer access modes or prevent races through those references.
Dependency edges publish preceding writes; siblings need disjoint writes or
explicit synchronization. Capture restrictions make sharing visible, but cannot
eliminate false sharing: independent writes in the same cache line may still
contend. Choose layout and grain sizes using measurements.

```cpp
std::vector<int> values(1024);
auto mapped = jobs.parallel_for("increment", std::span{values}, 64,
    [](atlas::jobs::job_context&, std::span<int> chunk)
    {
        for (auto& value : chunk) { ++value; }
    });
mapped.wait(); // Keep values alive and do not access it concurrently until here.
```

Include `<vector>` for this example. Each callback receives a disjoint contiguous
chunk, with a shorter final chunk when necessary. Empty input invokes no user
callback; input at or below the grain executes directly on the asynchronous root.
Larger input creates scoped children. Grain must be positive. `context.parallel_for`
attaches a range operation to the current job. A span is borrowed; its backing
storage must remain valid until completion. There is no automatic grain tuning or
parallel reduction; define a deterministic merge explicitly when required.

## Failure, shutdown and diagnostics

A throwing callback fails its task. Children already admitted still drain. A
failed child fails its parent, and failed prerequisites skip successor callbacks
after all prerequisites finish. An exception in the parent's own body takes
precedence; otherwise child registration order or declared prerequisite order
selects the reported failure independently of worker completion order.

`handle.wait()` throws `job_error` (an alias of the existing `graph_error`) with
the originating job name and original `cause()`. `exception()` returns the
original exception pointer after completion, or an empty pointer while pending
or successful. Repeated waits reproduce the outcome. Keep names for diagnostics;
they do not identify dependencies. `name()` returns a view valid while the handle
retains its task. `status()` and `ready()` are nonblocking observations, including
`joining` while descendants or task cleanup remain outstanding.

`timing()` is available only after completion and reports dependency wait, ready
queue wait, callback execution, descendant join/cleanup and total duration.
These wall-clock observations are not a tracing system or a performance claim.
`statistics().waiting` includes unfinished prerequisites, descendant joins and
cleanup. `completed` includes failed and skipped tasks; failed parents count as
failed outcomes too. The existing accounting invariant remains:

```text
submitted = queued + running + waiting + completed
```

Dropping handles does not cancel tasks. Completed handles survive runtime
destruction, but dispatchers and contexts do not keep the pool alive. Stop external
users before destroying the runtime. `shutdown()` rejects new roots, allows active
contexts to finish admitting children, drains accepted work and joins workers.
Callbacks and cleanup must finish; there is no preemption. The [scheduling and
cancellation API](21_job_scheduling_and_cancellation.md) supports cooperative stop tokens,
priority clients and inherited child policy.
`wait_idle()` does not aggregate failures. Observe failures through task handles.

Workers cannot call scheduler waits, handle waits or shutdown. Express dependencies
with `after`; keep blocking storage access off this CPU pool. The legacy independent
and sealed graph APIs share the same pool but retain their earlier contracts:
their callbacks have no injected child context, and their waits need not release
all callback captures. No UI, streaming or media integration is added here.


## Optional job sanitizer and reuse

Debug builds automatically enable the declared-access checker; `ATLAS_JOB_SANITIZER`
selects AUTO/ON/OFF. Parallel ranges declare chunk access automatically. Other jobs
can use `context.access` with read/write spans. See the library's standalone
[safety, diagnostics and packaging guide](../libs/jobs/README.md) for checks,
limitations, false-sharing warnings and CMake package consumption.
