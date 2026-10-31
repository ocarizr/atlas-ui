# Generic phased workloads and profiling

## Scope and invariants

The reference workload is scheduler-owned test/benchmark support, outside the
production jobs library. It has no UI, storage, ECS, media or gameplay concepts.
An immutable input snapshot feeds parallel map chunks, parallel partial hashes
and an ordered merge. The rolling hash composes partials by their original index;
completion/worker arrival order never defines output order. Unsigned arithmetic
has explicit modulo-2^64 behavior. Inputs and seeds are reproducible.

Each phase's root joins its children before the next dependent phase starts.
A failure drains admitted children, skips dependent phases and does not publish
a final checksum. A completed batch rejects late attempts to reopen that same
state. These are workload scope invariants built on normal jobs/handles, not a
global frame state machine imposed on all clients.

Parameterized native tests compare 1/2/4 workers, empty/small/uneven ranges,
grains, seeds, unchanged snapshots and sparse updates with a full sequential
reference. Sparse tests retain cached values for unchanged items and recompute
only dirty items. Benchmarks use fully dirty input, so reported parallel benefit
is not obtained by doing less transformation work than the reference.

## Tool and invocation

`atlas_jobs_benchmark` is an independent CMake/CI component linking only jobs and
the standard library. CI imports the same-run jobs product and runs correctness,
empty-input, invalid-argument and help checks. Timing thresholds are deliberately
absent from CI. Package delivery still selects the game/editor/content-tool
products; the benchmark is a development tool.

```powershell
. ./scripts/initialize_toolchain.ps1
cmake --preset windows-msvc
cmake --build --preset windows-release --target atlas_jobs_benchmark
./build/windows-msvc/benchmarks/jobs/Release/atlas_jobs_benchmark.exe --size 65536 --grain 512 --rounds 128 --workers 4 --repetitions 7 --seed 42 --machine "reference CPU" > build/jobs-report.xml
```

Use `--help` for flags or `--smoke` for a small correctness run. Bounds are
0..4,194,304 elements, 1..4,194,304 grain, 1..1,048,576 rounds, 1..256 workers and
1..100 repetitions. Seed is an unsigned decimal integer. `--machine` supplies a
human-readable hardware label, not a machine identifier or a local path.

Use a Release build with `ATLAS_JOB_SANITIZER=OFF` or default AUTO. ASan, coverage,
static-analysis builds and the job checker are validation configurations, not
representative performance measurements. Keep software, power settings and other
machine activity controlled when comparing results. OS worker placement is not
pinned by this tool, including on heterogeneous-core hardware.

## Measurement method

Inputs and output buffers are prepared outside the measured interval. A persistent
pool is created once, then warmed with the same deterministic data. Samples alternate
sequential-first and parallel-first order. The sequential reference is a simple
fused transform/hash loop. The parallel path includes dispatch, phase boundaries,
scratch copies, partial reduction, completion and handle cleanup. Both produce the
same checksum, but the parallel path performs additional orchestration and passes;
this is a useful baseline, not a claim of identical instruction counts.

Each sample is retained; the summary uses the upper median for even sample counts.
The tool does not assert a speedup or hide slower small cases. Compare sequential
reference, one-worker scheduling, several worker counts and several grains.

Allocation counting and tracing use separate untimed runs, checked against the same
reference. The allocation census replaces ordinary/array/aligned C++ new/delete
only in this executable. It counts successful requests routed through those
replacements during dispatch-to-completion, excluding input preparation, pool
startup and report formatting. It excludes allocations inside DLLs or direct
malloc/OS APIs that bypass replacement new. Its count/byte values are not live
memory or process RSS. The replacement remains present during timing runs, with
counting disabled; independent external profiling is appropriate for later tuning.

## XML report and interpretation

The versioned XML includes machine label, compiler, target system, configuration,
hardware thread count, checker mode, case parameters, checksum, samples and a
bounded trace. Each task includes category, parent/prerequisite IDs, worker,
submission/start/body-end/completion times and scratch reservation statistics.

Per-worker `callback_ns` sums callback wall time. It excludes scheduler/cleanup
work and is not CPU utilization: callbacks that block would still accumulate
wall time. Divide by profile elapsed time only as a callback-occupancy observation.

`leaf_critical_path_lower_bound_ns` sums the longest leaf callback in each of the
three sequential phases. It is a work-only lower bound for this workload. It
excludes parent admission, scheduling, queue contention and cleanup; it is not a
generic exact critical-path reconstruction. Parent bodies may overlap children,
so summing their durations into that path would double-count work.

Scratch reservations are per owning task and are already released on completion.
Summing them reports total scope reservations, not their concurrent process peak.
A child using a parent's resource charges that parent. Traces with dropped or
pending records fail benchmark collection rather than silently producing partial
metrics. Full causal/lock tracing, allocator pools, automatic grain policies and
work stealing remain measurement-driven follow-up work.

See the [recorded baseline](20_job_workload_baseline.md) and the
[implementation plan](14_job_system_implementation_plan.md).

Completion-listener traces additionally distinguish caller-thread and worker-bound
affinity deliveries; caller intervals are excluded from worker occupancy. See the
[notification diagnostics contract](22_job_completion_callbacks.md).
