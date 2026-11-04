# Layout execution and measurement

The layout engine borrows the application's existing jobs dispatcher. It creates
no pool, runs no Lua callbacks and publishes geometry only after accepted work has
joined. The tree, cache and publication remain coordinator-owned.

## Scheduling policy

`layout_limits` retains the independent-root policy: 65,536 maximum nodes, a
4,096 dirty-node threshold and at most 16 child chunks per phase. Setting
`max_chunks = 1` disables worker admission. Independent roots can run joined
measure and arrange phases when the threshold is reached.

For one dirty root, `subtree_grain` controls optional intra-root measurement.
Its default is **zero**, which skips partition preparation entirely. A positive
value requests a minimum node count on both sides of a proposed split, in addition
to the existing total-node threshold and chunk cap:

```cpp
atlas::ui::layout_limits policy;
policy.subtree_grain = 1024;
policy.max_chunks = 8;
atlas::ui::layout_engine layout{shared_jobs, policy, shared_memory, diagnostics};
```

This is an opt-in policy to calibrate with the actual kernel and machine, not an
automatic timing controller. Current scalar measurement kernels are cheap; their
parallel execution must not be justified using an unrelated arithmetic benchmark.
The real-layout probe compares default, sequential, bounded and fine-panel paths.

## Dependency cut

Preparation uses an iterative raw-preorder subtree endpoint table. A bounded
frontier starts with the root and refines large intervals at child-panel
boundaries. The search descends through unary wrappers at every depth. Several
small sibling panels can share one interval; a panel containing only lightweight
leaves stays indivisible. Leaf-only roots, deep chains, insufficient work and
exhausted range budgets stay inline. No one-job-per-node rule is used.

Each selected interval contains complete adjacent sibling subtrees. Intervals are
disjoint and borrow one immutable source plus an exclusive desired-size slice.
Workers run the same reverse-preorder scalar kernel as the sequential reference.
Parent aggregation retains its exact child reduction order, including explicit
stack order. No subtree objects are copied into task payloads.

An executor-created root context admits at most `max_chunks` attached children and
returns without waiting. Its completion handle joins the children and cleanup.
Only the coordinator waits. After that dependency boundary, the coordinator
reduces unselected ancestors and leaves, skipping every already measured interval.
This conservative frontier is a nonblocking divide-and-conquer composition; it
does not require recursive C++ calls, per-node dependency counters or a dynamic
worker-side ready queue.

Measurement and arrangement dependencies remain distinct. Current intrinsic
measurement depends on child desired sizes. Sibling boundary relations belong to
intrinsic canvases and affect arrangement, not natural measurement. Arrangement
within one root remains sequential in resolved parent/sibling dependency order.
Stacks allocate sizes after measurement; related source borders precede dependent
placement. Independent roots retain their separate arrangement jobs.

The policy applies to common panel hierarchy semantics, not a canvas-specific
fan-out hook. Grid or other future panels must supply their own sizing/slot
semantics. Width-dependent text or arbitrary new intrinsic callbacks cannot be
assumed independent: their constraint flow and any bounded remeasurement must be
defined before using this scheduling path. General constraint solving is absent.

## Ownership, memory and failure

For `N` nodes and a child cap `C`, endpoint storage requests `4N` bytes and range
capacity requests `sizeof(measure_interval) * C` bytes. On the validated x64 build,
the interval is 12 bytes. There are no per-node callable allocations. The bounded
refinement uses no recursion and at most `C` ranges; its worst-case planning work
is `O(NC + C²)`, with the configured cap at most 64.

One attached scope admits at most `C + 1` tasks, including its root. This is a
scope bound, not an observed simultaneous peak, a scheduler queue-capacity count
or a bound on terminal records retained by handles/observers across scopes.
Task payloads borrow the output/source lifetime retained by the coordinator until
join. The worker declares immutable node reads and exclusive desired writes to the
optional jobs checker. Disjoint writes may still share cache lines at range
boundaries; the checker can warn about possible false sharing. No claim of race
freedom or zero cache-line contention is made.

The supplied thread-safe PMR owns endpoint/range vectors and existing numeric
buffers. Planner storage is released before arrangement and is not retained in the
root cache. Scheduler records, control blocks and EnTT storage have separate
allocation paths. PMR totals are not process RSS or a fragmentation guarantee.

`statistics()` reports total admitted jobs, selected subtree ranges/nodes and
requested planner capacity. Selected counts describe the planned work; failures
can prevent its execution. Admission statistics count accepted tasks, not peak
running tasks. Unchanged revision reuse allocates no planner storage. Cache
invalidation and numeric reuse remain root-granular; captured input and changed
forest preparation can still materialize large arrays.

Allocation failures, root rejection, worker failures and cancellation return no
partial result. Already accepted children remain attached through cleanup, even
if later child admission fails. The previous cache is committed only after the
entire calculation succeeds. Stale live publication still rejects by session,
revision and viewport. No frame-local layout work is intentionally carried into
the next frame.

## Diagnostics

Injected optional CPU scopes distinguish `ui.tree.snapshot`, `ui.layout.prepare`,
`ui.layout.plan`, `ui.layout.measure`, `ui.layout.subtree`, `ui.layout.join`,
`ui.layout.measure.parents`, `ui.layout.arrange` and `ui.tree.publish_layout`.
Coordinator measure scopes include waiting; worker scopes describe actual callback
wall intervals. They are not CPU utilization measurements. Initialization/failure
logging retains the existing guarded diagnostics contract without per-node success
logs. Profiling follows `ATLAS_PROFILING=auto|on|off`.

## Real-layout benchmark

Build `atlas_ui_benchmark` in Release and run:

```powershell
atlas_ui_benchmark --layout
atlas_ui_benchmark --layout-smoke
# Requires a profiling-enabled build; use ATLAS_PROFILING=on for Release.
atlas_ui_benchmark --layout-profile
```

The ordinary worker-count argument retains the separate arithmetic phase probe.
Layout mode uses four persistent workers, three warmups and 20 samples per policy.
Sampling rotates policy order, retains raw samples, validates exact numeric
geometry and reports P50/P95. Cases cover small/wide/large trees, nested related
panels, a deep unary chain, multiple dirty roots and unchanged cached input.

Frame wall timing includes requested mutation, input capture, calculation, live
publication, temporary input release and retirement of the previous consumer
result. Capture/calculation/publication/retirement are also reported separately. Input
construction, checks and report formatting are outside timing; one consumer result
is retained per policy. In changed cases, one leaf per root changes every sample.
The cached case still pays capture/publication but reuses calculated geometry.

A separate untimed census forces another change for changed cases. It reports
requested PMR calls, retained bytes before/after and simultaneously live requested
PMR peak. The benchmark-only replacement C++ allocation counter is reused from
the jobs benchmark and self-checks its entry points. Replacement operators remain
linked during timing with counting disabled. Its counts/bytes are cumulative
successful requests during the census, not live bytes; they exclude startup and
allocations in DLLs/direct OS or malloc paths bypassing replacement operators.
Do not add PMR and C++ census totals because their coverage can overlap.

Profile mode embeds Chrome trace JSON in XML and changes timings/memory. Use it
for attribution separately from the ordinary Release/profiling-off timing run.
Each case clears setup events before warmup and rejects incomplete captures. When
importing diagnostics, use products built with the required profiling policy;
setting the flag only on a consumer does not reconfigure exported definitions.
XML includes compiler, configuration, platform, worker/thread counts and checker/
profiling state. Run on an otherwise idle machine. No timing threshold is enforced
by the smoke test and no production UI/FPS guarantee is inferred.

Queue/pool reserved capacities, whole-process peaks, retained third-party heaps
and production editor workloads still require the broader memory audit (#116)
and representative readiness work (#81). Those limitations must accompany any
performance comparison.

## Local observations

Local Windows x64/MSVC 19.44.35229.0 Release observations on 2026-11-03 used four
workers, 20 reported hardware threads, profiling/checker off, three warmups and
20 retained samples per policy. The last-created leaf in each root changed per
sample, so exact input comparison includes the cost of detecting a late change.
Two independent invocations showed no consistent benefit from intra-root
measurement. Values below are the first invocation's frame P50/P95 microseconds,
including capture, publication and retirement:

| Case (nodes) | Serial | Default | Bounded intra-root | Fine panel |
| --- | ---: | ---: | ---: | ---: |
| Small (73) | 10.7 / 28.0 | 10.0 / 26.7 | 10.0 / 22.7 | 52.5 / 63.5 |
| Wide (8,225) | 1,693.3 / 1,968.8 | 1,655.1 / 1,813.9 | 1,759.6 / 1,933.4 | 1,816.1 / 1,931.9 |
| Large (32,801) | 7,306.6 / 7,756.6 | 7,376.7 / 7,876.1 | 7,557.1 / 8,240.0 | 7,619.1 / 8,008.1 |
| Nested related (4,129) | 915.9 / 965.7 | 937.2 / 1,194.1 | 1,053.1 / 1,349.5 | 1,072.5 / 1,153.6 |
| Deep narrow (4,114) | 925.8 / 1,725.3 | 921.9 / 981.5 | 948.3 / 1,007.6 | 976.9 / 1,155.0 |
| Eight dirty roots (8,232) | 1,742.9 / 1,877.2 | 1,509.6 / 1,706.4 | 1,507.0 / 1,688.8 | 1,483.8 / 1,657.0 |
| Cached (1,041) | 18.5 / 18.8 | 18.3 / 18.5 | 18.5 / 18.8 | 18.6 / 18.8 |

The bounded policy uses threshold 4,096, grain 1,024 and cap 16. Fine-panel uses
threshold/grain one and cap 64; leaf-only panels remain indivisible. Default uses
the existing independent-root policy with intra-root grain zero. Small timing
differences between the identical serial kernels are measurement variability,
not evidence of a faster default algorithm. The second invocation measured
7,435.1 versus 7,657.5 us for serial/bounded large trees and 1,647.8 versus
1,432.2 us for serial/default eight-root forests.

For the large case, all policies retained 8,068,992 requested PMR bytes after the
frame and reached the same 21,058,236-byte PMR peak (about 20.08 MiB). Temporary
planner storage was released before the later peak. This excludes EnTT, scheduler
heaps, control blocks outside PMR and allocator caches; it is not a low-footprint
claim for the application.

| Large-case policy | Admitted jobs | Planner capacity (bytes) | C++ requests | Cumulative C++ requested bytes |
| --- | ---: | ---: | ---: | ---: |
| Serial/default | 0 | 0 | 5 | 584 |
| Bounded | 17 | 131,396 | 242 | 30,320 |
| Fine panel | 33 | 131,972 | 466 | 58,352 |

An additional Release run with profiling explicitly enabled attributed large-case
serial P50 scopes to about 1,862.5 us of preparation, 343.3 us of measurement and
2,369.7 us of arrangement. Bounded measurement occupied 417.9 us, including plan
and join; those nested scopes must not be added to their enclosing measurement.
These instrumented values are attribution evidence, not replacements for the
uninstrumented table. All seven per-case captures were complete.

The default therefore keeps intra-root measurement off. Independent-root jobs
remain useful in the measured forest; extra parallelism is available for explicit
calibration rather than enabled by an arithmetic-kernel result. Finer incremental
reuse, snapshot/preparation traffic, resolved-slot arrangement candidates and
scheduler allocation costs need separate evidence before further optimization.
