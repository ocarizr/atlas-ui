# Atlas UI

A retained-mode UI platform for Windows, written in C++20. Atlas UI combines reusable runtime libraries, a visual editor, a dependency-driven job system, asynchronous asset streaming, and event-driven MVVM bindings.

The first milestone is a Windows application displaying a manually authored UI. The editor follows, using the same UI libraries and Direct3D 12 renderer.

## Technology

| Area | Decision |
| --- | --- |
| Platform | Windows Desktop; initial build profiles target x64 |
| Language and build | C++20, CMake, Visual Studio 2022, Ninja |
| Graphics | Direct3D 12 only |
| ECS | EnTT |
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
cmake --preset windows-msvc
cmake --build --preset windows-debug
ctest --preset windows-debug
```

**Current status:** development infrastructure and design documentation. Production libraries, applications, and GoogleTest suites have not been implemented yet. An empty C++ test suite is an error, not a passing result. The initial package contains the development documentation; it is not a runnable UI application.

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

The integration branch is `main`. Pull requests run relevant validation; pushes to `main` also package the current deliverables. Releases require a `vMAJOR.MINOR.PATCH` or `VMAJOR.MINOR.PATCH` tag pointing to a commit in `main` history.
