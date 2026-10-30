#pragma once
#include "allocation_counter.hpp"
#include "phased_workload.hpp"

#include <iosfwd>

namespace atlas::jobs::workloads
{
struct benchmark_options
{
    workload_config workload;
    std::size_t workers{4};
    std::size_t repetitions{5};
    std::string machine{"unspecified"};
};
struct benchmark_result
{
    std::vector<std::uint64_t> sequential_ns;
    std::vector<std::uint64_t> parallel_ns;
    std::uint64_t checksum;
    allocation_counts allocations;
    job_trace_report trace;
};
benchmark_options parse_options(int argc, char** argv);
benchmark_result measure(const benchmark_options& options);
void write_report(std::ostream& output, const benchmark_options& options,
                  const benchmark_result& result);
} // namespace atlas::jobs::workloads
