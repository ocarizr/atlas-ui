/** @file @brief Dirty-grid CPU frames with serial/job equivalence and memory observations. */
#include "../jobs/allocation_counter.hpp"
#include "layout_probe.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/paint_engine.hpp>
#include <chrono>
#include <iostream>

namespace atlas::ui::benchmarks
{
namespace
{
/// @brief Identical tree shape for both injected scheduling policies.
struct grid_case
{
    unsigned roots; ///< Independent branches.
    unsigned side;  ///< Number of rows and columns per root.
};

/// @brief Keep one prior snapshot/result/packet per policy to include retirement costs.
class grid_probe
{
  public:
    grid_probe(const jobs::job_dispatcher& dispatcher, grid_case scenario, bool parallel)
        : tree({scenario.roots * (scenario.side * scenario.side + 1)}, memory),
          layout(dispatcher, {.min_parallel_nodes = 1, .max_chunks = parallel ? 4U : 1U}, memory),
          paint(dispatcher, {}, memory)
    {
        grid_definition definition;
        definition.rows.assign(scenario.side, {grid_unit::fixed, 16});
        definition.columns.assign(scenario.side, {grid_unit::fixed, 24});
        definition.row_gap = definition.column_gap = 2;
        const grid_layout grid{std::move(definition)};
        for (unsigned root = 0; root < scenario.roots; ++root)
        {
            const auto parent = tree.create();
            tree.set_panel(parent, grid);
            for (unsigned index = 0; index < scenario.side * scenario.side; ++index)
            {
                const auto child = tree.create(parent);
                paint_style visual;
                visual.fill = true;
                tree.set_paint(child, visual);
                if (index == 0)
                {
                    changed.push_back(child);
                }
            }
        }

        tree.set_viewport({0, 0, 1920, 1080});
    } ///< Startup is outside measured frames.

    double sample(unsigned iteration)
    {
        const auto start = std::chrono::steady_clock::now();
        for (auto target : changed)
        {
            auto style = tree.layout(target);
            style.intrinsic.width = 10 + static_cast<float>(iteration % 2);
            tree.set_layout(target, style);
        }

        source = tree.snapshot();
        result = layout.calculate(source);
        packet = paint.calculate(source, result);
        return std::chrono::duration<double, std::micro>{std::chrono::steady_clock::now() - start}
            .count();
    } ///< Include capture, dirty layout, clipping/paint and previous-packet retirement.

    void equivalent(const grid_probe& reference) const
    {
        const auto actual = result.items();
        const auto expected = reference.result.items();
        if (actual.size() != expected.size() ||
            packet.quads().size() != reference.packet.quads().size())
        {
            throw std::runtime_error("grid result size mismatch");
        }

        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            if (actual[i].geometry != expected[i].geometry)
            {
                throw std::runtime_error("grid geometry mismatch");
            }
        }

        for (std::size_t i = 0; i < packet.quads().size(); ++i)
        {
            const auto& a = packet.quads()[i];
            const auto& b = reference.packet.quads()[i];
            if (a.bounds != b.bounds || a.clip != b.clip || a.vertices != b.vertices)
            {
                throw std::runtime_error("grid paint mismatch");
            }
        }
    } ///< Compare ordered numeric values while ignoring different tree session identities.

    std::shared_ptr<probe_resource> memory = std::make_shared<probe_resource>(); ///< Shared PMR.
    element_tree tree;                   ///< Exact live-node capacity.
    layout_engine layout;                ///< Persistent injected layout policy.
    paint_engine paint;                  ///< Common sequential painter.
    std::vector<element_handle> changed; ///< One changed leaf per root.
    element_snapshot source;             ///< Current immutable input.
    layout_snapshot result;              ///< Current numeric result.
    paint_snapshot packet;               ///< Current ordered rectangle packet.
};
} // namespace

/// @brief No timing pass/fail threshold; only exact reference correctness gates the smoke test.
int run_grid_benchmark(bool smoke)
{
    jobs::workloads::verify_allocation_counter();
    jobs::job_system workers{4};
    std::cout << "<grid_benchmark profiling_compiled=\"" << diagnostics::instrumentation_enabled
              << "\">\n";
    for (const grid_case scenario : {grid_case{1, 16}, grid_case{1, 32}, grid_case{4, 32}})
    {
        grid_probe serial{workers.dispatcher(), scenario, false};
        grid_probe candidate{workers.dispatcher(), scenario, true};
        for (unsigned i = 0; i < 3; ++i)
        {
            (void)serial.sample(i);
            (void)candidate.sample(i);
        }

        const unsigned samples = smoke ? 3 : 20;
        std::array<double, 2> total{};
        serial.memory->reset();
        candidate.memory->reset();
        for (unsigned i = 0; i < samples; ++i)
        {
            if (i % 2 == 0)
            {
                total[0] += serial.sample(i);
                total[1] += candidate.sample(i);
            }
            else
            {
                total[1] += candidate.sample(i);
                total[0] += serial.sample(i);
            }

            candidate.equivalent(serial);
        }

        for (unsigned policy = 0; policy < 2; ++policy)
        {
            auto& probe = policy ? candidate : serial;
            jobs::workloads::begin_allocation_counting();
            (void)probe.sample(samples);
            const auto allocations = jobs::workloads::end_allocation_counting();
            std::cout << "  <case roots=\"" << scenario.roots << "\" side=\"" << scenario.side
                      << "\" policy=\"" << (policy ? "forest" : "serial") << "\" nodes=\""
                      << probe.tree.size() << "\" mean_us=\"" << total[policy] / samples
                      << "\" jobs=\"" << probe.layout.statistics().admitted_jobs << "\" pmr_live=\""
                      << probe.memory->live << "\" pmr_peak=\"" << probe.memory->peak
                      << "\" cpp_calls_one_frame=\"" << allocations.calls
                      << "\" cpp_requested_one_frame=\"" << allocations.requested_bytes << "\"/>\n";
        }
    }

    std::cout << "</grid_benchmark>\n";
    return 0;
}
} // namespace atlas::ui::benchmarks
