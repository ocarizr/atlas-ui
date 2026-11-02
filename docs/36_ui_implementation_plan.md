# Shared UI runtime and authoring plan

## Product and architecture

Atlas UI delivers reusable UI technology and tools, not a game-specific interface.
`atlas_game` is a standalone runtime host and integration harness. The editor uses
the same runtime to draw its own interface and progressively authors that interface.
Applications compose configuration, services, boot, loop and orderly shutdown;
runtime libraries do not depend on application/editor implementations.

The initial editor preview is planned as a separate runtime session **in the editor
process**. Authoring documents, editor UI and preview state remain independent.
IPC/gRPC remains an optional future isolated-preview adapter, not a required call
path for local UI. Existing IPC bootstrap dependencies are not removed in phase 1;
that migration accompanies host integration. Native C++ types remain extensible;
Lua is the planned behavior language for UI and future gameplay, replacing the
earlier requirement to implement all behavior in C++. Gameplay is out of scope.

One owner executes a session's Lua/event dispatch. Dispatch produces commands;
workers never enter that VM or mutate its live objects. CPU work uses the injected
application dispatcher and explicit dependencies, with a sequential small-work
path. Results join and publish in deterministic order. Sharing a language does not
require UI/gameplay or editor/preview to share interpreter state.

## Frame contract

The planned frame alternates serial dispatch, command preparation, joined work and
deterministic application. Structural tree changes occur only at safe points.
Layout/paint workers read stable inputs and have exclusive outputs. Required visual
work completes before the immutable render packet is published. Streaming may
continue across frames; pending content uses a placeholder.

`start`, `enable`, `process_input`, `tick`, `late_tick`, `disable` and completion/MVVM
notifications need explicit lifecycle and ordering contracts in their owning phases.
The last visual-mutation dispatch precedes final layout/paint. An `end_of_frame`
notification after publication cannot silently change that published frame.
Reentrant notification cycles need limits and diagnostics, not an unbounded drain.

Commands carry target generation, property, frame-wide sequence and producer ID.
Two writes to the same element/property in one frame are an actionable conflict,
including identical values and writes from one producer. Independent properties or
element generations do not conflict. Keep provenance and emit bounded warnings.
For absolute property assignments only, the last serially admitted write wins
within a batch; this fallback does not mean the developer has resolved the conflict.
Never silently merge actions, relative deltas, input occurrences or lifecycle events.

A sync point ends a batch: an earlier batch may already have been applied and cannot
be retroactively rewritten. Retain conflict history across batches in that frame;
beginning the next frame resets it. Later animation/binding layers must have explicit
ownership/composition rules rather than accidental competing property writes.

## Development phases

Each phase is a focused increment with its own tests and documentation. Dependencies
describe delivery order; optional capabilities must not block the first editor.

| Phase | Focus and deliverable | Exit evidence | Status |
| --- | --- | --- | --- |
| 1 | Command foundation: bounded serial admission, typed absolute writes, immutable batches, deterministic merging and conflicts with diagnostics | Success/failure/boundary, allocation rollback, ownership, retained batches and warning/profiling tests | Implemented |
| 2 | Element tree and properties: EnTT-backed identity, hierarchy, safe creation/removal, transactional application and invalidation flags | Stale-handle/cycle/removal tests, atomic rejection, unaffected subtrees and deterministic reference state | Planned |
| 3 | Frame execution: named phases, injected dispatcher, sync points, exclusive range outputs and sequential fallback | Sequential/parallel equivalence, failure joins, cancellation/cleanup and bounded admission | Planned |
| 4 | Layout core: measure/arrange, constraints, size box, overlay, horizontal/vertical containers | Table-driven geometry, dirty propagation, nesting, boundaries and aspect/extent changes | Planned |
| 5 | Paint core: ordered rectangles, clipping and immutable packets consumed by the existing render host | Reference pixels, painter order, empty clips, frame/resource retirement; generic standalone demonstration | Planned |
| 6 | Images: streamer-fed assets, placeholders, image sizing and texture ownership | Pending/success/failure/cancel paths and decoded/resident budgets without codec file access | Planned |
| 7 | Interaction and lifecycle: hit testing, capture, focus, routing and ordered system events | Input replay, enable/disable/start ordering, removal during dispatch and no worker-side callbacks into session state | Planned |
| 8 | MVVM core: observable ViewModels, actions, named bindings resolved to validated runtime slots | One source of truth, type/error diagnostics, explicit repeated-event/coalescing policy and bounded cycles | Planned |
| 9 | Lua behavior: session-owned interpreter, bindings, command production and asynchronous result delivery | VM affinity, protected errors, command rollback, teardown cancellation and deterministic result acceptance | Planned |
| 10 | XML documents: UI/ViewModel schemas, validation, load/save and stable identities through the streamer boundary | Round trips, useful source diagnostics, malformed/reference/type failures and equivalent constructed runtime state | Planned |
| 11 | Text foundation: shaping, fonts, glyph atlas and text layout for tool interfaces | Reference strings/scripts, fallback, wrapping, DPI and atlas/fence pressure tests | Planned |
| 12 | Editor bootstrap: hierarchy, inspector, selection, commands and undo/redo using Atlas UI; isolated in-process preview state | Author/edit/preview/save/reopen a generic UI without game behavior; runtime host free of editor implementation dependencies | Planned |
| 13 | Editor self-authoring: migrate editor panels into authored UI and add a contribution/registration path | Author a panel, register it, reopen it; invalid editor documents retain a minimal recovery interface | Planned |
| 14 | Animation playback: curves, tracks, time evaluation and explicit property composition | Deterministic seeking/looping, passive-time advancement, completion and binding-conflict behavior | Planned |
| 15 | Prepared content: validated intermediate representation and versioned binary construction; evaluate optional C++ code generation | XML/prepared equivalence, version/integrity failures and construction-cost measurements; bundle integration waits for its format | Planned |
| 16 | Initial UI readiness: representative editor/runtime workloads, diagnostics, budgets and prioritized optimizations | Measured distributions, failure/cleanup tests, sequential references and updated jobs/streaming/renderer backlog | Planned |

Grid/advanced layout, material/SDF features, UI audio, video textures, animation
authoring and world-space UI each require later focused phases. They are not bundled
into an oversized foundation phase. Audio/video playback reuses ready jobs and the
streamer; editor animation tooling is separate from animation playback. Future world
rendering/gameplay does not introduce dependencies into generic UI behavior.

## Phase 1 boundary and follow-up

Phase 1 introduces `atlas::ui` as an independent SDK-free C++20 library. It does not
yet create widgets, validate target liveness, apply commands, execute jobs, enter Lua,
read XML or render. Generation-bearing handles are opaque values until phase 2 owns
their registry. Producer IDs are stable session-assigned diagnostic identities, not
pointers, wall-clock values or worker completion order.

Serial dispatch stays lightweight: validated commands append to reserved storage.
The eventual session/frame host owns buffers, frame IDs and sealing; widget/script
authors request property changes/actions without constructing scheduler graphs.
Sealing sorts bounded frame history by key/sequence, collapses absolute writes in
the current batch, and reports one conflict per affected key in that batch. Published
batches own their data and allocator lifetime, so later collection/reset cannot alter
worker inputs. Limits bound frame admission and batches, not arbitrary consumer
retention across frames. A failed allocation leaves pending commands retryable.

Phase 2 binds command production/publication to its owning tree session and defines
atomic application, stale targets and invalidation. Slot/generation values alone do
not establish provenance across independent sessions. Phase 3 introduces the actual jobified frame
orchestration. No scheduler rewrite or second worker pool is needed for phase 1.

See [command usage](../libs/ui/README.md), [renderer readiness](34_renderer_readiness.md),
[renderer/streaming gaps](35_renderer_and_streaming_backlog.md) and
[job-system gaps](25_job_system_backlog.md).
