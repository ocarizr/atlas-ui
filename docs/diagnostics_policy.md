# Library diagnostics and profiling policy

Applications own diagnostics destinations and inject `diagnostics::services` into
the renderer device (third argument) and streamer (fifth argument). Both pointers
are optional; absent destinations do not create a logger thread, global capture,
file or transport. Retained shared destinations outlive accepted asynchronous work.
Never reenter a subsystem from its diagnostic output implementation.

```cpp
auto log = std::make_shared<atlas::diagnostics::logger>();
auto capture = std::make_shared<atlas::diagnostics::profiler>();
atlas::diagnostics::services diagnostics{log, capture};
atlas::renderer::device graphics{options, memory, diagnostics};
atlas::streaming::asset_streamer streamer{jobs, catalog, limits, memory, diagnostics};
```

Use the same capture for `jobs_diagnostics::make_observer(log, capture)` when
composing the application's one scheduler. Jobs remains independent of the
diagnostics implementation; the adapter preserves failure/skipped logs and job
results. Ordinary successful job logs remain at debug severity. No per-frame or
per-copy informational logging is added.

## Build and capture are separate decisions

Configure CMake with `-DATLAS_PROFILING=auto|on|off`:

| Policy | Debug probes | Release / RelWithDebInfo / MinSizeRel probes |
| --- | --- | --- |
| `auto` (default) | Compiled | Removed |
| `on` | Compiled | Compiled |
| `off` | Removed | Removed |

The exported diagnostics target propagates `ATLAS_PROFILING_ENABLED` to consumers.
Rebuild diagnostics and its affected consumers together when changing policy;
prebuilt products carry their producer's policy. Current CI component products use
the default `auto` policy. A change of policy requires compatible producer/consumer products; current CI does not expose configurable cross-policy product reuse. Do not override the propagated macro independently.

Compiled probes record only if a profiler is explicitly injected. They perform
no clock reads when the capture destination is absent. Disabled probes contain no
profiling clock reads or event submission, including in the jobs adapter. Logging
is independent of this policy; enabled log destinations can still report failures
in Release or with profiling off. The explicit `profiler` API remains available
for deliberate application-managed capture, and its own options can disable it.

The default apps inject their existing logger into renderer instances. They do
not automatically allocate/export traces. To collect, the composition root injects
a bounded profiler, drains work, then calls `write_chrome_trace(output)` on an
application-owned stream. Storage/transport remains with the caller; no library
opens a trace file. Inspect recorded/dropped/error statistics before interpreting
a bounded capture; instrumentation affects measured timings.

## Captured boundaries

- Renderer: device and surface initialization/release, frame submission, resize,
  explicit idle waits and readback. These scopes are **CPU wall intervals**, including
  coordinator waits and Present. Separate fenced GPU duration counters cover pass execution;
  they are not clock-calibrated GPU timeline slices. Exceptions mark scopes as failed;
  logger endpoints report operation labels without changing original exceptions.
- Streamer: initialization, admission, completion delivery, child copy bodies and
  draining. `read.lifetime` in `streaming.async` covers accepted request lifetime,
  including storage/queue wait and callback delivery; it is not CPU execution time.
  Source/cancellation/callback failures are logged outside the admission mutex.
  CPU scopes are attributed to the executing thread, while async lifetimes retain
  the submitting thread. Failed reads still deliver once and retire their budgets.
- Jobs adapter: executed/suppressed bodies and completion events retain IDs,
  parent links, outcomes and queue/dependency/join metadata. The flag gates this
  adapter's capture, not scheduler clocks required for its other public behavior.

Shutdown/export order matters: stop producers, drain streamer and GPU surfaces,
shut down the scheduler, then export the profiler and flush logs. A profile scope
borrows labels/destinations that must survive until scope exit; integrations use
static operation labels and retained service owners.

## Platform contract

`atlas::renderer` is the application-facing, SDK-free facade. Its private device
and surface adapters separate the native implementation. Native sources are added
only for `ATLAS_TARGET_PLATFORM=windows` with native integration enabled. A future
platform implements these adapters and selects its sources in CMake; applications
keep the same facade. The current unavailable backend explicitly rejects native
creation in SDK-free validation. Other target platforms still fail configuration
until implemented; they are not advertised as supported. Diagnostics injection
does not expose D3D12 or Win32 types in this contract.
