# UI frame execution

`atlas::ui::frame_executor` coordinates named serial and joined-work phases on the
creating application coordinator. It borrows an injected dispatcher; it creates no
scheduler, render thread or Lua VM. Call order defines phase order; names are
diagnostic identities rather than a built-in game-loop or layout schedule.

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
properties and dirty flags in ascending live-slot order. Slot order is not a
parent-first traversal guarantee. Capture does not acknowledge dirtiness. Snapshots
survive tree mutation/destruction and retain their PMR resource. Their handles still
need live validation for later commands; capture does not reconcile stale results.

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
