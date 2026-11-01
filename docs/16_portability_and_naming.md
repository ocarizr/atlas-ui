# Portability and naming

Root configuration now exposes `ATLAS_TARGET_PLATFORM` with `windows` as the only
implemented target. Other enumerated desktop/mobile/console values are reserved and
fail explicitly. Native streamer sources are selected behind a shared private file
interface; the Windows SDK remains under `libs/platform/windows/streaming/` and the
SDK-free profile selects a standard reference backend. See the
[streaming contract](../libs/streaming/README.md). This does not establish another
platform's support or make Xbox a Windows Desktop alias.

## Architecture versus platform support

Atlas UI is designed as a cross-platform C++20 project. **Windows Desktop x64 is currently the only supported and validated target.** Direct3D 12 and XAudio2 are the current Windows choices; this does not make shared UI, jobs, streaming or content contracts Windows APIs. No Linux/macOS renderer, release or CI coverage is claimed.

Portable libraries live under `libs/<context>/`, with public headers in `include/atlas/<context>/` and implementation in `src/`. Applications in `apps/` and content tools in `tools/` are composition roots. Protocol sources live in `protocols/`; tests mirror contexts under `tests/`.

## Naming contract

| Area | Convention / example |
| --- | --- |
| Owned directories and files | snake_case: `libs/jobs/`, `job_system.hpp`, `asset_streamer.cpp`, `initialize_toolchain.ps1` |
| C++ namespaces, classes, structs, enums and aliases | snake_case: `atlas::jobs::job_system`, `streamer_limits`, `asset_bytes` |
| C++ functions, variables and enum values | snake_case: `wait_idle`, `worker_count`, `application_kind::game` |
| Private data members | snake_case with trailing underscore where class-private: `impl_`, `completion_` |
| GoogleTest suites, fixtures and cases | snake_case; keep standard `TEST`, `TEST_F`, `TEST_P` macros |
| CMake targets and aliases | `atlas_jobs` and `atlas::jobs`; functions/local variables use snake_case |
| CMake cache options and constants | Tool conventions: `ATLAS_ENABLE_ASAN`, `CMAKE_CXX_STANDARD` |
| Python | PEP 8: snake_case functions/modules, CapWords classes, uppercase constants |
| PowerShell | snake_case filenames and task-local variables; native parameter/cmdlet conventions remain valid |

There is no single naming standard imposed by portability. This is the project's explicit convention, applied consistently instead of mixing styles by IDE or operating system.

Required tool filenames retain their spelling: `CMakeLists.txt`, `CMakePresets.json`, `CMakeUserPresets.json`, `README.md`, `CONTRIBUTING.md`, `LICENSE`, `CODEOWNERS`, `vcpkg-configuration.json` and dotfiles. Preserve external SDK/library identifiers, required overrides such as GoogleTest `TearDown`, generated code, Protobuf schema conventions and serialized-format compatibility. Third-party port snapshots/patches under `dependencies/` retain upstream names. Human labels, branding, semantic-version artifact qualifiers, build-preset names and Git branch conventions are not C++ identifiers.

Use lowercase acronym words in owned APIs (`ipc`, `ecs`, `ui`). Includes use the exact case and spelling of the target path, with forward slashes. Do not rename external headers or schema fields to satisfy a source-style rule.

## Platform boundary

- Public headers and portable implementation must not include native SDK headers or expose native handles/types. Use domain values/contracts and hide implementation details; do not invent empty abstractions for systems that do not exist yet.
- Native Windows implementations/dependency targets belong in `libs/platform/windows/`; SDK integration tests belong in `tests/platform/windows/`. Future native backends follow the same separation.
- Generic `atlas::audio_dependencies` supplies Opus. `atlas::windows_dependencies` supplies D3D12, DXGI, DirectWrite, WIC, DXC, XAudio2 and COM. Portable jobs/streaming/application targets do not link the latter.
- The current production libraries use standard C++/shared dependencies. The Windows target currently groups SDK integration; full renderer/audio backend interfaces are still future work.
- The streamer's standard-library loose-file reader is portable storage infrastructure. Future overlapped Windows I/O belongs in a native storage adapter implementing the streamer's contract; codecs still never open files independently.
- Compiler flags follow the selected compiler. MSVC sanitizer/runtime policy is isolated in `cmake/platform/msvc.cmake`. Windows triplets belong to Windows presets; DXC is a Windows-conditional manifest dependency. Windows packaging remains explicitly selected for Windows.

`ATLAS_BUILD_WINDOWS_INTEGRATION` defaults to the Windows target condition and can be disabled. The `windows-portable` preset builds the current portable foundation on the validated host with SDK integration omitted. It demonstrates dependency separation; it is not a test of another operating system.

```powershell
. ./scripts/initialize_toolchain.ps1
cmake --preset windows-portable
cmake --build build/windows-portable --config Debug
ctest --test-dir build/windows-portable -C Debug --output-on-failure --no-tests=error
```

## Enforcement and extension

clang-tidy checks owned C++ naming. Pytest/repository quality checks owned source-path naming and native-header containment; tool/upstream exceptions are explicit. CI's component map separately registers portable dependencies and Windows integration. The temporary-asset fixture uses standard C++ instead of Win32; random temporary names only ensure isolation and do not change reproducible test data.

Adding a platform requires native adapters, dependency/triplet selection, platform tests, packaging policy and CI validation. Keep the portable domain tests reusable and extend supported-platform claims only after the new platform is actually validated.
