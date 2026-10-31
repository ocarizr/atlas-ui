# Atlas UI

A retained-mode UI platform with a cross-platform C++20 architecture. Windows Desktop x64 is currently the only supported and validated platform. Atlas UI combines reusable runtime libraries, a visual editor, a dependency-driven job system, asynchronous asset streaming, and event-driven MVVM bindings.

The job system and asset streamer are implementation prerequisites. Validate generic phased job workloads and stream-backed resource loading before implementing production UI systems. The scheduler remains client-agnostic; integrations belong to its consumers. A manually authored UI follows, then the editor, using the same libraries and Direct3D 12 renderer.

## Technology

| Area | Decision |
| --- | --- |
| Architecture / current support | Platform-agnostic libraries; Windows Desktop x64 supported and validated |
| Language and build | C++20/CMake; Windows validation with MSVC 2022, clang-cl and Ninja |
| Current graphics backend | Direct3D 12 on Windows |
| ECS | EnTT |
| Video | AV1/WebM with dav1d and libwebm |
| Audio | Opus/libopus, decoded to PCM for XAudio2 2.9 (Windows SDK) |
| Serialized content | XML, including UI and ViewModel documents |
| Configuration | YAML |
| Interprocess communication | gRPC with Protocol Buffers |
| Behavior extensions | C++, with rebuild and process restart |
| Testing | GoogleTest for C++; fixtures, parameterized cases, isolated resources |

Tool-owned formats such as CMake presets and GitHub Actions matrix JSON follow their respective tool specifications. They do not change the application's serialization policy.

## Applications and libraries

- **atlas_game** hosts the UI runtime. A separate gameplay library may be linked in the future; gameplay is outside the current scope.
- **atlas_editor** uses Atlas UI for its own interface and manages an atlas_game process that produces the preview.
- **Shared libraries** provide UI, layout, MVVM, jobs, ECS integration, assets, streaming, rendering, and diagnostics.
- **Content tools** validate assets, cook content, generate construction functions, and create packages.

The editor owns the authoring document. C++ changes may stop the preview, trigger a rebuild, restart the process, and reconstruct the editing session. Debuggers can attach to the preview process.

## Development

The project follows Domain-Driven Design and Test-Driven Development. See [Contributing](CONTRIBUTING.md) and the [Windows development guide](docs/10_windows_development.md).

```powershell
. ./scripts/initialize_toolchain.ps1
./scripts/bootstrap_dependencies.ps1
cmake --preset windows-msvc
cmake --build --preset windows-debug
ctest --preset windows-debug
```

**Current status:** persistent workers with direct dispatch, injected child contexts, dependency handles, joined ranges and optional sealed DAGs, weighted ready priorities, cooperative cancellation, origin-thread completion callbacks, bounded asynchronous range reads from memory/loose assets, job-dispatched read completions, GoogleTest suites, pinned dependencies, generated session protocol and four executable entry points. A test decodes streamed Opus bytes on a CPU job. Apps remain headless bootstrap programs; UI windows, managed preview services, asset bundles and media playback are not implemented. Generic phased workloads now have sequential-reference and sparse-update tests; a separate benchmark records scheduling/grain overhead and worker traces. Consumer-specific frame budgets remain to be established. Start with the [direct job dispatch guide](docs/18_job_dispatch_usage.md).

## Component validation

Each row has independently named CI checks and uses compiled prerequisite artifacts.
The marks below describe configured checks, not live results; actual outcomes appear
on each PR/main workflow run. Formatting and asset validation run globally.

| Component | Debug/tests | Release/tests | ASan | MSVC analysis | clang-tidy | Coverage | SDK-free |
| --- | --- | --- | --- | --- | --- | --- | --- |
| [Jobs](libs/jobs/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Diagnostics](libs/diagnostics/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Jobs diagnostics](libs/jobs_diagnostics/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Streaming](libs/streaming/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Protocol / IPC](libs/ipc/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Application host](libs/application/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Game](apps/game/main.cpp) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Editor](apps/editor/main.cpp) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Content tools](tools/cook/main.cpp) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Dependency integration](tests/dependencies/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Windows SDK](tests/platform/windows/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | — |

Third-party products are built in five independent groups: testing, content/logging,
media, IPC and graphics. They include Debug and Release variants and are retained
for 14 days; vendor binaries themselves are not sanitizer-instrumented. The
[CI design](docs/12_ci_and_delivery.md) describes reuse, provenance, scheduling and
the distinction between internal build artifacts and deliverable packaging.

## Documentation

| Document | Topics |
| --- | --- |
| [Scope](docs/01_scope.md) | Goals, boundaries, supported features |
| [Architecture](docs/02_architecture.md) | Domain boundaries, libraries, applications, dependencies |
| [UI and rendering](docs/03_ui_and_rendering.md) | Widgets, layout, invalidation, graphics, animation, video, localization |
| [Job system](docs/04_job_system.md) | Persistent workers, DAGs, synchronization, frame boundaries |
| [MVVM and events](docs/05_mvvm_and_events.md) | Source of truth, bindings, commands, generated wiring |
| [Assets and pipelines](docs/06_assets_and_pipelines.md) | XML content, streaming, cooking, packaging |
| [Editor and extensions](docs/07_editor_and_extensions.md) | Two-process preview, gRPC, C++ extensions, debugging |
| [Quality and milestones](docs/08_quality_and_milestones.md) | TDD, fixtures, coverage, delivery sequence |
| [Open design questions](docs/09_open_design_questions.md) | Remaining contracts |
| [Windows development](docs/10_windows_development.md) | Build profiles, analysis, sanitizers, coverage |
| [Technical decisions](docs/11_technical_decisions.md) | Rationale and implementation constraints |
| [Continuous integration and delivery](docs/12_ci_and_delivery.md) | Impact selection, validation, packaging, release gates |
| [Dependencies and media](docs/13_dependencies_and_media.md) | Pinned packages, AV1/WebM and UI sound effects |
| [Job-system implementation plan](docs/14_job_system_implementation_plan.md) | Foundation, maturity milestones and the gate before UI |
| [Streamer and media jobs](docs/15_streamer_and_media_jobs.md) | Exclusive storage boundary, memory/loose sources, bundle plan and decode scheduling |
| [Portability and naming](docs/16_portability_and_naming.md) | Platform boundaries, snake_case, exceptions and validation |
| [Direct job dispatch](docs/18_job_dispatch_usage.md) | Injected clients, child lifetimes, ranges, sequencing and outcomes |
| [Job workloads and profiling](docs/19_job_workloads_and_profiling.md) | Phased proof, scratch memory, benchmark methodology and XML traces |
| [Job baseline measurements](docs/20_job_workload_baseline.md) | Reference hardware, observed speedups and small-workload overhead |
| [Job scheduling and cancellation](docs/21_job_scheduling_and_cancellation.md) | Priority clients, fairness bounds, cooperative stop and lifecycle |
| [Job completion callbacks](docs/22_job_completion_callbacks.md) | Outcome filters, caller pumping, worker affinity and joined listener scopes |
| [Scheduler optimization](docs/23_job_scheduler_optimization.md) | Completion storage, measured allocations/timings and candidate decisions |
| [Diagnostics and packages](docs/24_diagnostics_and_packages.md) | Logging, Chrome trace export, jobs integration and repository strategy |
| [Job safety and reuse](libs/jobs/README.md) | Declared-access sanitizer, limitations, standalone build and package |
| [Job graph usage](docs/17_job_graph_usage.md) | Generic examples, dependencies, errors, ownership and diagnostics |

The integration branch is `main`. Pull requests run relevant validation; pushes to `main` also package the current deliverables. Releases require a `vMAJOR.MINOR.PATCH` or `VMAJOR.MINOR.PATCH` tag pointing to a commit in `main` history.
