# Job completion callbacks

The direct-dispatch API supports completion listeners through `jobs.callbacks()`
and `context.callbacks()`. A listener receives an executor-created `job_context`,
the completed source `task_handle`, and optional explicit owned data. Functions
must be capture-free, just like job functions. Registration returns a separate
`task_handle` for the notification and any children it creates.

## Events and outcomes

| Registration | Source result that invokes the function |
| --- | --- |
| `on_complete` | Any terminal result, including failure, cancellation and skipping |
| `on_success` | `succeeded` |
| `on_failure` | `failed` |
| `on_cancelled` | `cancelled` |
| `on_skipped` | `skipped` |

The generic `on(completion_event, ...)` form selects the same filters. Sources must
belong to the same runtime. Invalid/default/foreign sources, null functions and
empty labels are rejected before publishing a notification. Several listeners may
observe the same source. Registration also works after source completion, subject
to the origin-thread/scope rules below; it never invokes the function inline.

Observers differ from `after`: failed prerequisites do not suppress delivery.
The immutable source result is passed to the listener, including its original
exception, timings and identity. Notification success does not change or recover
the source result. For a nonmatching filter, the notification completes successfully
without invoking the function: its handle represents completion of the delivery
decision, not success of the source.

Listener exceptions are captured in the notification handle. They do not rewrite
the source or prevent other listeners from executing. Keep and inspect/wait the
notification handle to observe these errors. `wait_idle()` and `shutdown()` drain
work without aggregating/rethrowing job failures. For child notifications, the
parent joins their outcomes using the normal deterministic failure policy.

Notification delivery does not inherit the source's cancellation token. Otherwise
an `on_cancelled` or unconditional cleanup listener could itself be suppressed by
the request it needs to observe. A listener may explicitly throw `job_cancelled`;
this cancels the notification, not the observed source. The source's skipped status
matches `on_skipped`, even if its stored cause originated in another failed job.

## Thread affinity and pumping

Register on the thread that submitted the source. For a scoped child, register
while its submitting job's callback is active. Registrations for children from
another or expired job scope are rejected. Independent roots can be observed later
on their original thread, without retaining the creating job's scope.
Thread identity is not inferred solely from an OS identifier that could be reused.
Worker-origin callbacks must belong to the same pool; cross-pool worker-origin
registration is rejected. Legacy `submit` and sealed graph handles retain their
existing API; listeners attach to direct task handles, ranges and fences. Legacy
callbacks have no scoped context and cannot register worker-bound listeners.

- **Coordinator submission:** delivery uses that coordinator thread's mailbox.
  Call `runtime.dispatch_callbacks(limit)` at controlled application-loop points.
  Waiting on task handles, `wait_idle()` and `shutdown()` also pump the calling
  thread's mailbox while they need progress. They never pump somebody else's queue.
- **Scoped child submission:** delivery is pinned to the same persistent worker.
  It executes after the submitting callback releases the worker, as a child
  notification in that job's scope. The parent stays alive and joins it.
- **Independent root submission inside a job:** delivery is also pinned to the
  submitting worker, but the notification remains an independent root. This includes
  `context.dispatcher().run(...)` and `context.continue_with(...)`. Neither source
  nor listener becomes an implicit child. A continuation can therefore complete
  after its creator without introducing a completion cycle.
- **Jobs submitted inside a caller-thread listener:** the listener has its own
  scoped context. Joined children and their listeners keep that scope alive; caller
  delivery continues through the same mailbox.

No worker waits for a child, and no new worker is created for a listener.

Root registrations made inside other jobs/listeners need their own explicit data
ownership: they do not extend the creator's input lifetime. Source-child listeners
retain the original submitting scope. During draining, new root registrations are
rejected; active scopes may still register joined child listeners.

A callback cannot interrupt arbitrary C++ execution on its owner thread. An owner
must return to the dispatcher or a supported wait. A busy/blocking job delays its
worker-bound listeners. This is deferred execution on the same physical thread,
not resumption of a returned C++ stack and not a fiber/coroutine implementation.

`dispatch_callbacks(limit)` processes at most that many notification tasks and
returns the number processed, including nonmatching delivery decisions. A budget
such as 64 can bound event-loop work in task counts; it cannot bound a long callback
in milliseconds. `source.wait()` waits for the source, not all its listeners.
Wait the returned notification handle when delivery must be complete before
continuing. It can also be a prerequisite for `jobs.after(...)`.

Source metadata is already terminal in the listener. Read `status()`, `exception()`
or timings directly. Callbacks must not call task waits, `wait_idle()`, `shutdown()`
or recursively pump callbacks. These operations are rejected even for a listener
executing on the coordinator, preventing recursive waits and lifecycle deadlocks.

An exiting coordinator closes its mailbox. Queued or future deliveries to that
mailbox fail with `Callback owner thread exited before delivery`; the user function
is never redirected to a different thread. Accepted work is still drained and the
notification's error remains observable. A live coordinator must pump/drain its
notifications before abandoning participation; another thread cannot execute them
on its behalf. Concurrent shutdown callers each drain their own mailbox before
serializing the final worker joins.

## Coordinator example

Here `jobs` is an injected dispatcher and `runtime` is the application-owned pool.
The caller keeps the borrowed buffers/report alive through notification completion.

```cpp
struct batch_report
{
    atlas::jobs::job_status status{atlas::jobs::job_status::pending};
};
batch_report report;
auto work = jobs.parallel_for(
    "transform", std::span{values}, 128,
    [](atlas::jobs::job_context&, std::span<int>& chunk)
    {
        for (auto& value : chunk) { value *= 2; }
    });
auto delivered = jobs.callbacks().on_complete(
    work, "report result", &report,
    [](atlas::jobs::job_context&, const atlas::jobs::task_handle& result,
       batch_report*& output)
    {
        output->status = result.status();
    });

// A synchronous coordinator can wait this handle; an event loop can pump instead.
delivered.wait();
```

The listener observes completion after all range chunks and descendants join and
source cleanup finishes. `delivered.wait()` can succeed while `work` failed:
the reporting listener succeeded at recording the failure.

## Child scopes and ownership

Inside a parent callback whose explicit input is `data`, register through its
context. The borrowed pointer below points to owned job input, not a stack local:

```cpp
auto child = context.spawn("calculate", [](atlas::jobs::job_context&) {});
context.callbacks().on_complete(
    child, "consume child", &data,
    [](atlas::jobs::job_context&, const atlas::jobs::task_handle& result,
       parent_input*& owner)
    {
        owner->last_status = result.status();
        // Use the injected context to create joined descendants when needed.
    });
```

The callback receives a new notification context; do not retain/reuse the original
parent context or borrow the parent function's stack locals. Parent payload and
scratch survive through the listener and its joined descendants. Independent roots
do not extend these lifetimes. Source payload/scratch are already released when a
listener runs; keep shared results in an explicit owner rather than borrowing freed
source input. The affinity guarantee applies to function execution. Payload cleanup
after joined descendants follows the usual jobs contract and may occur on the last
completing thread; thread-affine resources need an appropriate explicit owner.

Same-source listeners in the same priority queue are published in registration
order. Different sources can finish in different orders. Use dependencies or stable
merging when observable side effects require deterministic order.

## Scheduling and diagnostics

General ready work retains Milestone 4's global weighted priority credits. Each
worker alternates eligible pinned notifications with general work; local notification
queues use their own weighted credits and never refill the global queue's credits.
This prevents affinity traffic from silently invalidating the general queue's
service policy. Affinity constrains which worker can make progress, so service
still depends on the owner returning from its callback.

Statistics include queued notifications in `queued`/`queued_by_priority` and active
caller deliveries in `running`. `callbacks_dispatched_by_priority` counts affinity
deliveries; `caller_dispatched` is their coordinator-thread subset. General
`dispatched_by_priority` continues to count the shared worker queue, including
abandoned-delivery cleanup routed there without invoking the user function.

Traces mark `affinity_bound` and `caller_thread`. Affinity deliveries use
`dispatch_index == 0`; general worker queue indices keep their existing meaning.
Caller deliveries use `worker_index == size_t::max()`; worker-bound deliveries keep
the actual worker index. The benchmark exporter excludes caller intervals from
worker occupancy and emits the execution kind. Trace capacity includes notification
tasks. Rebuild clients with matching headers/libraries after these additive APIs
and diagnostic layout changes. See [jobs safety/reuse](../libs/jobs/README.md).
