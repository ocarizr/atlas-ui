# Local scheduler reference baseline

This historical Milestone 3 baseline predates weighted priority scheduling and cooperative cancellation. It is not a performance measurement of the later policy.

Measured on 2026-10-30 using Intel Core Ultra 7 265KF (20 reported cores/logical
processors), Windows x64, MSVC 19.44.35229.0, Release, job sanitizer AUTO (disabled for Release).
No ASan, coverage or task tracing was enabled in timing runs. The machine was not
an isolated laboratory host; no CPU affinity/power policy was imposed. These are
observations on this configuration, not universal guarantees or CI thresholds.

Each case used seed 42, one warmup and seven samples, alternating execution order.
Separate untimed passes recorded allocations and a full bounded task trace.
The allocation-counter self-check exercised ordinary, array, aligned and nothrow
requests. XML parsing verified unique IDs, temporal/parent/dependency ordering,
worker bounds, successful outcomes, non-overlapping worker callback intervals,
complete traces and the work-only critical-path lower bound. A special-character
machine label verified XML attribute escaping.

| Case | Items | Rounds | Grain | Workers | Sequential median (ms) | Parallel median (ms) | Ratio | C++ new calls |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| tiny-fine | 128 | 1 | 1 | 4 | 0.000200 | 0.678900 | 0.00029x | 3901 |
| tiny-batched | 128 | 1 | 128 | 4 | 0.000200 | 0.035100 | 0.00570x | 77 |
| medium | 4096 | 64 | 128 | 4 | 0.592000 | 0.306600 | 1.93085x | 1045 |
| heavy-one | 65536 | 128 | 512 | 1 | 19.926400 | 20.968300 | 0.95031x | 4027 |
| heavy-two | 65536 | 128 | 512 | 2 | 20.529400 | 10.638200 | 1.92978x | 4027 |
| heavy-four | 65536 | 128 | 512 | 4 | 19.919000 | 5.744600 | 3.46743x | 4027 |
| heavy-eight | 65536 | 128 | 512 | 8 | 19.965700 | 3.687200 | 5.41487x | 4027 |
| heavy-fine | 65536 | 128 | 64 | 4 | 20.014900 | 8.310400 | 2.40842x | 31789 |
| heavy-coarse | 65536 | 128 | 4096 | 4 | 19.945700 | 5.486800 | 3.63522x | 556 |

The heavy fully-dirty workload benefits from additional workers on this CPU.
One-worker scheduling exposes overhead relative to the fused sequential loop.
Very fine chunks add substantial dispatch/allocation cost; coarse chunks reduce it.
Tiny inputs lose to a direct loop even when batching reduces their scheduling cost.
The observed allocation pressure is evidence for later task-storage/pooling work,
not a reason to replace the measured baseline with an unvalidated allocator.

The ratio is sequential median divided by parallel median. Samples are noisy,
especially the sub-microsecond sequential tiny case. Ratios below one represent
regressions; none were hidden. The parallel algorithm additionally performs partial
hashing, scratch copies and merge orchestration, as described in the
[methodology](19_job_workloads_and_profiling.md).

Reproduce the table by varying size/rounds/grain/workers in the documented command,
using `--repetitions 7 --seed 42`. Keep generated XML outside tracked source paths.
The report contains per-worker callback durations, task identities/relationships,
scratch high-water statistics, allocation-request counts/bytes and individual
samples. Its critical-path number is a leaf-work lower bound, not an exact causal
path including queue/lock contention. Full profiler reconstruction, real consumer
budgets and broader hardware/workload validation remain follow-up work.
