# Dependencies and media

## Reproducible integration

`vcpkg.json` pins the public registry and tool checkout to a specific baseline. Run `scripts/Bootstrap-Dependencies.ps1` once, then configure CMake. Packages are installed automatically into each build tree; compiled package archives can be reused through the binary cache. No machine-specific paths or downloaded SDK sources belong in Git.

The manifest uses vcpkg's required JSON format, an exception for tool-owned configuration. Application content remains XML and application configuration remains YAML.

| Capability | Dependency / CMake target |
| --- | --- |
| ECS | EnTT / `EnTT::EnTT` |
| Logging and formatting | spdlog, transitive fmt / `spdlog::spdlog` |
| XML content | pugixml / `pugixml::pugixml` |
| YAML configuration | yaml-cpp / `yaml-cpp::yaml-cpp` |
| IPC and code generation | Protobuf and gRPC / `Atlas::Protocol` |
| C++ tests | GoogleTest / `GTest::gtest_main` |
| Shader compilation | DXC / `Microsoft::DirectXShaderCompiler` |
| Graphics | Windows SDK: Direct3D 12, DXGI, DXGUID |
| Text and images | Windows SDK: DirectWrite and WIC |
| UI sound effects | Windows SDK: XAudio2 2.9 and COM |
| Video decode | dav1d / `Atlas::VideoDependencies` |
| Video container | libwebm / `Atlas::VideoDependencies` |

Domain-oriented interface targets expose dependencies without coupling the scheduler to graphics, serialization, ECS or IPC. Dependency tests exercise real library symbols; generating and round-tripping a session message verifies the Protobuf toolchain. No proprietary video SDK or fallback is retained.

The pinned libwebm port needs its nested header root exposed by the video wrapper. The gRPC client wrapper scopes a vendor-only MSVC deprecation diagnostic from gRPC 1.84's experimental certificate helpers; project call sites retain warnings-as-errors. Generated protocol sources are compiled separately from hand-written code-quality rules.

The maintained [Protobuf overlay](../dependencies/vcpkg-ports/README.md) fixes an exported empty-string ABI mismatch between the package's C++17 runtime and C++20 MSVC Release consumers. The patch uses consistent dynamic initialization in the library and installed headers; it does not modify generated project sources. Overlay changes invalidate the CI dependency cache.

## Public video stack

Use AV1 video in WebM, decoded by **dav1d** and demuxed by **libwebm**. [dav1d](https://github.com/videolan/dav1d) is a public decoder focused on speed and correctness, including optimized assembly and high-bit-depth support. Its [license](https://github.com/videolan/dav1d/blob/master/COPYING) is permissive; the pinned port also includes notices for optimized assembly. [libwebm](https://github.com/webmproject/libwebm) provides public container parsing under its [BSD license](https://github.com/webmproject/libwebm/blob/main/LICENSE.TXT).

This replaces the original proprietary-codec requirement. It is not a claim that all AV1 assets decode faster or consume less memory than every alternative. Quality depends on encoding, bitrate, chroma format, bit depth and renderer conversion; speed depends on resolution, frame rate, keyframe structure, CPU and thread budget.

VP9/libvpx was considered as a mature public alternative. AV1/dav1d is selected for the combination of public source, optimized decoding and high-quality/high-bit-depth content. Keep one initial media stack; do not add an unrestricted multi-codec framework to the runtime.

## Video implementation contract

The present delivery links and opens the decoder; it does not implement a player. Before playback ships:

- Define an encoding profile with dimension/frame-rate limits, bit depth/chroma policy and a bounded GOP/keyframe interval.
- Use container cues/indexes for seek, reset decoder state and decode forward from a suitable keyframe. Scrubbing arbitrary predicted frames is not constant-time random access.
- Bound compressed and decoded queues and RAM/VRAM budgets. Drop obsolete frames after seek and publish completed textures at a controlled frame boundary.
- Start with one decoder thread when scheduling decode work through Atlas jobs. Profile explicit decoder parallelism before enabling another internal pool per video.
- Perform color conversion/upload through the renderer; define range, color space, transfer and alpha requirements before claiming support.
- Benchmark representative clips for CPU time, memory, startup latency, loop continuity, seek/scrub latency and visual quality. No codec-performance budget has been claimed yet.

Hardware AV1 decoding can be investigated later behind the video contract; the initial path does not depend on Store codec extensions or a proprietary SDK. Offline encoding is separate from decoding and will be designed with the cooking pipeline.

## UI sound effects

Audio is now in scope for button presses, hover/focus/selection feedback and timeline events. Use XAudio2 2.9 from the Windows SDK. Short clips should start as preloaded PCM WAVE assets with reusable/bounded source voices, a master/UI gain group and explicit device/error handling.

Behavior/timeline events issue audio commands at defined update points. Do not decode large clips or wait for disk/device work in a UI-frame job. Playback/device latency is separate from completion of the frame's CPU work. Spatial audio, music streaming and video-audio synchronization are not part of the initial UI-SFX contract.

The audio dependency is linked in this delivery; voice pooling, asset loading and actual UI-triggered playback remain implementation milestones. This does not introduce gameplay audio or a general audio engine.

## Deployment

Packages contain the four bootstrap executables, their discovered non-system runtime DLLs, the design documents and dependency copyright notices. Windows system DLLs are not redistributed; a machine without the development toolchain needs the matching Visual C++ x64 runtime installed. Generated protocol files and downloaded packages remain build outputs. Debug/ASan deployment is for development, while release packaging uses the Release configuration.
