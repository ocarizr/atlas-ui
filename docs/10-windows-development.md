# Windows development

## Prerequisites

- Windows x64 and Visual Studio 2022 Desktop development with C++, Windows SDK and ASan component.
- CMake 3.25+, Ninja and LLVM tools: clang-cl, clang-format, clang-tidy, llvm-profdata and llvm-cov.
- Git, PowerShell 7 and Python 3.12 for CI tooling; install `scripts/ci/requirements.txt` in a local environment.

Use a Developer PowerShell x64 with CMake/Ninja/LLVM on PATH, especially for Ninja profiles. Keep the matching ASan runtime DLL discoverable when executing instrumented binaries outside the debugger.

## Build profiles

```powershell
. ./scripts/Initialize-Toolchain.ps1
./scripts/Bootstrap-Dependencies.ps1
cmake --list-presets=all
cmake --preset windows-msvc
cmake --build --preset windows-debug
cmake --build --preset windows-release
ctest --preset windows-debug
```

| Configure preset | Purpose |
| --- | --- |
| windows-msvc | MSVC Debug/Release and native debugging |
| windows-asan | AddressSanitizer, symbols, no /RTC, incremental linking or Edit and Continue |
| windows-analysis | MSVC /analyze |
| windows-clang | clang-cl with Ninja Multi-Config |
| windows-tidy | clang-tidy integrated with compilation |
| windows-coverage | LLVM source-based coverage |

Builds live under `build/<preset>`. Local overrides use untracked `CMakeUserPresets.json`. Owned targets call `atlas_configure_target(name)` and tests use `atlas_configure_test(name)`; each library also declares public requirements and dependencies. Do not impose project warnings on third parties.

The jobs, protocol, dependency and entry-point targets now have real CTest labels. CTest fails if a selected component has no tests. The applications remain bootstrap programs, not a rendered UI or running editor service. If upgrading a build directory created before vcpkg integration, configure once with `cmake --fresh --preset windows-msvc`. The first dependency build downloads/builds Debug and Release packages; later profiles reuse the binary cache.

## Style and analysis

Run Python tooling tests locally with `python -m pytest`. `pytest.ini` enables strict configuration/marker checking, importlib-based collection, warnings as errors, and automatic temporary-directory cleanup. Use `python -m pytest --junitxml=reports/pytest.xml` for the same XML report produced by CI.

```powershell
./scripts/Check-Format.ps1
./scripts/Check-Format.ps1 -Fix
clang-tidy --verify-config
cmake --preset windows-tidy
cmake --build --preset windows-tidy
cmake --preset windows-analysis
cmake --build --preset windows-analysis
```

Formatting uses Allman braces, four spaces and a 100-column limit. The script checks existing tracked/untracked C++ files, respects ignore rules, and excludes third-party/generated code. `-ClangFormat` accepts an explicit executable path. No C++ files means no formatting work, not a completed code-quality audit.

clang-tidy needs actual compiler includes/defines. Ninja exports compile commands. Selected diagnostics fail the check; suppressions need a concrete reason. Keep tool versions aligned between local use and CI.

MSVC analysis excludes imported external headers through `/analyze:external-`; owned code remains analyzed with warnings as errors. Test targets alone disable C6326 because GoogleTest assertion macros expand constant comparisons at their call sites. clang-tidy receives `/EHsc` explicitly so its Windows driver matches the compiler's exception mode.

## Sanitizers

MSVC [ASan](https://learn.microsoft.com/en-us/cpp/sanitizers/asan?view=msvc-170) is supported on Windows x86/x64. Its [limitations](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-known-issues?view=msvc-170) include incompatibility with /RTC, incremental linking and Edit and Continue. ASan does not detect data races.

The pinned vendor archives use their normal non-ASan ABI. The ASan profile disables MSVC vector/string container annotations consistently in owned targets to avoid annotation-ABI link mismatches with those archives. Atlas code remains memory-instrumented, but accesses within a container's allocated capacity may not receive container-overflow diagnostics, and vendor internals are not fully instrumented. A fully sanitized dependency build is a separate future profile.

```powershell
cmake --preset windows-asan
cmake --build --preset windows-asan
ctest --preset windows-asan
```

Native Windows is absent from the [LLVM ThreadSanitizer platform list](https://clang.llvm.org/docs/ThreadSanitizer.html). No TSan preset is offered; requesting it produces an explicit error. Linux/WSL is not a hidden requirement of this Windows-only project. Ownership rules, static analysis, dependency tests and stress help, but leave a real dynamic race-detection gap.

## Coverage

Use a clang-cl build separate from ASan. LLVM instrumentation produces per-process/module profiles; merge them with llvm-profdata and report lines/branches with llvm-cov. Follow the [LLVM coverage workflow](https://clang.llvm.org/docs/SourceBasedCodeCoverage.html).

```powershell
cmake --preset windows-coverage
cmake --build --preset windows-coverage
ctest --preset windows-coverage
```

The local test preset writes `build/windows-coverage/%m-%p.profraw`. Component CI uses a separate per-run directory, merges fresh profiles, includes the selected native test executables and retains a text report. The registered foundation targets use this path. Never publish stale results as current coverage.

## Validation scope

The native suites cover the scheduler, generated protocol, linked dependencies and application entry points. D3D12 integration uses a WARP device; the audio check initializes XAudio2 without claiming audible playback. The current application smoke path is headless. CI tooling has separate pytest regression tests for impact selection, release gating and asset validation. Neither bootstrap tests nor decoder initialization validate a complete UI or video player.

See [CI and delivery](12-ci-and-delivery.md) for selective builds, packages and release rules.
