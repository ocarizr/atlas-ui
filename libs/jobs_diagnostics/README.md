# Atlas Jobs Diagnostics Adapter

An optional C++20 integration between independent `atlas::jobs` and
`atlas::diagnostics` packages. Jobs itself keeps its standard-library-only
dependency surface. Diagnostics has no scheduler dependency.

```cpp
#include <atlas/jobs_diagnostics/observer.hpp>

auto log = std::make_shared<atlas::diagnostics::logger>();
auto capture = std::make_shared<atlas::diagnostics::profiler>();
atlas::jobs::job_system_options options;
options.worker_count = 4;
options.observer = atlas::jobs_diagnostics::make_observer(log, capture);
atlas::jobs::job_system runtime{options};

runtime.dispatcher().run("update", [](atlas::jobs::job_context&) {}).wait();
runtime.wait_idle();
log->flush();
capture->write_chrome_trace(storage_stream);
```

Either destination may be null, but not both. The observer keeps destinations alive.
The game/editor composition root currently injects the logging destination; capture
is explicitly enabled by supplying a profiler. There is no global capture or file
output hidden in job execution.

Completed direct tasks generate an execution duration and a completion instant.
Job/parent IDs, outcome, priority, dependency wait, ready-queue wait, join and total
time accompany events. Suppressed bodies have zero-duration events in
`jobs.suppressed`; a running job that cooperatively cancels still has its real body
interval. Completion instants run on the finishing thread, while body intervals
use the original execution thread, including caller callbacks joined by workers.
Lifetime is metadata, not an overlapping CPU-execution span.

Failed tasks log at error, skipped tasks at warning, and successful/cancelled tasks
at debug. Failure logs include the original standard exception message or a
non-standard-exception marker. Observation/output failure does not rewrite job
results. This version observes direct dispatch, ranges, children and notifications;
legacy `submit`/sealed-graph callbacks retain their earlier instrumentation contract.

The underlying `job_observer::completed()` hook runs after terminal publication,
outside scheduler locks. Borrowed event views are valid only during that call.
Observers must be noexcept, finite, and must not wait on scheduler work, use saved
job contexts, or perform external I/O. `handle.wait()` observes the source result;
`wait_idle()`/`shutdown()` additionally drain accepted observer deliveries. A
nonreturning observer can prevent draining; arbitrary user code cannot be made
nonblocking by the scheduler.

Instrumentation is opt-in, allocates while capturing/logging, and changes timings.
Measure scheduler performance with and without it. Capture limits/drops and logging
errors must remain visible to diagnostics consumers. Live editor visualization,
full causal dependency flows, lock contention and real consumer frame budgets remain
future integration work.

Build/install `atlas_jobs` 0.2.x and `atlas_diagnostics` 0.2.x first, then configure
this directory with their install prefixes in `CMAKE_PREFIX_PATH`. Consumers use
`find_package(atlas_jobs_diagnostics 0.2.0 CONFIG REQUIRED)` and link
`atlas::jobs_diagnostics`. Jobs 0.2 adds observer options and changes compiled layouts:
rebuild all matching clients; do not mix 0.1 headers/binaries with 0.2.
