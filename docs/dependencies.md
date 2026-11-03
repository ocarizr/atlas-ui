# Dependencies and media

## Reproducible integration

`vcpkg.json` pins the public registry and tool checkout to a specific baseline. For normal local builds, run `scripts/bootstrap_dependencies.ps1` once, then configure CMake; installed packages and binary archives are reused. Hosted CI instead restores version/ABI-keyed compiled products, partitioned by `.ci/dependencies.yaml`. Component consumers use their bundled CMake helpers with manifest installation disabled. See the [CI product contracts](ci_and_delivery.md). No machine-specific paths or downloaded SDK sources belong in Git.

The manifest uses vcpkg's required JSON format, an exception for tool-owned configuration. Application content remains XML and application configuration remains YAML.

| Capability | Dependency / CMake target |
| --- | --- |
| ECS | EnTT / `EnTT::EnTT` |
| Logging and formatting | spdlog, transitive fmt / `spdlog::spdlog` |
| CPU allocation | mimalloc / the `atlas::memory` PMR adapter |
| XML content | pugixml / `pugixml::pugixml` |
| YAML configuration | yaml-cpp / `yaml-cpp::yaml-cpp` |
| IPC and code generation | Protobuf and gRPC / `atlas::protocol` |
| C++ tests | GoogleTest / `GTest::gtest_main` |
| Shader compilation | DXC / `Microsoft::DirectXShaderCompiler` |
| Graphics | Windows SDK: Direct3D 12, DXGI, DXGUID |
| GPU allocation | D3D12 Memory Allocator / `GPUOpen::D3D12MemoryAllocator` |
| Text and images | Windows SDK: DirectWrite and WIC |
| UI sound effects | Windows SDK: XAudio2 2.9 and COM |
| Audio decode | libopus / `atlas::audio_dependencies` (`Opus::opus`) |
| Video decode | dav1d / `atlas::video_dependencies` |
| Video/audio container | libwebm / `atlas::video_dependencies` |

Domain-oriented interface targets expose dependencies without coupling the scheduler to graphics, serialization, ECS or IPC. Dependency tests exercise real library symbols; generating and round-tripping a session message verifies the Protobuf toolchain. No proprietary video SDK or fallback is retained.

Codec dependencies are platform-agnostic. Native graphics, DXC and audio-device dependencies are selected only by `libs/platform/windows/` when Windows integration is enabled. `atlas::audio_dependencies` links Opus without XAudio2/COM; the native SDK target is `atlas::windows_dependencies`. Portable dependency tests and Windows SDK tests are separate targets/CTest labels. Windows is the current supported backend, not the architecture of shared libraries.

The pinned libwebm port needs its nested header root exposed by the video wrapper. The gRPC client wrapper scopes a vendor-only MSVC deprecation diagnostic from gRPC 1.84's experimental certificate helpers; project call sites retain warnings-as-errors. Generated protocol sources are compiled separately from hand-written code-quality rules.

The maintained [Protobuf overlay](../dependencies/vcpkg-ports/README.md) fixes an exported empty-string ABI mismatch between the package's C++17 runtime and C++20 MSVC Release consumers. The patch uses consistent dynamic initialization in the library and installed headers; it does not modify generated project sources. Overlay changes invalidate the CI dependency cache.

## Media integration status

The public media stack is AV1/dav1d and Opus/libopus in WebM/libwebm.
Dependencies and real-symbol integration tests are available. A test decodes
streamer-provided Opus bytes on a CPU job. There is no WebM playback pipeline,
video presentation, audio voice manager or synchronized player.

The Windows SDK provides XAudio2 2.9 for decoded PCM integration; linking it does
not implement device playback. Codecs receive buffers through the streamer and
must not use file-opening helpers such as libwebm's `MkvReader`.
Decode state requires exclusive ordered access. Third-party decoder threads,
Atlas workers and real-time device callbacks have different ownership constraints.

Codec choice alone establishes neither visual quality nor a speed/memory guarantee.
Playback profiles, bounded queues, clock/seek rules and performance evidence are
tracked in [Media](https://github.com/ocarizr/atlas-ui/issues/33).
