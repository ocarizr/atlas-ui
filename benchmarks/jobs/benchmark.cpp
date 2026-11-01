/**
 * @file
 * @brief Warmup, alternating samples and reference verification for generic job workloads.
 */

#include "benchmark.hpp"

#include <algorithm>
#include <chrono>

namespace atlas::jobs::workloads
{
namespace
{
/// @brief Steady clock used by benchmark wall-duration samples.
using clock_type = std::chrono::steady_clock;

/// @brief Read elapsed steady-clock nanoseconds for the benchmark's current sample.
std::uint64_t elapsed(clock_type::time_point start)
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now() - start).count());
}

/// @brief Build a workload pool configuration with explicit workers and opt-in trace capacity.
job_system_options runtime_options(const benchmark_options& options, bool tracing)
{
    job_system_options result;
    result.worker_count = options.workers;
    result.scratch.max_reserved_bytes = std::max(
        result.scratch.max_reserved_bytes,
        std::min(options.workload.grain, options.workload.size) * sizeof(std::uint64_t) + 4096);
    const auto chunks =
        options.workload.size == 0 ? 0 : 1 + (options.workload.size - 1) / options.workload.grain;
    result.trace_capacity = tracing ? 4 + chunks * 2 : 0;
    return result;
}

/// @brief Validate one candidate access against currently live declarations or the sequential
/// reference.
void check(const workload_state& state, std::uint64_t reference)
{
    if (state.checksum != reference || state.phase.load() != 3)
    {
        throw std::runtime_error{"Parallel phases did not match the sequential reference"};
    }
}

/// @brief Run sequential and parallel samples in alternating order and verify equivalent results.
void measure_pair(job_system& runtime, const benchmark_options& options, benchmark_result& result,
                  std::size_t iteration)
{
    auto state = prepare_workload(options.workload);
    const auto sequential = [&]
    {
        const auto start = clock_type::now();
        const auto reference = sequential_reference(*state);
        result.sequential_ns.push_back(elapsed(start));
        if (reference != result.checksum)
        {
            throw std::runtime_error{"Reference checksum changed"};
        }
    };
    const auto parallel = [&]
    {
        const auto start = clock_type::now();
        submit_workload(runtime.dispatcher(), state).merge.wait();
        result.parallel_ns.push_back(elapsed(start));
        check(*state, result.checksum);
    };
    if (iteration % 2 == 0)
    {
        sequential();
        parallel();
    }
    else
    {
        parallel();
        sequential();
    }
}

/// @brief Measure successful C++ allocation requests for one verified workload outside timing
/// samples.
allocation_counts allocation_census(job_system& runtime, const benchmark_options& options,
                                    std::uint64_t reference)
{
    auto state = prepare_workload(options.workload);
    runtime.wait_idle();
    begin_allocation_counting();
    try
    {
        submit_workload(runtime.dispatcher(), state).merge.wait();
    }
    catch (...)
    {
        (void)end_allocation_counting();
        throw;
    }

    const auto result = end_allocation_counting();
    check(*state, reference);
    return result;
}
} // namespace

/// @brief Warm up, validate and measure the configured deterministic workload and allocation
/// census.
benchmark_result measure(const benchmark_options& options)
{
    verify_allocation_counter();
    benchmark_result result{};
    result.sequential_ns.reserve(options.repetitions);
    result.parallel_ns.reserve(options.repetitions);
    job_system runtime{runtime_options(options, false)};
    auto warmup = prepare_workload(options.workload);
    result.checksum = sequential_reference(*warmup);
    submit_workload(runtime.dispatcher(), warmup).merge.wait();
    check(*warmup, result.checksum);
    for (std::size_t iteration = 0; iteration < options.repetitions; ++iteration)
    {
        measure_pair(runtime, options, result, iteration);
    }

    result.allocations = allocation_census(runtime, options, result.checksum);
    job_system traced{runtime_options(options, true)};
    auto trace_state = prepare_workload(options.workload);
    submit_workload(traced.dispatcher(), trace_state).merge.wait();
    check(*trace_state, result.checksum);
    result.trace = traced.trace_report();
    if (result.trace.dropped || result.trace.pending)
    {
        throw std::runtime_error{"Incomplete benchmark trace"};
    }

    return result;
}
} // namespace atlas::jobs::workloads
