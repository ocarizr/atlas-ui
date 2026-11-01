# Diagnostics and independently consumable libraries

## Dependency boundaries

`atlas::diagnostics` owns logging, capture, configuration parsing and Chrome trace
serialization. Its implementation uses spdlog and yaml-cpp; its headers expose only
standard C++ types. `atlas::jobs` owns scheduling and a generic optional observer.
`atlas::jobs_diagnostics` adapts terminal observations into structured logs and trace
events. Applications compose these packages; neither foundational library depends
on the application or editor.

This keeps diagnostics independently buildable and jobs reusable without spdlog.
Core jobs remains usable with the declared-access sanitizer on or off. The observer
is optional and disabled by default. Its metadata reuses existing task ownership;
enabling it does not require bounded scheduler tracing to be enabled.

Workers never open log/trace files. The logger's bounded async backend owns one
persistent thread. Producers do not wait for output I/O during an administrative
flush: those attempts are rejected and counted. Synchronous logging is an explicit
caller choice and is unsuitable for CPU-worker or audio-device callbacks.

Diagnostics accepts injected output/storage objects and streams. Physical paths,
archive naming and persistence stay with the application's storage infrastructure.
The current streamer exposes reads only: file-backed diagnostics storage needs a
future write adapter under that boundary. Console logging, structured output hooks,
delegated archive rotation and stream-based trace export are implemented now.

Read the [logging/capture contract](../libs/diagnostics/README.md) and
[jobs adapter contract](../libs/jobs_diagnostics/README.md) for ownership, limits,
loss/error reporting and examples. Chrome JSON is a tool-owned format; XML content,
YAML configuration and Protobuf IPC remain the project's data conventions.

## Repository strategy

Repository topology and dependency version selection are separate decisions.
A separate repository can be consumed at a commit/tag or through a versioned
installed package. A monorepo can also publish several versioned packages, while
applications select earlier releases through a package registry/lockfile. Building
everything with `add_subdirectory` naturally selects the current checked-out source;
it does not provide delayed internal upgrades on its own.

For this stage, keep the monorepo and independently consumable library boundaries.
Atomic source/API changes and affected-consumer checks are useful while contracts
are evolving. Extract a repository when independent release cadence, external
consumers or ownership justify separate CI, issue tracking and coordinated upgrades.
Splitting Git repositories alone does not define compatibility or resolve conflicting
transitive dependency versions in one executable.

The implemented building blocks are standalone CMake projects, exported targets,
package configs and version checks:

| Package | Version | Public target | Dependencies |
| --- | --- | --- | --- |
| atlas_jobs | 0.2.0 | atlas::jobs | C++20, Threads |
| atlas_diagnostics | 0.1.0 | atlas::diagnostics | C++20; private spdlog/yaml-cpp/Threads |
| atlas_jobs_diagnostics | 0.1.0 | atlas::jobs_diagnostics | atlas_jobs 0.2.x, atlas_diagnostics 0.1.x |

The new packages and jobs use same-minor compatibility while their versions are
0.x. Consumers can request `EXACT` versions; a future source-fetch policy should
pin immutable commit IDs. Versions, compiler/CRT/configuration and ABI-compatible
vendor products all matter. Rebuild consumers after public job option layout changes.

The repository's normal source build and CI still validate one revision together.
Same-run owned artifacts intentionally require matching revision/run/profile;
this change does not weaken provenance or silently enable mixed-release builds.
A registry, publication cadence, dependency lock/update process, and tests against
supported package-version combinations are needed before delayed internal upgrades
become the default. No repositories are moved or created by this implementation.

## CI and validation

Diagnostics and the adapter have their own component targets, test labels and
profile checks. Diagnostics needs content/logging vendors and testing, without IPC
or graphics. The adapter imports diagnostics and jobs artifacts; application
consumers import the adapter transitively. Jobs itself still needs no content/IPC
vendor group. Installed export filenames use snake_case to avoid CMake's generated
`jobs-*.cmake` glob accidentally including another library's export file.

Validation covers severity/category filtering, structured messages, ownership,
queue overflow, output failures, reentrancy, rotation delegation, producer/shutdown
cleanup, flush progress, YAML rejection, capture bounds/invalid values, escaping,
concurrent recording and export failure. Jobs tests cover callbacks, joins,
original execution threads and observer draining outside pool locks. Standalone
install/consumer checks exercise all three packages together, not just in-tree aliases.

Measure instrumentation overhead separately. Event timelines are observations,
not deterministic replay input, sampled CPU utilization or proof of race freedom.
Native Windows TSan remains unavailable; retain ASan, static analysis and controlled
stress without treating them as equivalent dynamic race detection.

Reference formats and dependency mechanisms:
[Perfetto external trace formats](https://perfetto.dev/docs/getting-started/other-formats)
and [CMake dependency guide](https://cmake.org/cmake/help/latest/guide/using-dependencies/).
