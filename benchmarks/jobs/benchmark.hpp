/**
 * @file
 * @brief Warmup, alternating samples and reference verification for generic job workloads.
 */

#pragma once
#include "allocation_counter.hpp"
#include "phased_workload.hpp"

#include <iosfwd>

namespace atlas::jobs::workloads
{
/// @brief Reproducible workload, pool size and sampling configuration for local comparisons.
struct benchmark_options
{
    /// @brief Seeded synthetic input configuration used by the benchmark.
    workload_config workload;
    /// @brief Persistent worker threads or their observed count in the enclosing snapshot.
    std::size_t workers{4};
    /// @brief Number of measured sample pairs after warmup.
    std::size_t repetitions{5};
    /// @brief Caller-supplied reference machine description included in the XML report.
    std::string machine{"unspecified"};
};

/// @brief Verified measurements, allocation census and trace captured for one workload
/// configuration.
struct benchmark_result
{
    /// @brief Measured sequential execution samples in nanoseconds.
    std::vector<std::uint64_t> sequential_ns;
    /// @brief Measured jobified execution samples in nanoseconds.
    std::vector<std::uint64_t> parallel_ns;
    /// @brief Deterministic ordered reference hash used to verify equivalent work.
    std::uint64_t checksum;
    /// @brief Successful replacement C++ allocation census collected outside timing samples.
    allocation_counts allocations;
    /// @brief Optional bounded task trace or the resulting trace report.
    job_trace_report trace;
};

/// @brief Parse and validate benchmark command-line sizes, seeds and execution policy.
benchmark_options parse_options(int argc, char** argv);
/// @brief Warm up, validate and measure the configured deterministic workload and allocation
/// census.
benchmark_result measure(const benchmark_options& options);
/// @brief Write benchmark measurements, provenance, allocations and bounded trace metadata as XML.
void write_report(std::ostream& output, const benchmark_options& options,
                  const benchmark_result& result);
} // namespace atlas::jobs::workloads
