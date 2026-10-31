# Measured scheduler optimization

## Milestone 5: completion storage

The first optimization pass removes the second completion mechanism from direct
tasks. A task already publishes its immutable result with a release store to its
terminal status, after payload/scratch cleanup. Pending coordinator waits already
use the scheduler condition variable, including cooperative delivery of their own
completion notifications. The former per-task `promise/shared_future` duplicated
that state and allocated additional completion storage.

Direct handles now observe terminal status with acquire semantics. An already
completed wait returns or rethrows the stored failure without locking the pool.
A pending wait retains the scheduler while waiting on its existing predicate and
condition variable. If runtime destruction races that lookup, the handle rechecks
terminal publication with acquire semantics: runtime shutdown drains accepted
work before releasing the scheduler. Handles retain their result independently of
the runtime, including original failure/cancellation causes.

This keeps the public API, worker count, dependency policy, cancellation rules,
child joins, payload cleanup and notification affinity unchanged. Legacy independent
`job_handle` and sealed graph APIs retain their existing future-based completion.
No callback starts running on a waiting coordinator except the origin-thread
notification tasks already allowed by the callback contract.

## Measurement method

The baseline is the merged PR #8 implementation at `ad4aeb7`. Baseline and candidate
executables use the same compiler/configuration and the same deterministic phased
workloads. Save each executable separately so measurements can alternate without
recompiling between samples. Use Release with job sanitizer AUTO (disabled), no
ASan/coverage instrumentation, and no other build/test workloads running alongside
the measurement. Runtime warmup and allocation census remain outside timing samples.

Use the [benchmark invocation and limits](19_job_workloads_and_profiling.md), with
seed 42 and 17 repetitions. Compare tiny fine/batched inputs, medium work, one and
several workers, and fine/coarse grains. Check that both reports have the same
checksums/task counts and complete traces before comparing their allocation counts
or timing samples. The allocation census measures successful replacement C++ new
requests and requested bytes, not live memory or OS allocations. Keep generated
XML reports outside tracked sources.

Timing is observational, not a CI acceptance threshold. Repeat comparisons in
alternating executable order, retain regressions, and report the machine and
configuration. Fine-grained or tiny work can remain slower than a direct loop even
after reducing scheduler overhead. Allocation savings do not prove a wall-time
improvement on every workload.

## Local results

Measured on 2026-10-31, Intel Core Ultra 7 265KF (20 reported cores/logical
processors), Windows x64, MSVC 19.44.35229.0, Release/AUTO. Five batches alternated
baseline-first and candidate-first executable order. Each executable warmed its
pool and collected 17 timing pairs per batch: the table reports the median of 85
parallel samples per executable/case. No builds or validation suites ran alongside
these measurements. The host was not isolated and OS scheduling/power settings were
not pinned. This is local evidence, not a portable speedup guarantee.

| Case | Items | Rounds | Grain | Workers | Before (ms) | After (ms) | Elapsed change | C++ new calls before / after |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| tiny-fine | 128 | 1 | 1 | 4 | 0.9100 | 0.8667 | -4.76% | 3901 / 3641 |
| tiny-batched | 128 | 1 | 128 | 4 | 0.0342 | 0.0314 | -8.19% | 77 / 72 |
| medium | 4096 | 64 | 128 | 4 | 0.3722 | 0.3744 | +0.59% | 1045 / 977 |
| heavy-one | 65536 | 128 | 512 | 1 | 22.6139 | 22.5013 | -0.50% | 4027 / 3767 |
| heavy-four | 65536 | 128 | 512 | 4 | 6.1942 | 6.1520 | -0.68% | 4027 / 3767 |
| heavy-eight | 65536 | 128 | 512 | 8 | 5.2254 | 4.8271 | -7.62% | 4027 / 3767 |
| heavy-fine | 65536 | 128 | 64 | 4 | 10.3006 | 10.1579 | -1.39% | 31789 / 29737 |
| heavy-coarse | 65536 | 128 | 4096 | 4 | 6.1382 | 6.1482 | +0.16% | 556 / 520 |

Negative elapsed changes indicate shorter measured execution. Small differences
are not statistically established wins/losses; the medium and coarse regressions
are retained. Paired sequential medians were 0.5871/0.5845 ms for medium and
21.3134/21.4594 ms for heavy-coarse, illustrating variation even in unchanged work.
Tiny sequential medians remained 0.0002 ms: scheduling still loses to that direct
loop. Do not use the first separate before/after run to claim a speedup; alternating
executables reduced the apparent improvements seen in that initial comparison.

The repeatable resource result is one fewer allocation request and 248 fewer
requested bytes per direct task on this MSVC standard library. For heavy-fine,
2,052 tasks reduced requested bytes from 4,292,016 to 3,783,120 (508,896 fewer).
For the 260-task heavy cases, bytes fell from 543,600 to 479,120. These include the
reduced task object and removed completion storage; other standard libraries may
allocate futures differently. Checksums/task counts matched and traces were
complete. A local allocation-budget check failed on the baseline and passed on
all eight candidate cases; platform-specific allocation counts are not CI gates.

## Candidate decisions

| Candidate | Decision and reason |
| --- | --- |
| Completion storage | Implemented and measured above: redundant per-task futures are removed, with a retained baseline executable for allocation and elapsed-time comparisons. |
| Allocation pools | Deferred. Remaining task/link allocations are real, but a pool must preserve handles that outlive the runtime and user cleanup outside locks. Measure its benefit separately after removing unnecessary storage. |
| Worker-local queues/work stealing | Deferred. The current traces expose ready waits and callback intervals, not mutex contention. Global weighted FIFO dispatch and pinned notifications have tested service rules that stealing must preserve. Add contention evidence before changing those rules. |
| Additional affinity | Deferred. Notification affinity is a correctness contract; no measured client requirement justifies pinning general work to a worker/core. |
| Fibers/coroutines | Deferred. Current dependencies, children and listeners make progress without suspending callbacks or blocking workers. Suspension would add stack/lifetime and cancellation contracts without a demonstrated workload requiring it. |

These are evidence-based decisions for this pass, not permanent prohibitions.
Revisit them with representative consumer workloads and profiler evidence. The
library still allocates task/input/link storage and uses a shared scheduler mutex;
it does not claim lock-free, allocation-free or real-time execution.

## Correctness gate

Concurrent and repeated waits cover success, failure, cancellation and skipped
results, including visibility of result writes and payload cleanup, before,
during and after runtime shutdown/destruction. Existing range, child, continuation,
notification, priority, lifecycle and sanitizer tests remain the reference semantics. Validate
affected consumers, Debug/Release, ASan, static analysis, portable builds, coverage
and standalone installed consumers. Reproducible stress exercises wakeups and
cleanup; native Windows ThreadSanitizer remains unavailable.

Further scheduler optimization should preserve those contracts and improve a
measured workload, without treating higher job counts or CPU occupancy alone as
success. Consumer integrations still need their own frame/latency budgets.
