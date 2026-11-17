# UI frame execution

`atlas::ui::frame_executor` coordinates named serial and joined-work phases on the
creating application coordinator. It borrows an injected dispatcher; it creates no
scheduler, render thread or Lua VM. Call order defines phase order; names are
diagnostic identities rather than a built-in game-loop or layout schedule.

[Input and lifecycle](ui_input.md) supplies serial event stages that can run inside
these dispatch phases. Its `begin_frame` opens the tree's command producer; avoid
opening that producer a second time in the same frame. Callers choose where to
seal/apply commands and run joined work between input, tick and late-tick.

```cpp
atlas::ui::frame_executor frames{shared_jobs, {}, shared_memory, diagnostics};
frames.begin_frame(42);
frames.dispatch("tick", [&] {
    auto& commands = tree.commands();
    commands.begin_frame(42);
    commands.set(element, atlas::ui::property::width, 240.f, producer_id);
    tree.apply(commands.seal());
});
const auto input = tree.snapshot();
const auto widths = frames.run_phase<float>("prepare_widths", input.elements(),
    [](const atlas::ui::phase_range<atlas::ui::element_record, float>& range) {
        for (std::size_t index = 0; index < range.inputs.size(); ++index) {
            range.outputs[index] = range.inputs[index].requested.width;
        }
    });
frames.dispatch("publish", [&] { consume_widths(widths.values()); });
frames.end_frame();
```

The example copies requested widths; it does not calculate layout. Serial dispatch
is the owner-thread entry for command/event production and publication. Captured
locals are allowed there. Work kernels are capture-free callables or nonnull function
pointers. They receive stable input and an equal-length exclusive output slice, its
original index and a cooperative stop token.

## Snapshot and publication contracts

`tree.snapshot()` copies committed handles, parent/child/sibling links, requested
properties, common layout styles, typed panel policies, visual origins and dirty
flags in ascending live-slot order. Canvas and stack overrides use separate sparse spans. It also
captures the layout revision and viewport. Slot order is not a
parent-first traversal guarantee. Capture does not acknowledge dirtiness. Snapshots
survive tree mutation/destruction and retain their PMR resource. Their handles still
need live validation for later commands; capture does not reconcile stale results.
The [layout engine](ui_layout.md) uses these captures for joined measure/arrange and
validates revision/session before live geometry publication.

`run_phase<Output>()` privately default-constructs outputs before job admission.
Workers declare slice reads/writes through the jobs access checker. Every accepted
child joins the root before results return, including on failure. Successful results
are immutable, ordered by input index and carry a frame/phase identity. Small work
runs the same kernel on the coordinator without job admission.

Return is a CPU synchronization point, distinct from GPU completion/presentation.
Inputs must remain stable/alive until return. Snapshots supply owned input; arbitrary
borrowed spans remain the caller's responsibility. Workers cannot access the live
registry. Publishing snapshots/results to other threads still needs synchronization.
Only the result container and its values are owned automatically. Pointer-valued
inputs/outputs do not transfer ownership or deep-copy their targets; callers must
retain any external storage those values reference.

## Frames and failure

Frames have positive increasing IDs; distinct phase names contain at most 64 bytes
without control characters. Preflight rejection consumes no phase identity or jobs.
Failure after admission latches the frame: later phases/end-frame are forbidden.
A newer frame explicitly resets a failed scope. An open successful frame must end first.

Failed allocation/admission, child errors and cancellation return no partial result.
Parallel errors preserve their original exception type after joining accepted work.
Earlier successful results remain valid: publication is per phase, not whole-frame
rollback. Arbitrary callback side effects are not undone. Failed serial producers
may need `discard_pending()` before another command dispatch.

`begin_frame(id, job_options)` explicitly selects priority/token for both paths,
overriding options previously attached to the injected dispatcher. Stop is checked
before preparation, around kernels and before successful return. Long kernels should
call `range.throw_if_cancelled()`; consumer code cannot be forcibly preempted.

Worker, foreign-thread and reentrant calls fail before admission. Output construction,
joins and diagnostic delivery remain guarded. Phase names are copied before callbacks
run. The pool must outlive execution; no borrowed job payload escapes a returning call.

## Budgets and diagnostics

Defaults are 65,536 items, 64 MiB direct outputs, 32 phases, grain 128, threshold 4,096
and at most 16 children plus one root per phase. Supported caps are 1,048,576 items,
64 phases and 64 children. Grain expands to respect the child cap. Empty work,
one-grain work, a child cap of one and work below threshold stay sequential.
The policy evaluates count/grain at runtime; there is no automatic hardware discovery
or adaptive timing. Applications can disable splitting for a one-worker pool.

Bounds exclude other scheduler consumers, nested user allocations, metadata and
retained results across frames. Retention requires a consumer budget. PMR resources
must support release on every thread holding results. Disjoint slices do not prevent
false sharing; grain/alignment and output layout matter. Declared-access checking
does not infer accesses through pointers inside inputs.

Injected profiling records serial scopes in `ui.dispatch`, coordinator work/join in
`ui.phase`, and worker ranges in `ui.cpu`. Coordinator intervals include waiting;
they are not CPU utilization. The jobs observer can supply task/parent timings.
Lifecycle logs use debug severity and failures retain their original exceptions.
Profiling follows `ATLAS_PROFILING=auto|on|off`; no file/transport is created.

## Measurement tool

`atlas_ui_benchmark [workers]` writes XML for fixed 32/256/4096/65536-item arithmetic
workloads. Persistent executors are warmed, order alternates, all 20 samples are
retained and ordered checksums must agree. The report includes P50/P95 and actual
admitted jobs. One path forces sequential execution; the other permits bounded jobs.
The 32-item case fits one grain and stays serial. Timing includes output preparation,
admission and join, excluding reduction/destruction. Padded 64-byte-aligned outputs
reduce boundary sharing in this probe. It is not a layout benchmark or production UI budget;
calibrate policy with the actual kernel, hardware, toolchain and instrumentation.

## Authored viewport readiness

Build the `ui-content` component's `atlas_ui_readiness_benchmark` target.
Run it without arguments in Release for 100 samples per case and per policy,
after five warm-up frames. The probe alternates order between serial execution
and a candidate with eight chunks, layout admission at 256 nodes and a subtree
grain of 128, and paint admission at 128 surviving quads. It uses four workers
at 1920×1080 logical units and reports the actual number of admitted jobs.
`--smoke` runs the same assertions on smaller documents without timing gates.

Each scenario serializes and reopens an authored document, then builds separate
runtime instances with rounded SDF panels, a Lua tick and a ViewModel loaded
through a typed, memory-backed streamer. The workloads cover small and dense
idle trees, viewport resize, shape glow, clipped drawer motion and layout
animation. The four dense mutation scenarios share the same tree shape.
Frame timing starts with owner dispatch and ends after the new paint packet
replaces the previous one. Each frame compares geometry, painter order and
ViewModel behavior with the serial reference. Glow and drawer motion must
preserve the layout revision. A separate memory pass counts requested PMR
bytes and checks their retirement after each policy instance is destroyed.

Run `--trace` in Debug or with `ATLAS_PROFILING=on` and redirect stdout to a
Chrome trace JSON file. This is a short, separately instrumented capture.
Release timings exclude startup, file I/O, font shaping, renderer upload,
GPU execution and presentation. The memory pass covers the injected
composition/layout/paint resource and excludes scheduler allocations, Lua
allocations outside that resource, allocator overhead and whole-process RSS.
Native editor tests cover authoring/persistence and preview ownership
separately, including save cleanup and rejected replacement.

Two Windows x64 runs on a Core Ultra 7 265KF with clang-cl 19.1.5 Release,
profiling inactive and the jobs checker disabled produced these P95 ranges:

| Workload | Nodes | Serial P95 | Candidate P95 |
| --- | ---: | ---: | ---: |
| Idle menu | 27 | 8.9–9.0 µs | 8.7–9.0 µs |
| Idle preview | 2,329 | 1.55–1.64 ms | 1.69–1.71 ms |
| Resize preview | 2,329 | 1.83–1.90 ms | 2.01–2.10 ms |
| Shape glow | 2,329 | 1.04–1.16 ms | 1.12–1.15 ms |
| Clipped drawer | 2,329 | 0.89–1.16 ms | 1.10–1.11 ms |
| Layout reflow | 2,329 | 1.23–1.31 ms | 1.38–1.45 ms |

The serial dense cases retained 1.96 MiB and peaked at 3.97 MiB through the
injected resource. At most nine layout jobs and nine paint jobs were admitted
per candidate frame. The candidate increased the dense frame PMR peak by 128
bytes, excluding scheduler storage. Both visual animations arranged zero
nodes after warm-up. The candidate did not consistently improve frame tails,
and the serial policy remained faster at P95 in both resize and reflow runs.
The reference budgets for repeating this probe on the same host are 25 µs P95
for the small menu, 2.5 ms P95 for the dense cases, and 5 MiB peak requested
PMR for a dense policy instance. These are local comparison budgets.
