# Technical decisions

## Direct3D 12 only

Direct3D 12 is the only supported graphics API. Do not add Direct3D 11 or Vulkan backends or a multi-backend abstraction without an explicit architecture change.

The [D3D12 command-list/queue model](https://learn.microsoft.com/en-us/windows/win32/direct3d12/command-queues-and-command-lists) supports explicit work submission and multi-threaded command recording. Atlas still owns resource states, allocator lifetimes, descriptors, fences and upload/publication rules. The API does not automatically make application data races safe or guarantee faster frames.

Keep UI render-data preparation separate from native graphics-resource ownership. This supports modularity and testing without committing to multiple graphics APIs.

## EnTT

Use [EnTT](https://github.com/skypjack/entt) instead of developing a general ECS framework. It provides ECS infrastructure compatible with the C++20 project. The package version is fixed by the vcpkg baseline; do not consume a floating branch.

Atlas remains responsible for UI hierarchy, scheduling, mutation boundaries and access ownership. EnTT does not replace the custom job system, MVVM or streaming policy.

## XML, YAML and Protocol Buffers

XML is the format for authored/serialized application data, including UI and ViewModels. YAML is reserved for configuration. Protobuf is the IPC message schema and encoding, used through gRPC. Prepared binary assets and compiled generated constructors remain runtime outputs of the content pipeline.

Respect tool-owned formats: CMake presets, dependency lock files and GitHub matrix outputs may require JSON. They are not an alternative application-data authoring format.

## gRPC preview control

Use gRPC between atlas_editor and the managed atlas_game process. Define typed services, version/session/revision contracts, deadlines and recovery. Image transport is not chosen yet; separate control-plane RPCs from a future GPU-sharing/frame-delivery decision.

## Persistent workers

| Concept | Meaning |
| --- | --- |
| Thread pool | Persistent OS threads execute queued jobs |
| Work stealing | Idle workers take eligible work from another queue |
| Fiber | Stackful cooperative execution context |
| C++20 coroutine | Suspend/resume function machinery requiring an executor; not a thread |

Start with ready-job queues and dependency counters. Fibers/coroutines and work stealing are optional refinements based on measurements. Pending prerequisites must not occupy workers.

## Measured performance

Compare wall time, total CPU cost, allocations and critical paths. Parallel scheduling can add total work while shortening an activity, which is acceptable when it improves the intended frame budget.

Generated construction can remove parsing and name resolution from execution, but may increase build time and binary size. Keep large media in prepared content and validate codegen benefits in a vertical slice.
