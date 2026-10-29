# Contributing

## Architecture

Atlas UI targets Windows Desktop and C++20. Use Direct3D 12 exclusively, EnTT for ECS infrastructure, XML for serialized application content, YAML for configuration, and gRPC/Protocol Buffers for IPC.

External libraries use the pinned vcpkg manifest. Video uses public AV1/WebM libraries (dav1d/libwebm), and UI sound effects use XAudio2 2.9 from the Windows SDK. Complete the job-system UI-readiness gate before implementing production UI systems.

Apply Domain-Driven Design through explicit vocabulary, invariants, and context boundaries. Shared systems are reusable CMake libraries with small public APIs and declared dependencies. Applications are composition roots; libraries must not depend on application or editor implementation details.

The editor and runtime share UI and rendering code. Keep the editor's authoring state separate from the preview process. Gameplay remains outside the current implementation scope.

## Test-Driven Development

For new or corrected production behavior, first write a test that fails for the intended reason. Implement the behavior, then refactor with the tests passing.

- Use GoogleTest `TEST`/`TEST_F` for individual facts and fixtures; use `TEST_P`, `ValuesIn`, and typed tests for variations.
- Reuse builders and parameter tables. Move large serialized scenarios and datasets into `Tests/<Context>/Data/` as XML application data.
- Generated test data must be reproducible, with explicit seeds. Do not introduce a data-generation framework without a concrete need.
- Use per-test `SetUp`/`TearDown` and RAII. Cleanup must run after failed assertions and early exits.
- Stop and join workers/processes before releasing their resources. Use isolated temporary directories and avoid mutable state shared across tests.
- Cover success, expected failure, boundaries, invariants, and cleanup. Assert that forbidden side effects did not occur.
- Concurrent tests use controlled signals/barriers and diagnostic timeouts, not arbitrary sleeps.
- Use pytest for Python tests: function-scoped fixtures, `tmp_path`, plain assertions, `pytest.raises` with specific diagnostics, and `pytest.mark.parametrize` with readable case IDs. Keep fixtures close to their consumers; use yield fixtures or finalizers when resources need explicit teardown. Avoid mutable state shared between cases and test-class setup inheritance.

## File organization

Split code by responsibility and tests by behavioral context, including successful and failing paths. Roughly 300 lines is a review signal, not a reason to fragment cohesive code mechanically. Avoid thousand-line files, repetitive test bodies, deep fixture inheritance, and macros that hide test intent.

## Style and checks

Follow `.editorconfig`, `.clang-format`, and `.clang-tidy`. Build outside the source tree. Apply project warnings and instrumentation to owned targets, not third-party libraries.

Run the relevant [Windows checks](docs/10-windows-development.md). Report actual results; no discovered tests must never be described as a passing suite. Coverage includes lines and branches but does not prove correctness or absence of races.

ASan is available on the Windows toolchain. Native LLVM ThreadSanitizer is unavailable; retain explicit ownership, static analysis, dependency tests, and stress coverage without presenting these as equivalent race detection.

## Branching and commits

Never work or commit directly on `main`. If no suitable working branch is active, create one before editing. Reuse the active working branch when continuing its task.

Branch names use `<type>/<domain>/<description>`, in lowercase kebab-case. The description contains one to four hyphen-separated words.

| Type | Purpose |
| --- | --- |
| documentation | Add or change documentation |
| feature | Implement new functionality or development capabilities |
| bugfix | Correct behavior that does not work as intended |
| improvement | Extend or improve existing behavior, coverage, or scenarios |

Use a concrete domain such as `job-system`, `renderer`, `streamer`, or `repository`. Examples: `feature/job-system/add-dependency-counters`, `bugfix/streamer/fix-cancelled-load-cleanup`, `improvement/renderer/cover-resize-failures`, and `documentation/repository/describe-build-profiles`.

After finishing and validating the work, review the diff, stage the intended files, and commit on the working branch. Every commit has a concise English subject and these body sections:

- **Purpose:** the problem or outcome and why it matters.
- **Changes:** the most relevant behavior/architecture changes, describing the previous and resulting state where useful; no file-by-file inventory is required.
- **Validation:** actual local commands/checks and results performed before the commit, including material limitations. A PR CI run is not local validation.

These sections must be reusable in a later PR description. Keep unrelated work out of the commit. Integration into `main` requires a pull request; never push commits directly to `main`.

## Pull requests and delivery

Use a focused branch and target `main`. Update documentation when contracts change. Update `.ci/components.yaml` when adding a library, application, build target, dependency, or test label. Shared-library changes must validate affected consumers.

PRs validate; they do not package or publish. The main-branch workflow packages. Publishing additionally requires a strict version tag and verification that its commit belongs to `main`. Use standard semantic versioning: major for incompatible changes, minor for compatible features, patch for compatible fixes.

Keep generated output, local SDKs, machine settings, and temporary work outside tracked source paths. Maintain `.gitignore` without hiding source assets or test fixtures.
