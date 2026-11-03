# Atlas UI

Reusable C++20 foundations for UI runtime and authoring tools. The architecture
separates portable contracts from native adapters; Windows Desktop x64 with
Direct3D 12 is the only supported and validated platform.

## Current capabilities

Atlas includes a persistent job system, bounded asynchronous memory/loose-asset
streaming, explicit shared PMR allocation, injected logging/metrics/profiling,
native windows and generic graphics resources/passes. Optional worker recording
uses the application's existing pool. A bounded render host separates window
messages from graphics coordination.

The game and editor hosts display the same streamed textured quad. The independent
UI library produces immutable command batches with deterministic merging and
conflict diagnostics. Widget trees, layout, MVVM, Lua execution, visual authoring,
asset cooking/bundles and media playback are not implemented. Codec/ECS/IPC
dependencies and bootstrap contracts are available; content-tool entry points are
placeholders. Gameplay belongs outside this reusable-technology repository.

| Area | Integration |
| --- | --- |
| Build | C++20/CMake, MSVC 2022 or clang-cl, Ninja |
| Graphics | Direct3D 12, DXC, D3D12 Memory Allocator |
| CPU allocation | Application-selected shared PMR, mimalloc or system backend |
| Data policy | XML content, YAML configuration, Protobuf/gRPC IPC |
| Dependencies | Pinned vcpkg baseline; EnTT, spdlog, pugixml, yaml-cpp |
| Media dependencies | AV1/dav1d, Opus/libopus, WebM/libwebm; Windows XAudio2 2.9 |
| Tests | GoogleTest/CTest and pytest |

## Development

```powershell
. ./scripts/initialize_toolchain.ps1
./scripts/bootstrap_dependencies.ps1
cmake --preset windows-msvc
cmake --build --preset windows-debug
ctest --preset windows-debug
```

See [Windows development](docs/windows_development.md) and
[Contributing](CONTRIBUTING.md). Runtime contracts and supported limitations are
documented in this repository. Delivery planning, acceptance criteria and
dependencies live in [Issues](https://github.com/ocarizr/atlas-ui/issues) grouped by
[Milestones](https://github.com/ocarizr/atlas-ui/milestones); no GitHub Project is required.

## Component validation

Each row has independently named CI checks and uses compiled prerequisite artifacts.
The marks below describe configured checks, not live results; actual outcomes appear
on each PR/main workflow run. Formatting and asset validation run globally.

| Component | Debug/tests | Release/tests | ASan | MSVC analysis | clang-tidy | Coverage | SDK-free |
| --- | --- | --- | --- | --- | --- | --- | --- |
| [UI](libs/ui/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Command contracts |
| [Window](libs/window/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Contracts only |
| [Renderer](libs/renderer/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Contracts only |
| [Render host](libs/render_host/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Handoff contracts |
| [Renderer readiness](tests/renderer_readiness/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Explicit unavailability |
| [Renderer jobs](libs/renderer_jobs/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Executor contracts |
| [Renderer content](libs/renderer_content/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Injected decode contracts |
| [Jobs](libs/jobs/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Memory](libs/memory/CMakeLists.txt) | Yes | Yes | System backend | Yes | Yes | Yes | Yes |
| [Memory consumers](tests/memory_integration/CMakeLists.txt) | Yes | Yes | System backend | Yes | Yes | Yes | Yes |
| [Diagnostics](libs/diagnostics/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Jobs diagnostics](libs/jobs_diagnostics/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Streaming](libs/streaming/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Protocol / IPC](libs/ipc/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Application host](libs/application/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Game](apps/game/main.cpp) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Editor](apps/editor/main.cpp) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Content tools](tools/cook/main.cpp) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Dependency integration](tests/dependencies/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| [Windows SDK](tests/platform/windows/CMakeLists.txt) | Yes | Yes | Yes | Yes | Yes | Yes | â€” |

Third-party products are built in six independent groups: testing, memory, content/logging,
media, IPC and graphics. They include Debug and Release variants and are retained
for 14 days; vendor binaries themselves are not sanitizer-instrumented. The
[CI design](docs/ci_and_delivery.md) describes reuse, provenance, scheduling and
the distinction between internal build artifacts and deliverable packaging.

## Documentation

The generated [API wiki](https://github.com/ocarizr/atlas-ui/wiki) provides library
and symbol references. PRs validate Doxygen generation; only `main` publishes wiki
updates. Edit source comments rather than generated wiki pages.

| Topic | Guides |
| --- | --- |
| Product | [Scope](docs/scope.md), [architecture](docs/architecture.md), [application hosts](docs/application_hosts.md) |
| UI | [Commands](docs/ui_commands.md), [public API and examples](libs/ui/README.md) |
| Jobs | [Overview](docs/jobs.md), [dispatch](docs/job_dispatch.md), [graphs](docs/job_graphs.md), [scheduling](docs/job_scheduling.md), [callbacks](docs/job_callbacks.md), [measurement](docs/job_workloads.md), [safety](libs/jobs/README.md) |
| Memory and storage | [PMR resources](libs/memory/README.md), [assets](docs/assets.md), [streaming](docs/streaming.md), [streaming API](libs/streaming/README.md) |
| Rendering | [Contracts](docs/renderer.md), [resources](docs/renderer_resources.md), [passes](docs/renderer_passes.md), [recording](docs/renderer_recording.md), [content](docs/renderer_content.md), [render host/readiness](docs/render_host.md) |
| Observability | [Diagnostics and packages](docs/diagnostics.md), [build/capture policy](docs/diagnostics_policy.md), [metrics](libs/diagnostics/README.md#metrics) |
| Development | [Testing](docs/testing.md), [portability/naming](docs/portability_and_naming.md), [source style](docs/source_style.md), [dependencies](docs/dependencies.md), [CI/delivery](docs/ci_and_delivery.md), [API generation](docs/api_reference.md) |

Integration uses `main` through pull requests. Main packages current executable
deliverables; publication additionally requires a strict `vMAJOR.MINOR.PATCH` or
`VMAJOR.MINOR.PATCH` tag whose commit belongs to `main` history.
