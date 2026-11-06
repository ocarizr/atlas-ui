# Architecture

## Libraries and composition roots

Atlas uses explicit ownership and small CMake libraries. Applications select
services and own their lifetimes. Domain logic stays independent of native SDKs
and application implementations. DDD defines vocabulary and invariants; ECS and
data-oriented storage do not require an object-heavy domain model.

Product delivery targets viewport UI, its self-authored editor and standalone UI
applications. Shared services remain reusable within that boundary. See the
[product scope](scope.md); world/scene rendering and authoring are not delivery
requirements.

| Library | Current responsibility |
| --- | --- |
| `scripting` | Owner-thread Lua behavior, protected model demands and value-only native work transport |
| `ui_scripting` | UI lifecycle/input event adapters and validated response flags |
| `mvvm` | Independent typed ViewModel schemas/state and bounded observable actions/events |
| `mvvm_content` | Memory-only XML authoring and equivalent native factory generation |
| `ui_mvvm` | Compiled property/event bindings between UI and independent models |
| `jobs` | Persistent workers, dispatch/dependencies, joins, ranges, cancellation and callbacks |
| `memory` | Shared PMR resource policy and mimalloc/system adapters |
| `diagnostics` | Logging, metric snapshots and bounded trace collection/export |
| `jobs_diagnostics` | Optional scheduler observer forwarding logs and profiling |
| `streaming` | Catalog, exact-range reads, retained-byte/request budgets and storage adapters |
| `window` | Native window lifecycle, messages and synchronized presentation leases |
| `renderer` | Platform-neutral device, resources, passes, fences and presentation |
| `renderer_jobs` | Joined recording through an injected dispatcher |
| `renderer_content` | Streamer-fed image/HLSL preparation and fenced GPU ownership |
| `render_host` | Bounded immutable main/render frame handoff and lifecycle coordination |
| `ui_images` | Nonblocking image acquisition, immutable decoded leases and bounded GPU residency |
| `ui_renderer` | Ordered paint packets, clipping and adjacent graphics batching |
| `ui` | Element state/snapshots, immutable commands, joined phases and constrained measure/arrange |
| `ipc` / `protocol` | Session bootstrap and generated gRPC/Protobuf contracts |
| `application` | Common executable bootstrap and service composition |

`atlas_game` and `atlas_editor` currently share a generic rendering demonstration.
The editor has no authoring state or preview session implementation. Existing IPC
bootstrap linkage does not mean that isolated preview/frame transport exists.
`atlas_cook` and `atlas_package` expose placeholder tool entry points.

## Native boundary

Public headers expose neither Windows SDK types nor COM/D3D12 objects. Native
sources and vendor integration live under `libs/platform/windows/`; native tests
live under `tests/platform/windows/`. `ATLAS_TARGET_PLATFORM` selects the target,
and CMake selects only the implemented native sources. An SDK-free unavailable
backend validates portable contracts on Windows; it is not another supported OS.
See [portability and naming](portability_and_naming.md).

## Concurrency and ownership

Main owns window messages. An application-owned render coordinator owns device,
resources and submission. Workers execute ready CPU work; they do not wait on disk,
GPU fences or presentation. One injected pool serves all consumers. Immutable
snapshots cross thread boundaries through explicit synchronization and bounded
handoff. Graphics completion uses GPU fences, distinct from CPU job completion.

The live UI tree and its single bound command producer are owner-affine. EnTT stores
private node components; immutable batches carry session identities across publication
boundaries. The [layout engine](ui_layout.md) publishes immutable geometry after joined
measure/arrange work. Ordered painting and streamed images consume immutable values;
interactive widgets are not implemented. Library-owned
state cannot be mutated concurrently merely because an ECS library is available.

## Data and allocation

XML is the serialized-content policy, YAML is configuration, and Protobuf is IPC.
Tool-owned JSON/YAML formats follow their tools. The asset inventory has an actual
XML validator; ViewModel XML/factories are implemented in `mvvm_content`; UI document schemas and cooked bundle layouts are not implemented.
Runtime consumers request logical asset IDs and byte ranges through streaming.

Composition roots inject a retained PMR resource into allocating consumers.
CPU PMR, GPU D3D12MA allocation and third-party/driver heaps are separate domains.
See [memory](../libs/memory/README.md) and [assets](assets.md).

See [ViewModel contracts](mvvm.md) for data-authored fields, observable state,
independent native factories and explicit UI adapter boundaries.
