/** @file @brief Offset-only layout/paint cost and PMR requested bytes without timing gates. */
#include "layout_probe.hpp"
#include <algorithm>
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/paint_engine.hpp>
#include <chrono>
#include <iostream>
#include <vector>

namespace atlas::ui::benchmarks
{
namespace
{
/// @brief One deterministic list with all content retained; this is not virtualization.
class scroll_probe
{
  public:
    explicit scroll_probe(const jobs::job_dispatcher& dispatcher, unsigned count)
        : tree({count + 2}), layout(dispatcher, {}, memory), paint(dispatcher, {}, memory)
    {
        viewport = tree.create();
        tree.set_panel(viewport, scroll_layout{});
        const auto stack = tree.create(viewport);
        tree.set_panel(stack, vertical_layout{2});
        for (unsigned index = 0; index < count; ++index)
        {
            const auto child = tree.create(stack);
            tree.set_panel(child, leaf_layout{});
            layout_style style;
            style.intrinsic = {100, 18};
            tree.set_layout(child, style);
            paint_style visual;
            visual.fill = true;
            tree.set_paint(child, visual);
        }

        tree.set_viewport({0, 0, 800, 600});
        result = layout.calculate(tree.snapshot());
    } ///< Startup is outside measured update intervals.

    double sample(unsigned iteration)
    {
        const auto start = std::chrono::steady_clock::now();
        tree.set_panel(viewport,
                       scroll_layout{false, true, {0, static_cast<float>(iteration % 200)}});
        const auto source = tree.snapshot();
        result = layout.calculate(source);
        const auto output = paint.calculate(source, result);
        const auto end = std::chrono::steady_clock::now();
        if (layout.statistics().measured_nodes != 0 || layout.statistics().arranged_nodes != 0 ||
            result.scrolls().size() != 1 || output.quads().empty())
        {
            throw std::runtime_error("scroll benchmark invalidated numeric layout or lost paint");
        }

        return std::chrono::duration<double, std::micro>{end - start}.count();
    } ///< Include mutation, capture, cached calculation and paint preparation in wall time.

    std::shared_ptr<probe_resource> memory = std::make_shared<probe_resource>(); ///< Shared PMR.
    element_tree tree;       ///< Owns the complete unvirtualized list.
    layout_engine layout;    ///< Numeric cache uses the application's shared pool.
    paint_engine paint;      ///< Default sequential painter, without artificial job admission.
    element_handle viewport; ///< Single scroll owner.
    layout_snapshot result;  ///< Keep one consumer packet across frames.
};
} // namespace

/// @brief Report local observations, never a hardware-dependent CI pass/fail threshold.
int run_scroll_benchmark(bool smoke)
{
    jobs::job_system pool{2};
    std::cout << "<scroll_benchmark profiling_compiled=\"" << diagnostics::instrumentation_enabled
              << "\">\n";
    for (const unsigned count : {32U, 1024U, 8192U})
    {
        scroll_probe probe{pool.dispatcher(), count};
        for (unsigned iteration = 0; iteration < 3; ++iteration)
        {
            (void)probe.sample(iteration);
        }

        std::vector<double> samples;
        samples.reserve(smoke ? 3U : 30U);
        for (unsigned iteration = 0; iteration < (smoke ? 3U : 30U); ++iteration)
        {
            samples.push_back(probe.sample(iteration + 3));
        }

        std::sort(samples.begin(), samples.end());
        probe.memory->reset();
        const auto retained = probe.memory->live.load();
        (void)probe.sample(100);
        std::cout << "  <case nodes=\"" << count + 2 << "\" update_p50_us=\""
                  << samples[(samples.size() - 1) / 2] << "\" update_p95_us=\""
                  << samples[(samples.size() - 1) * 95 / 100] << "\" pmr_calls=\""
                  << probe.memory->calls.load() << "\" retained_pmr_bytes=\"" << retained
                  << "\" peak_pmr_bytes=\"" << probe.memory->peak.load()
                  << "\" scroll_item_bytes=\"" << sizeof(scroll_item) << "\"/>\n";
    }

    std::cout << "</scroll_benchmark>\n";
    return 0;
}
} // namespace atlas::ui::benchmarks
