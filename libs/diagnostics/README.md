# Atlas Diagnostics

A reusable C++20 library for structured logging, bounded profiling and numeric metrics.
Public headers expose standard C++ types only. spdlog and yaml-cpp are private
implementation dependencies; no Atlas application, jobs, storage or Windows SDK
dependency is required.

## Metrics

Metrics are aggregated numeric series, separate from logs and profiler counter
events. Applications explicitly create an instance and record observations; the
library does not sample CPU, discover application events, collect user data, start
timers/threads, install a global registry or send anything automatically.

```cpp
#include <atlas/diagnostics/metrics.hpp>

atlas::diagnostics::metrics collection;
collection.increment("menu.visits", 1, {{"screen", "settings"}});
collection.set("cpu.percent", 12.5); // The caller supplies any sampled value.
auto snapshot = collection.snapshot();
```

`increment` adds a finite nonnegative double to a cumulative counter; `set` replaces
a gauge with any finite double, including negative values. A name has one kind per
instance, even across attributes. Both return false for invalid/rejected observations
without changing existing values. Counter overflow is rejected. Doubles do not
preserve every integer above 2^53; this is not an exact accounting ledger. Gauges
represent the last accepted update in lock order, not an average or distribution.
Concurrent arrival order can affect gauges and floating-point sums; these
observations are not deterministic application state or replay input.

A series is its case-sensitive UTF-8 name plus sorted key/value attributes.
Attribute order does not create duplicate series; duplicate keys are rejected.
Names/keys must be nonempty; values may be empty. ASCII control characters and
invalid UTF-8 are rejected. Values and labels are copied and owned. Choose stable
names that include units where useful, such as `cpu.percent` or `io.bytes`.

`metrics_options` bounds series (1024), attributes per series (8), and bytes per
name/key/value (256) by default. Zero attributes is allowed; series/text limits
must be positive. New series beyond capacity return false and increment `dropped`;
existing series remain writable. Invalid values/identities, type mismatches and
allocation failures increment `rejected`. `accepted` counts applied observations.
Statistics counters are observational and are not transactional export receipts.

Concurrent recording/snapshots are synchronized. Snapshots own a consistent copy
and remain usable after the collection is destroyed. Stop/join all callers before
destroying the collection. Points are
sorted by name/attributes, with cumulative values and system-clock start/capture
timestamps. Wall-clock adjustments can affect those timestamps; they are not latency
measurements or unique registry IDs. Repeated snapshots do not consume/reset data.
There is no eviction/reset or histogram/distribution instrument in this increment.
Avoid high-cardinality labels such as user IDs, request IDs or arbitrary asset paths.
Configured capacity bounds retained series/payload, not all allocator overhead,
caller-owned snapshots or temporary recording allocations. This is not a lock-free
or allocation-free real-time API; do not use it from a device callback.

### Reporting and telemetry adapters

Implement `metrics_output::publish(const metric_snapshot&)` to map snapshots into
an application telemetry transport, platform analytics SDK or test recorder:

```cpp
class memory_metrics_output final : public atlas::diagnostics::metrics_output
{
  public:
    void publish(const atlas::diagnostics::metric_snapshot& snapshot) override
    {
        saved = snapshot; // Copy if the adapter needs the data after this call.
    }
    atlas::diagnostics::metric_snapshot saved;
};

memory_metrics_output output;
collection.report(output); // Explicit, synchronous, on this calling thread.
```

`report` snapshots first, releases the collection lock, then calls the output once.
Output failure (including nonstandard exceptions) returns false, increments
`export_errors` and preserves the cumulative data. Successful return increments
`exported`; it means the adapter returned, not that a remote server persisted data.
There are no automatic retries, delta temporality, network connections or background
delivery. Disabled instances reject recording/reporting without invoking outputs.
Direct `snapshot()` may throw on allocation failure; `report()` contains such failures.
Adapters must interpret counters as cumulative, not add every snapshot as a new
increment. Delta translation, source identity and restart detection belong to the
adapter/application contract.

An output may record new observations; they belong to a later snapshot. Do not
recursively call `report` from an output. Concurrent reports require a thread-safe
output and application-defined ordering; the collection does not serialize transport
delivery. Async adapters must copy/queue owned snapshots, define bounded queues,
flush/shutdown/retry policies, and outlive their accepted deliveries. Perform blocking
transport work outside CPU-job workers. Storage writes stay behind the streamer.

Telemetry transport can be a separate library; analytics names, user/session identity,
consent and event-to-metric mappings belong to the application. A numeric cumulative
counter is not an analytics event stream. No Unity Analytics, Grafana/Prometheus or
other vendor integration is provided or configured here. Histograms, exporter formats
and platform adapters require their own contracts and validation when needed.

Version 0.2 adds metrics and extends `diagnostics_settings`. Rebuild matching clients
and use the matching jobs-diagnostics 0.2 package; do not mix compiled 0.1 interfaces.

## Logging

```cpp
#include <atlas/diagnostics/logging.hpp>

atlas::diagnostics::logging_options options;
options.category_levels.emplace_back("jobs", atlas::diagnostics::log_level::debug);
atlas::diagnostics::logger log{options};
log.write(atlas::diagnostics::log_level::info, "application", "Ready");
log.flush();
```

The default output is the console. Each logger owns its backend and, by default,
one persistent async thread; it does not use spdlog's global logger registry or
global thread pool. Share one logger at the process composition root.

Levels are trace/debug/info/warning/error/critical/off. Exact category overrides
replace the default info threshold. Outputs receive a structured `log_record`
(severity, category, message, timestamp, producer thread and formatted text).
Record views are borrowed for the call; copy them before forwarding to an editor
queue. `log_output::record()` defaults to the byte-oriented `append()` method.

The default queue holds 4,096 records; overflow evicts the oldest queued record.
`write()` returns true for a submitted request, not guaranteed persistence.
Statistics distinguish submitted, filtered, written, dropped and failed operations.
`enabled(level, category)` is a configuration-only query for avoiding expensive
message construction; queries do not increment write statistics. Calls into an
output are serialized within one logger. Sharing an output across logger instances
requires synchronization in that output.
Message bytes are limited to 16,384, category names to 256, and dynamically created
categories to 64 plus the reserved internal control logger. Invalid/oversized
requests and exhausted category capacity return false and increment errors.

`flush()` is an administrative barrier for earlier admitted messages, except those
already lost to overflow. Producers arriving during a flush return false and add
to dropped counts rather than waiting for backend I/O. The barrier bypasses the
overflow policy for its control message. `shutdown()` stops admission, flushes and
joins; it is idempotent. Stop producers before destroying their logger.

Output failures become error statistics instead of exceptions in job callbacks.
Output methods must finish, must not reenter any logger, and must not destroy their
logger. Reentrant writes/flushes are rejected; reentrant statistics throw.
Synchronous logging is available but performs output work on the producer: do not
select it for CPU jobs or real-time callbacks. Async logging still uses short
mutex-protected operations and allocation; it is not hard-real-time safe.

## Storage and rotation

Inject a shared `log_output` to select a destination. Diagnostics never accepts a
path or opens a file. Atlas-managed log/trace persistence belongs to the storage
boundary, just like other application-managed writes. An external application can
provide its own storage adapter without importing Atlas streaming.

Set `rotation_bytes` and `retained_files` only for an output implementing
`supports_rotation()` and `rotate(retained_files)`. The sink flushes and delegates
archive rotation before a new record would exceed its session byte threshold.
Records stay whole: one oversized formatted record can exceed that threshold.
Archive naming/retention and an initially empty active output belong to the adapter.
Console output rejects rotation. Physical file/archive storage is not implemented
by this package; rotation is exercised through injected storage in tests.

## Profiling

```cpp
#include <atlas/diagnostics/profiling.hpp>

atlas::diagnostics::profiler capture;
{
    atlas::diagnostics::profile_scope scope{capture, "update", "application"};
    // Work to measure.
}
capture.write_chrome_trace(storage_stream);
```

Scopes own their labels and borrow the profiler, which must outlive them. Manual
duration, instant and finite numeric counter events are also available. Capture
uses a steady-clock epoch and thread tracks. Complete events use Chrome `X`,
instants `i`, and counters `C`; timestamps/durations are microseconds with
nanosecond fractions. Chrome Trace Event JSON is a tool export format, independent
of XML application content and YAML configuration. Open it in Perfetto's trace UI.

Capture is thread-safe and bounded by event count (default 65,536) and captured
string bytes (default 8 MiB). Track count is bounded by event capacity. These are
logical capture limits, not process RSS or temporary export-copy budgets. Invalid
UTF-8, intervals or counters increment errors; exhausted capacity increments
dropped. Recording errors do not throw into the application. Check statistics.
Disabled captures do not reserve their event array.

Export copies a snapshot before writing to the injected stream and reports output
failure by exception. Export does not consume the capture. `clear()` resets records,
tracks and counters while retaining the epoch; quiesce scopes before clearing when
a strict session boundary is required. Capture is instrumented tracing, not sampled
CPU profiling, exact critical-path reconstruction or hardware utilization.

## YAML configuration

`parse_settings(bytes)` parses already loaded YAML and rejects unknown/duplicate
keys, invalid types/levels and invalid limits. It does not read a configuration file
or install global settings. Reconfiguration creates a new explicitly owned instance;
hot reload and settings precedence are separate application contracts.

```yaml
logging:
  level: info
  asynchronous: true
  queue_capacity: 4096
  max_categories: 64
  max_message_bytes: 16384
  rotation_bytes: 0
  retained_files: 3
  categories:
    jobs: debug
profiling:
  enabled: true
  max_events: 65536
  max_string_bytes: 8388608
metrics:
  enabled: true
  max_series: 1024
  max_attributes: 8
  max_text_bytes: 256
```

## Standalone package

```powershell
cmake -S libs/diagnostics -B build/diagnostics -G "Ninja Multi-Config" -DCMAKE_TOOLCHAIN_FILE=<vcpkg-toolchain>
cmake --build build/diagnostics --config Debug
cmake --install build/diagnostics --config Debug --prefix build/diagnostics-install
```

Consumers use `find_package(atlas_diagnostics 0.2.0 CONFIG REQUIRED)` and link
`atlas::diagnostics`. Provide spdlog 1.17+, yaml-cpp 0.9+ compatible packages and matching
compiler/CRT/configuration. Validation uses the repository's pinned versions.
Package compatibility follows the same minor version while the API is 0.x.
See the [integration and repository strategy](../../docs/24_diagnostics_and_packages.md).
