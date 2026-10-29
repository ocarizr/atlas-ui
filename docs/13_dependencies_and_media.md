# Dependencies and media

## Reproducible integration

`vcpkg.json` pins the public registry and tool checkout to a specific baseline. Run `scripts/bootstrap_dependencies.ps1` once, then configure CMake. Packages are installed automatically into each build tree; compiled package archives can be reused through the binary cache. No machine-specific paths or downloaded SDK sources belong in Git.

The manifest uses vcpkg's required JSON format, an exception for tool-owned configuration. Application content remains XML and application configuration remains YAML.

| Capability | Dependency / CMake target |
| --- | --- |
| ECS | EnTT / `EnTT::EnTT` |
| Logging and formatting | spdlog, transitive fmt / `spdlog::spdlog` |
| XML content | pugixml / `pugixml::pugixml` |
| YAML configuration | yaml-cpp / `yaml-cpp::yaml-cpp` |
| IPC and code generation | Protobuf and gRPC / `atlas::protocol` |
| C++ tests | GoogleTest / `GTest::gtest_main` |
| Shader compilation | DXC / `Microsoft::DirectXShaderCompiler` |
| Graphics | Windows SDK: Direct3D 12, DXGI, DXGUID |
| Text and images | Windows SDK: DirectWrite and WIC |
| UI sound effects | Windows SDK: XAudio2 2.9 and COM |
| Audio decode | libopus / `atlas::audio_dependencies` (`Opus::opus`) |
| Video decode | dav1d / `atlas::video_dependencies` |
| Video/audio container | libwebm / `atlas::video_dependencies` |

Domain-oriented interface targets expose dependencies without coupling the scheduler to graphics, serialization, ECS or IPC. Dependency tests exercise real library symbols; generating and round-tripping a session message verifies the Protobuf toolchain. No proprietary video SDK or fallback is retained.

Codec dependencies are platform-agnostic. Native graphics, DXC and audio-device dependencies are selected only by `libs/platform/windows/` when Windows integration is enabled. `atlas::audio_dependencies` links Opus without XAudio2/COM; the native SDK target is `atlas::windows_dependencies`. Portable dependency tests and Windows SDK tests are separate targets/CTest labels. Windows is the current supported backend, not the architecture of shared libraries.

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

Use **Opus/libopus** for compressed audio and XAudio2 2.9 for playback. [libopus](https://opus-codec.org/docs/opus_api-1.6/group__opus__decoder.html) decodes compressed packets to PCM; XAudio2 receives those prepared PCM buffers. The pinned public `opus` package is linked through `atlas::audio_dependencies`. WebM carries audio-only UI sounds or Opus tracks accompanying AV1; no additional file-opening decoder wrapper is introduced.

Button presses, hover/focus/selection feedback and timeline events use preloaded or prefetched Opus clips, reusable/bounded source voices, a master/UI gain group and explicit device/error handling. Decode jobs preserve packet order and exclusive access to each decoder state; independent streams can decode concurrently. Pre-skip, output gain, channel mapping and end padding need explicit playback contracts.

Behavior/timeline events issue audio commands at defined update points. The streamer supplies compressed bytes, demux/decode jobs prepare PCM and an audio-owner handoff submits ready buffers. No disk waits or expensive work belongs in frame jobs or real-time XAudio2 callbacks. Playback/device latency is separate from CPU completion. Video audio will share this Opus path; timestamps, clock ownership, seek/preroll and synchronization must be validated before combined playback. Spatial/gameplay audio and a general music system remain outside scope.

The current integration test decodes a streamed Opus packet on a CPU job. Complete container playback, voice pooling and UI-triggered sound remain milestones. All media receive buffers through [the streamer](15_streamer_and_media_jobs.md); file-opening helpers such as libwebm's `MkvReader` are forbidden in media consumers.

## Deployment

Packages contain the four bootstrap executables, their discovered non-system runtime DLLs, the design documents and dependency copyright notices. Windows system DLLs are not redistributed; a machine without the development toolchain needs the matching Visual C++ x64 runtime installed. Generated protocol files and downloaded packages remain build outputs. Debug/ASan deployment is for development, while release packaging uses the Release configuration.
