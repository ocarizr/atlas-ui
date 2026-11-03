/** @file @brief Synthetic phase-grain observations; no production UI budget or speedup threshold.
 */
#include <algorithm>
#include <array>
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/frame_executor.hpp>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <limits>
#include <numeric>
#include <vector>

namespace
{
/// @brief Avoid adjacent chunk writes sharing a cache line in this synthetic comparison.
struct alignas(64) output_value
{
    std::uint64_t value = 0; ///< Deterministic arithmetic result, independent of worker assignment.
    std::array<std::byte, 56>
        padding{}; ///< Explicit one-cache-line extent avoids implicit MSVC padding.
};

/// @brief Fixed arithmetic work intentionally excludes layout, storage and GPU activity.
void kernel(const atlas::ui::phase_range<std::uint64_t, output_value>& range)
{
    for (std::size_t index = 0; index < range.inputs.size(); ++index)
    {
        auto value = range.inputs[index];
        for (std::uint64_t repeat = 0; repeat < 128; ++repeat)
        {
            value = (value ^ (value >> 13U)) * 0x9e3779b185ebca87ULL + repeat;
        }

        range.outputs[index].value = value;
    }
}

/// @brief Timing excludes result reduction/destruction, but includes preparation, admission and
/// join.
struct sample
{
    double microseconds;    ///< Coordinator wall time, not worker CPU utilization.
    std::uint64_t checksum; ///< Stable ordered result check outside the timed interval.
};

/// @brief Warmed persistent executors use explicit increasing frame identities.
sample measure(atlas::ui::frame_executor& executor, std::span<const std::uint64_t> input,
               std::uint64_t frame)
{
    const auto start = std::chrono::steady_clock::now();
    executor.begin_frame(frame);
    const auto result = executor.run_phase<output_value>("arithmetic", input, kernel);
    executor.end_frame();
    const auto end = std::chrono::steady_clock::now();
    std::uint64_t checksum = 0;
    for (const auto& value : result.values())
    {
        checksum = checksum * 31U + value.value;
    }

    return {std::chrono::duration<double, std::micro>{end - start}.count(), checksum};
}

/// @brief Quantiles use retained samples; no faster/slower sample is discarded.
double quantile(std::vector<double> samples, std::size_t percent)
{
    std::sort(samples.begin(), samples.end());
    return samples[(samples.size() - 1) * percent / 100];
}
} // namespace

/// @brief Write a reproducible XML report to stdout; optional argument selects 1..64 workers.
int main(int argc, char** argv)
{
    try
    {
        std::size_t workers = 4;
        if (argc > 2)
        {
            throw std::invalid_argument("expected at most one worker-count argument");
        }

        if (argc == 2)
        {
            const std::string_view value{argv[1]};
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), workers);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                workers == 0 || workers > 64)
            {
                throw std::invalid_argument("worker count must be in 1..64");
            }
        }

        atlas::jobs::job_system pool{workers};
        atlas::ui::frame_limits limits;
        limits.min_parallel_items = std::numeric_limits<std::size_t>::max();
        atlas::ui::frame_executor serial{pool.dispatcher(), limits};
        limits.min_parallel_items = 1;
        atlas::ui::frame_executor parallel{pool.dispatcher(), limits};
        std::cout << "<ui_phase_benchmark workers=\"" << workers
                  << "\" samples=\"20\" grain=\"128\" max_chunks=\"16\" profiling_compiled=\""
                  << atlas::diagnostics::instrumentation_enabled << "\">\n";
        std::uint64_t frame = 0;
        for (const std::size_t count : std::array<std::size_t, 4>{32, 256, 4096, 65536})
        {
            std::vector<std::uint64_t> input;
            input.resize(count);
            std::iota(input.begin(), input.end(), 1ULL);
            for (int warmup = 0; warmup < 3; ++warmup)
            {
                ++frame;
                (void)measure(serial, input, frame);
                (void)measure(parallel, input, frame);
            }

            std::vector<double> sequential_times;
            std::vector<double> parallel_times;
            for (int iteration = 0; iteration < 20; ++iteration)
            {
                ++frame;
                sample first{};
                sample second{};
                if (iteration % 2 == 0)
                {
                    first = measure(serial, input, frame);
                    second = measure(parallel, input, frame);
                }
                else
                {
                    second = measure(parallel, input, frame);
                    first = measure(serial, input, frame);
                }

                if (first.checksum != second.checksum)
                {
                    throw std::runtime_error("phase outputs differ");
                }

                sequential_times.push_back(first.microseconds);
                parallel_times.push_back(second.microseconds);
            }

            std::cout << "  <case items=\"" << count << "\" sequential_p50_us=\""
                      << quantile(sequential_times, 50) << "\" sequential_p95_us=\""
                      << quantile(sequential_times, 95) << "\" parallel_p50_us=\""
                      << quantile(parallel_times, 50) << "\" parallel_p95_us=\""
                      << quantile(parallel_times, 95) << "\" admitted_jobs=\""
                      << parallel.statistics().admitted_jobs << "\"/>\n";
        }

        std::cout << "</ui_phase_benchmark>\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        (void)std::fputs(error.what(), stderr);
        (void)std::fputc('\n', stderr);
        return 1;
    }
    catch (...)
    {
        (void)std::fputs("unclassified UI benchmark failure\n", stderr);
        return 1;
    }
}
