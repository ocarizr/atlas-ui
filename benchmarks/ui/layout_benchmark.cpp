/** @file @brief Alternating real-layout samples and separate PMR/C++ allocation censuses. */
#include "../jobs/allocation_counter.hpp"
#include "layout_probe.hpp"
#include <algorithm>
#include <array>
#include <atlas/jobs/job_system.hpp>
#include <chrono>
#include <iostream>
#include <sstream>
#include <vector>

namespace atlas::ui::benchmarks
{
namespace
{
using clock = std::chrono::steady_clock; ///< All times are coordinator wall microseconds.

/// @brief One retained wall-duration sample with independently visible coordinator components.
struct layout_sample
{
    double frame;     ///< Mutation, capture, calculation and publication.
    double capture;   ///< Immutable snapshot construction.
    double calculate; ///< Preparation, kernels, joining and cache commit.
    double publish;   ///< Live-tree revision validation and publication.
    double retire;    ///< Previous consumer result and temporary input release.
};

/// @brief Preserve exactly one output; geometry comparison and report formatting are untimed.
layout_sample frame(layout_probe& probe, std::size_t iteration)
{
    const auto start = clock::now();
    if (!probe.unchanged)
    {
        for (const auto target : probe.changes)
        {
            auto style = probe.tree.layout(target);
            style.intrinsic.width = static_cast<float>(20 + iteration % 2);
            probe.tree.set_layout(target, style);
        }
    }

    const auto capture = clock::now();
    clock::time_point calculate{};
    clock::time_point publish{};
    clock::time_point published{};
    {
        const auto input = probe.tree.snapshot();
        calculate = clock::now();
        const auto result = probe.engine.calculate(input);
        publish = clock::now();
        probe.tree.publish_layout(result);
        published = clock::now();
        probe.last = result;
    }

    const auto end = clock::now();
    const auto us = [](auto duration)
    {
        return std::chrono::duration<double, std::micro>{duration}.count();
    };
    return {us(end - start), us(calculate - capture), us(publish - calculate),
            us(published - publish), us(end - published)};
}

/// @brief Sessions differ, so compare corresponding numeric geometry rather than handle bytes.
void equivalent(const layout_probe& reference, const layout_probe& candidate)
{
    const auto first = reference.last.items();
    const auto second = candidate.last.items();
    if (first.size() != second.size() || !std::equal(first.begin(), first.end(), second.begin(),
                                                     [](const auto& left, const auto& right)
                                                     {
                                                         return left.geometry == right.geometry;
                                                     }))
    {
        throw std::runtime_error("layout benchmark geometry differs across policies");
    }
}

/// @brief Retain all samples and select the lower discrete percentile without dropping outliers.
double percentile(const std::vector<layout_sample>& samples, std::size_t percent,
                  double layout_sample::* field)
{
    std::vector<double> values;
    values.reserve(samples.size());
    for (const auto& sample : samples)
    {
        values.push_back(sample.*field);
    }

    std::sort(values.begin(), values.end());
    return values[(values.size() - 1) * percent / 100];
}

/// @brief Allocation counting is untimed and excludes pool/tree/engine startup and report output.
void report(layout_probe& probe, jobs::job_system& pool, std::string_view name,
            const std::vector<layout_sample>& samples)
{
    const auto retained = probe.memory->live.load();
    probe.memory->reset();
    const auto census_iteration =
        probe.tree.layout(probe.changed).intrinsic.width == 20 ? 101U : 100U;
    jobs::workloads::begin_allocation_counting();
    try
    {
        frame(probe, census_iteration);
        pool.wait_idle();
    }
    catch (...)
    {
        (void)jobs::workloads::end_allocation_counting();
        throw;
    }

    const auto allocations = jobs::workloads::end_allocation_counting();
    const auto stats = probe.engine.statistics();
    const auto task_bound = stats.admitted_jobs == 0
                                ? 0
                                : (stats.subtree_ranges != 0
                                       ? stats.subtree_ranges + 1
                                       : std::min(probe.roots, std::size_t{probe.max_chunks}) + 1);
    std::cout << "    <policy name=\"" << name << "\" frame_p50_us=\""
              << percentile(samples, 50, &layout_sample::frame) << "\" frame_p95_us=\""
              << percentile(samples, 95, &layout_sample::frame) << "\" capture_p50_us=\""
              << percentile(samples, 50, &layout_sample::capture) << "\" calculate_p50_us=\""
              << percentile(samples, 50, &layout_sample::calculate) << "\" publish_p50_us=\""
              << percentile(samples, 50, &layout_sample::publish) << "\" pmr_retained_before=\""
              << retained << "\" pmr_peak=\"" << probe.memory->peak.load()
              << "\" pmr_retained_after=\"" << probe.memory->live.load()
              << "\" pmr_allocation_calls=\"" << probe.memory->calls.load()
              << "\" cpp_allocation_calls=\"" << allocations.calls << "\" cpp_requested_bytes=\""
              << allocations.requested_bytes << "\" jobs=\"" << stats.admitted_jobs
              << "\" subtree_ranges=\"" << stats.subtree_ranges << "\" subtree_nodes=\""
              << stats.subtree_nodes << "\" planner_bytes=\"" << stats.scheduling_bytes
              << "\" scope_task_upper_bound=\"" << task_bound << "\" retire_p50_us=\""
              << percentile(samples, 50, &layout_sample::retire) << "\">\n";
    for (const auto& sample : samples)
    {
        std::cout << "      <sample frame_us=\"" << sample.frame << "\" capture_us=\""
                  << sample.capture << "\" calculate_us=\"" << sample.calculate
                  << "\" publish_us=\"" << sample.publish << "\" retire_us=\"" << sample.retire
                  << "\"/>\n";
    }

    std::cout << "    </policy>\n";
}

/// @brief Emit each case before clearing capture; setup events never consume the measured budget.
void report_trace(const std::shared_ptr<diagnostics::profiler>& capture)
{
    if (!capture)
    {
        return;
    }

    const auto stats = capture->statistics();
    if (stats.dropped != 0 || stats.errors != 0)
    {
        throw std::runtime_error("layout benchmark profiling capture is incomplete");
    }

    std::ostringstream trace;
    capture->write_chrome_trace(trace);
    std::cout << "    <trace><![CDATA[" << trace.str() << "]]></trace>\n";
}
} // namespace

/// @brief Fine-panel comparison stays under the same 64-child cap; it is not unbounded fan-out.
int run_layout_benchmark(bool smoke, bool profile)
{
    if (profile && !diagnostics::instrumentation_enabled)
    {
        throw std::invalid_argument(
            "layout profiling requires ATLAS_PROFILING=on or a profiling-enabled configuration");
    }

    jobs::workloads::verify_allocation_counter();
    jobs::job_system pool{4};
    const auto capture = profile ? std::make_shared<diagnostics::profiler>() : nullptr;
    const diagnostics::services services{{}, capture};
    const std::array cases{layout_case{"small", 8, 8},
                           layout_case{"wide", 32, 256},
                           layout_case{"large", 32, 1024},
                           layout_case{"nested_related", 16, 256, 16, 1, true},
                           layout_case{"deep_narrow", 1, 16, 4096},
                           layout_case{"many_roots", 4, 256, 0, 8},
                           layout_case{"cached", 16, 64, 0, 1, false, true}};
    const std::array policies{layout_limits{65536, 1, 1, 1}, layout_limits{},
                              layout_limits{65536, 4096, 16, 1024}, layout_limits{65536, 1, 64, 1}};
    const std::array names{"serial", "default", "bounded", "panel_fine"};
    std::cout << "<ui_layout_benchmark workers=\"4\" hardware_threads=\""
              << std::thread::hardware_concurrency() << "\" samples=\"" << (smoke ? 1 : 20)
              << "\" profiling_compiled=\"" << diagnostics::instrumentation_enabled
              << "\" capture_enabled=\"" << profile << "\" pointer_bits=\"" << sizeof(void*) * 8
              << "\" compiler=\"" << ATLAS_BENCHMARK_COMPILER << "\" system=\""
              << ATLAS_BENCHMARK_SYSTEM << "\" jobs_checker=\"" << pool.sanitizer_report().enabled
#ifdef NDEBUG
              << "\" configuration=\"Release"
#else
              << "\" configuration=\"Debug"
#endif
              << "\">\n";
    for (const auto scenario : cases)
    {
        if (smoke && std::string_view{scenario.name} != "small")
        {
            continue;
        }

        std::array<std::unique_ptr<layout_probe>, 4> probes;
        std::array<std::vector<layout_sample>, 4> samples;
        for (std::size_t policy = 0; policy < probes.size(); ++policy)
        {
            probes[policy] = std::make_unique<layout_probe>(pool.dispatcher(), scenario,
                                                            policies[policy], services);
        }

        if (capture)
        {
            capture->clear();
        }

        for (std::size_t policy = 0; policy < probes.size(); ++policy)
        {
            for (std::size_t warmup = 0; warmup < 3; ++warmup)
            {
                frame(*probes[policy], warmup);
            }
        }

        for (std::size_t iteration = 0; iteration < (smoke ? 1U : 20U); ++iteration)
        {
            for (std::size_t offset = 0; offset < probes.size(); ++offset)
            {
                const auto policy = (iteration + offset) % probes.size();
                samples[policy].push_back(frame(*probes[policy], iteration + 3));
            }

            for (std::size_t policy = 1; policy < probes.size(); ++policy)
            {
                equivalent(*probes[0], *probes[policy]);
            }
        }

        std::cout << "  <case name=\"" << scenario.name << "\" nodes=\"" << probes[0]->tree.size()
                  << "\">\n";
        for (std::size_t policy = 0; policy < probes.size(); ++policy)
        {
            report(*probes[policy], pool, names[policy], samples[policy]);
        }

        report_trace(capture);
        std::cout << "  </case>\n";
    }

    std::cout << "</ui_layout_benchmark>\n";
    return 0;
}
} // namespace atlas::ui::benchmarks
