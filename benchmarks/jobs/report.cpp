/**
 * @file
 * @brief XML serialization of workload provenance, samples and bounded traces.
 */

#include "benchmark.hpp"

#include <algorithm>
#include <array>
#include <ostream>
#include <thread>
#include <unordered_set>

namespace atlas::jobs::workloads
{
namespace
{
/// @brief Escape text for the benchmark's XML report without opening storage.
std::string xml(std::string_view value)
{
    std::string result;
    for (const auto character : value)
    {
        switch (character)
        {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            case '\'':
                result += "&apos;";
                break;
            case '\n':
                result += "&#10;";
                break;
            case '\r':
                result += "&#13;";
                break;
            case '\t':
                result += "&#9;";
                break;
            default:
                result += character;
                break;
        }
    }

    return result;
}

/// @brief Select the middle measured duration without treating it as a portable performance
/// guarantee.
std::uint64_t median(const std::vector<std::uint64_t>& values)
{
    auto ordered = values;
    std::ranges::sort(ordered);
    return ordered[ordered.size() / 2];
}

/// @brief Serialize sequential/parallel sample durations into the benchmark XML report.
void write_samples(std::ostream& output, const benchmark_result& result)
{
    output << "<samples>\n";
    for (std::size_t index = 0; index < result.sequential_ns.size(); ++index)
    {
        output << "<sample sequential_ns=\"" << result.sequential_ns[index] << "\" parallel_ns=\""
               << result.parallel_ns[index] << "\"/>\n";
    }

    output << "</samples>\n";
}

/// @brief Serialize one task's identity, outcome and execution/join timing.
void write_task(std::ostream& output, const task_trace& task)
{
    output << "<task id=\"" << task.id << "\" parent_id=\"" << task.parent_id << "\" name=\""
           << xml(task.name) << "\" category=\"" << xml(task.category) << "\" worker=\""
           << task.worker_index << "\" executor=\"" << (task.caller_thread ? "caller" : "worker")
           << "\" affinity_bound=\"" << (task.affinity_bound ? "true" : "false") << "\" status=\""
           << static_cast<unsigned>(task.status) << "\" priority=\""
           << static_cast<unsigned>(task.priority) << "\" dispatch_index=\"" << task.dispatch_index
           << "\" submitted_ns=\"" << task.submitted.count() << "\" started_ns=\""
           << task.started.count() << "\" body_finished_ns=\"" << task.body_finished.count()
           << "\" completed_ns=\"" << task.completed.count() << "\" scratch_calls=\""
           << task.scratch.allocation_calls << "\" scratch_reserved_bytes=\""
           << task.scratch.reserved_bytes << "\">\n";
    for (const auto prerequisite : task.prerequisites)
    {
        output << "<prerequisite id=\"" << prerequisite << "\"/>\n";
    }

    output << "</task>\n";
}

/// @brief Serialize bounded task trace metadata into the workload report.
void write_trace(std::ostream& output, const job_trace_report& trace)
{
    std::unordered_set<std::uint64_t> parents;
    for (const auto& task : trace.tasks)
    {
        parents.insert(task.parent_id);
    }

    std::vector<std::uint64_t> busy(trace.worker_count);
    std::array<std::uint64_t, 3> critical{};
    std::chrono::nanoseconds first = std::chrono::nanoseconds::max();
    std::chrono::nanoseconds last{};
    for (const auto& task : trace.tasks)
    {
        const auto execution =
            static_cast<std::uint64_t>((task.body_finished - task.started).count());
        if (!task.caller_thread)
        {
            busy.at(task.worker_index) += execution;
        }

        first = std::min(first, task.submitted);
        last = std::max(last, task.completed);
        if (!task.caller_thread && !parents.contains(task.id))
        {
            const auto category = task.category == "map" ? 0U : task.category == "fold" ? 1U : 2U;
            critical[category] = std::max(critical[category], execution);
        }
    }

    output << "<profile elapsed_ns=\"" << (last - first).count()
           << "\" leaf_critical_path_lower_bound_ns=\"" << critical[0] + critical[1] + critical[2]
           << "\" dropped=\"" << trace.dropped << "\" pending=\"" << trace.pending << "\">\n";
    for (std::size_t index = 0; index < busy.size(); ++index)
    {
        output << "<worker index=\"" << index << "\" callback_ns=\"" << busy[index] << "\"/>\n";
    }

    for (const auto& task : trace.tasks)
    {
        write_task(output, task);
    }

    output << "</profile>\n";
}
} // namespace

/// @brief Write benchmark measurements, provenance, allocations and bounded trace metadata as XML.
void write_report(std::ostream& output, const benchmark_options& options,
                  const benchmark_result& result)
{
    output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<job_benchmark schema_version=\"1\" "
              "machine=\""
           << xml(options.machine) << "\" compiler=\"" << ATLAS_BENCHMARK_COMPILER << "\" system=\""
           << ATLAS_BENCHMARK_SYSTEM << "\" configuration=\"" << ATLAS_BENCHMARK_CONFIGURATION
           << "\" hardware_threads=\"" << std::thread::hardware_concurrency()
           << "\" job_sanitizer=\"" << ATLAS_JOBS_SANITIZER << "\" size=\"" << options.workload.size
           << "\" grain=\"" << options.workload.grain << "\" rounds=\"" << options.workload.rounds
           << "\" seed=\"" << options.workload.seed << "\" workers=\"" << options.workers
           << "\" checksum=\"" << result.checksum << "\">\n";
    output << "<summary sequential_median_ns=\"" << median(result.sequential_ns)
           << "\" parallel_median_ns=\"" << median(result.parallel_ns) << "\" allocation_calls=\""
           << result.allocations.calls << "\" allocation_requested_bytes=\""
           << result.allocations.requested_bytes << "\"/>\n";
    write_samples(output, result);
    write_trace(output, result.trace);
    output << "</job_benchmark>\n";
}
} // namespace atlas::jobs::workloads
