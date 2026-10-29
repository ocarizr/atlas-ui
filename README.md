# Atlas UI

A retained-mode UI platform for Windows, written in C++20. Atlas UI combines reusable runtime libraries, a visual editor, a dependency-driven job system, asynchronous asset streaming, and event-driven MVVM bindings.

The job system and asset streamer are implementation prerequisites. Validate job frame graphs and stream-backed resource loading before implementing production UI systems. A manually authored UI follows, then the editor, using the same libraries and Direct3D 12 renderer.

## Technology

| Area | Decision |
| --- | --- |
| Platform | Windows Desktop; initial build profiles target x64 |
| Language and build | C++20, CMake, Visual Studio 2022, Ninja |
| Graphics | Direct3D 12 only |
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

- **AtlasGame** hosts the UI runtime. A separate gameplay library may be linked in the future; gameplay is outside the current scope.
- **AtlasEditor** uses Atlas UI for its own interface and manages an AtlasGame process that produces the preview.
- **Shared libraries** provide UI, layout, MVVM, jobs, ECS integration, assets, streaming, rendering, and diagnostics.
- **Content tools** validate assets, cook content, generate construction functions, and create packages.

The editor owns the authoring document. C++ changes may stop the preview, trigger a rebuild, restart the process, and reconstruct the editing session. Debuggers can attach to the preview process.

## Development

The project follows Domain-Driven Design and Test-Driven Development. See [Contributing](CONTRIBUTING.md) and the [Windows development guide](docs/10-windows-development.md).

```powershell
. ./scripts/Initialize-Toolchain.ps1
./scripts/Bootstrap-Dependencies.ps1
cmake --preset windows-msvc
cmake --build --preset windows-debug
ctest --preset windows-debug
```

**Current status:** persistent-worker independent jobs, bounded asynchronous range reads from memory/loose assets, job-dispatched read completions, GoogleTest suites, pinned dependencies, generated session protocol and four executable entry points. A test decodes streamed Opus bytes on a CPU job. Apps remain headless bootstrap programs; UI windows, managed preview services, asset bundles and media playback are not implemented. Dependency DAGs and the synthetic frame-graph gate precede production UI.

## Documentation

| Document | Topics |
| --- | --- |
| [Scope](docs/01-scope.md) | Goals, boundaries, supported features |
| [Architecture](docs/02-architecture.md) | Domain boundaries, libraries, applications, dependencies |
| [UI and rendering](docs/03-ui-and-rendering.md) | Widgets, layout, invalidation, graphics, animation, video, localization |
| [Job system](docs/04-job-system.md) | Persistent workers, DAGs, synchronization, frame boundaries |
| [MVVM and events](docs/05-mvvm-and-events.md) | Source of truth, bindings, commands, generated wiring |
| [Assets and pipelines](docs/06-assets-and-pipelines.md) | XML content, streaming, cooking, packaging |
| [Editor and extensions](docs/07-editor-and-extensions.md) | Two-process preview, gRPC, C++ extensions, debugging |
| [Quality and milestones](docs/08-quality-and-milestones.md) | TDD, fixtures, coverage, delivery sequence |
| [Open design questions](docs/09-open-design-questions.md) | Remaining contracts |
| [Windows development](docs/10-windows-development.md) | Build profiles, analysis, sanitizers, coverage |
| [Technical decisions](docs/11-technical-decisions.md) | Rationale and implementation constraints |
| [Continuous integration and delivery](docs/12-ci-and-delivery.md) | Impact selection, validation, packaging, release gates |
| [Dependencies and media](docs/13-dependencies-and-media.md) | Pinned packages, AV1/WebM and UI sound effects |
| [Job-system implementation plan](docs/14-job-system-implementation-plan.md) | Foundation, maturity milestones and the gate before UI |
| [Streamer and media jobs](docs/15-streamer-and-media-jobs.md) | Exclusive storage boundary, memory/loose sources, bundle plan and decode scheduling |

The integration branch is `main`. Pull requests run relevant validation; pushes to `main` also package the current deliverables. Releases require a `vMAJOR.MINOR.PATCH` or `VMAJOR.MINOR.PATCH` tag pointing to a commit in `main` history.
