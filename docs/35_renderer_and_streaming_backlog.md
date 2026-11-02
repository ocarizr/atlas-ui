# Renderer and streaming follow-up work

## Assessment after renderer phase six

The initial graphics foundation supports a bounded, controlled Windows 2D UI slice.
Correct ownership and working rendering do not establish production frame budgets.
Keep UI layout, widget behavior, assets and scheduling policy in their owning
contexts. Do not replace generic job/streamer contracts with UI-specific behavior.

The renderer already batches **CPU recording into ordered command lists**, supports
coarse parallel recording and reuses fenced contexts. It also reuses GPU allocation
heaps through D3D12MA and successful pipelines through a device-local cache.
It does **not** currently merge draw packets into geometry/instance batches. Native
recording issues one `DrawInstanced(..., 1, ...)` per packet and repeats pipeline,
root/constants, viewport/scissor and vertex bindings. Distinguish these mechanisms
before calling the entire implementation unbatched or allocation-free.

Priorities: P1 is needed before the corresponding production workload; P2 improves
capability/observability; measured candidates require a demonstrated bottleneck.
None of the measured optimizations below blocks the first small manual UI tree.

## Renderer candidates

| ID | Priority | Gap / current evidence | Completion evidence |
| --- | --- | --- | --- |
| RND-01 | Measured | Adjacent draw coalescing, indexed/instanced geometry; one draw per current packet | Pixel equivalence, fewer draws and improved CPU/GPU cost on representative UI; preserve painter order, alpha/depth, clipping and resource boundaries |
| RND-02 | Measured | Redundant state binds inside `pass_execution.cpp` | Per-context state cache invalidation tests, especially root-signature changes, with measured reduction in binding cost |
| RND-03 | Measured | Full frame resolution and nested description copies; transient normal-heap allocations remain | Allocation profiles, dirty/sparse-update workloads and identical validation/failure atomicity before adopting arenas or cached resolution |
| RND-04 | P1 for many assets | Per-upload staging allocations and command submissions despite reused transfer contexts | Bounded upload ring/batching, wrap/fence/cancel/overflow tests and measured small-asset throughput/latency |
| RND-05 | P2 | Device-loss policy is fail-stop; no automatic graphics-domain reconstruction or DRED breadcrumb report | Explicit reconstruction ownership, retained asset/authoring state, failure propagation and device-removal tests before promising recovery |
| RND-06 | P1 for richer content | Fixed single-mip RGBA8/D32 and b0/t0/s0 ABI; no complete color/mip/font-material pipeline | Format/color/alpha reference images, mip/atlas lifetime tests and compatible shader ABI changes |
| RND-07 | P2 | Process-local PSO cache; trusted in-memory HLSL only, no persistent bytecode/cache schema | Versioned deterministic cache/cooker contract and invalidation by compiler, adapter/driver where required, source and pipeline state |
| RND-08 | Measured | Surface presentation copies an offscreen target; sequential flip mode supports diagnostic readback | Measure direct surface targets or flip-discard with capture-on-request while preserving resize/capture/retirement contracts |
| RND-09 | P2 | GPU duration counters exist; no calibrated GPU timeline, per-pass query scopes or complete causal CPU/jobs/GPU trace | Clock-calibration/drift handling, bounded query/loss accounting and known-chain capture validation |
| RND-10 | Measured | Resize drains the shared queue and may stall other surfaces | Per-surface dependency/fence ownership and resize-storm measurements without unsafe buffer reuse |
| RND-11 | P1 per consumer | No universal UI/world latency, memory or frame-lead budget | Representative workloads, distributions and clear quality/performance limits on supported hardware with diagnostics on/off |
| RND-12 | P2 | Native timeout policy is fatal; richer initialization cancellation/normal-close reporting can improve usability | Controlled initialization/close/hang cases with continued message pumping, no leaked leases and explicit exit/recovery policy |

Sorting arbitrary packets by texture or pipeline is not a safe initial optimization:
transparent composition can depend on their original order. Start by measuring
adjacent compatible batches in the UI paint compiler, keeping renderer contracts
general enough for future world rendering.

## Streamer and content gaps

| ID | Priority | Gap / boundary | Completion evidence |
| --- | --- | --- | --- |
| STR-01 | P1 for caches/hot reload | No source generation, content identity or invalidation contract; loose files permit replacement/sharing | Stable identity/version policy, replacement/truncation tests, and prevention of stale completions publishing into a newer resource generation |
| STR-02 | Measured | Files are opened/queried/closed per read; no handle cache or coalesced ranges | Metadata/I/O profiles first; any cache must implement STR-01 and preserve cancellation/shutdown ownership |
| STR-03 | P1 for sustained demand | Encoded bytes and request/I/O counts are bounded, but decoded CPU caches need their own aggregate budget | Consumer byte admission, retention/eviction and failure/cancellation accounting; no claim that streamer residency bounds process memory |
| STR-04 | P2 for dynamic loose content | Exact-range API requires a known byte length; no high-level metadata/full-asset convenience request | Bounded metadata/full-read API through the streamer, never filesystem probing in a codec or UI consumer |
| STR-05 | P2 | CPU priority applies to ready work; storage priority, prefetch and deadlines are not implemented | Queued I/O policy and mixed foreground/background distributions without blocking CPU workers |
| STR-06 | P2 | Raw `read` returns a standard future whose worker-side wait cannot be intercepted | Compatible safer ticket/continuation ergonomics, coordinator-wait diagnostics and clear source/callback/dispatch failure semantics |
| STR-07 | Dependency-blocked | Bundle reader/cooker format is undefined | Agree format, index, integrity, versioning and deterministic writer fixtures before implementing bundle loading |
| CNT-01 | Addressed in phase six | Moved content requests were not safely inspectable/cancellable | `valid()`, non-ready invalid handles and harmless moved-from cancellation, with ownership-transfer regression tests |
| CNT-02 | P2 | WIC normalizes static image buffers; no EXIF/ICC/color pipeline, animated images or decoder-object reuse | Real asset corpus and profiling before adding pooling, codecs or color transformations |

The streaming library retains its generic storage boundary. Asset publication,
decoded-cache eviction and GPU lifetime coordination belong to asset/content
consumers. A shared allocator improves allocation policy but does not define those
ownership or eviction rules.

## Jobs reassessment

The renderer integration uses direct dispatch/joined ranges through the injected
dispatcher; stream completions decode only after I/O readiness. One persistent pool
serves these consumers. No graph construction or second scheduler was required in
the application integration, so this work does not justify a scheduler rewrite.

JOB-01 remains open: bounded frame queues, renderer slots and streamer admission
are consumer limits, not a global bound on arbitrary scheduler task/dependency
storage. Use explicit fan-out bounds for the initial UI. JOB-02 and JOB-04 now have
additional consumer evidence, but real UI layout/paint, video/audio deadlines and
declared-access audits remain necessary. JOB-03 still requires deterministic
consumer publication/replay; execution/arrival timing is not a replay contract.

Worker-local queues/work stealing, task pools, automatic grains and fibers remain
measured candidates. The current weighted shared queues/mutex and allocations have
known costs; switching them without contention/latency evidence could sacrifice
fairness, notification affinity or clarity. See [the complete jobs backlog](25_job_system_backlog.md).

## Recommended next increment

Build a small shared UI runtime with a manually authored tree, rectangles/images,
anchors or one simple layout rule, clipping and immutable paint output. Keep a
sequential reference, cap tree/job/resource sizes, publish each frame explicitly
through `frame_payload` and use placeholders while content is pending. Measure that
vertical slice before choosing RND-01/RND-02 or changing job grain/scheduling.

Add XML authoring/binding, text shaping/font atlases and asset generations/cache
policy as their actual UI use cases arrive. Editor tooling, bundle packaging,
video and world-space UI remain subsequent work. A broad optimization pass before
the first representative UI would lack the workload evidence needed to prioritize it.
