# Quality and milestones

## Diagnostics

Use spdlog for console/file output, rotation, asynchronous logging, categories, verbosity and scopes. The editor log window consumes shared logging infrastructure. YAML config is read at startup; reloadable settings and precedence need explicit contracts.

Profiling covers CPU/frame/job timings, memory, streaming, dependencies, frame/job timelines and asset loads, with immediate editor visualization. Proposed extra metrics include queue time, barrier waits, dirty-region counts and render-data cost. Measure instrumentation overhead separately.

## Testing discipline

Follow TDD and the [contribution rules](../CONTRIBUTING.md). C++ tests use GoogleTest, integrated with CMake/CTest. Use fixtures, `SetUp`/`TearDown`, RAII, data builders, parameterized/typed tests and external XML datasets where appropriate.

Reusable fixture infrastructure must preserve readable scenarios. Share expensive immutable suite resources only when safe. Use per-test cleanup even after failures; stop/join concurrent work before releasing referenced data. Split large suites by behavior/context rather than accumulating every test for a class in one file.

Facts/theories describe testing intent: use GoogleTest's native constructs rather than requiring .NET frameworks. Generated data uses explicit seeds. Python tests use pytest with function-scoped fixtures, isolated temporary directories, plain assertions, explicit exception checks, and individually reported parameterized cases.

| Level | Required areas |
| --- | --- |
| Unit | Layout, bindings, serialization, jobs, streaming, animation |
| Integration | Asset loading, packaging, editor pipelines, MVVM updates |
| End-to-end | Open UI, load data, play animation, validate output |

Cover valid inputs, expected failures, boundaries, cancellation, rollback and cleanup. Verify the absence of forbidden side effects. Concurrent tests need controlled synchronization and diagnostic timeouts, not sleeps.

## Coverage and validation

Measure lines and branches; examine uncovered failure paths. No minimum percentage has been approved. Coverage is evidence of exercised paths, not proof of meaningful assertions or race freedom. Explain exclusions.

Windows validation includes formatting, clang-tidy, MSVC analysis, Debug/Release builds, ASan, and concurrency stress/invariants. Native LLVM TSan is not supported; do not report substitute checks as dynamic race detection. Visual validation and graphics debug-layer checks follow renderer implementation.

## Milestones

| Stage | Observable result |
| --- | --- |
| Development foundation | Build/style policies, modular CI, documentation, package/release gates |
| Scheduler foundation | Independent jobs, worker lifecycle, failures, entry points and dependency integration |
| Streamer foundation | Exclusive storage boundary, memory/loose ranges, bounded requests and ready completion/decode jobs |
| UI-readiness gate | Dependency DAGs, ranges/children and a measured synthetic frame graph (plan milestones 1-3) |
| First UI runtime | AtlasGame window showing manually authored XML UI through the established job graph |
| Tested systems | EnTT integration, persistent workers, layout/invalidation, widgets and profiling |
| MVVM vertical slice | Gain XP/apply damage update presentation through actions/events/bindings |
| Measured parallelism | Incremental layout graph compared against sequential execution |
| Content pipeline | Validated XML, constructors, cooked content, packages and streaming |
| Editor bootstrap | Atlas UI editor, hierarchy/inspector and managed gRPC preview process |
| Extensibility | Create and integrate an editor contribution; rebuild/restart native behavior |
| Complete 1.0 scope | Expanded widgets, animation, localization, AV1/WebM video and UI sound effects, tools and end-to-end tests |

This is a dependency-oriented sequence, not a delivery schedule. The production UI must wait for the scheduler's documented UI-readiness gate; it does not require every later optimization or streaming feature.

## Success criteria and risks

Version 1.0 should load packaged UI, stream assets, support ECS/MVVM/localization/video/timelines/events, provide visual authoring, diagnostics, cooking/packaging, jobified processing, deterministic frames and measured parallel speedups.

Hardware, frame-time/FPS, memory, widget/list counts, load-time and speedup budgets remain open. Layout dependencies can limit parallelism; codegen affects build time and binary size; media decode, voice/device handling and deployment require integration tests; editor recovery and IPC need explicit ownership/versioning. Resolve these with measurements and contracts.
