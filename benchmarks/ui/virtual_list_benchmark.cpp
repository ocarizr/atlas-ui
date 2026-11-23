/** @file @brief Bounded virtual-list end-to-end latency and requested-byte census. */
#include "../jobs/allocation_counter.hpp"
#include "layout_probe.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/paint_engine.hpp>
#include <atlas/ui/virtual_list.hpp>
#include <chrono>
#include <iostream>

namespace atlas::ui::benchmarks
{
namespace
{
/// @brief One list with fixed widget bounds, source size and deterministic rapid scroll directions.
class virtual_probe
{
  public:
    virtual_probe(const jobs::job_dispatcher& dispatcher, std::size_t count)
        : tree(tree_limits{256}, memory), input(tree, {}, memory), layout(dispatcher, {}, memory),
          paint(dispatcher, {}, memory),
          source(std::make_shared<fixed_item_source>(fixed_item_options{count, 20})),
          viewport(tree.create()), list(
                                       tree, input, viewport_setup(), dispatcher,
                                       [](row_context& row)
                                       {
                                           paint_style visual;
                                           visual.fill = true;
                                           row.tree().set_paint(row.root(), visual);
                                       },
                                       {}, memory)
    {
        tree.set_viewport({0, 0, 800, 600});
        list.update(source, 600);
    } ///< Startup and logical source construction stay outside the sampled frame.

    element_handle viewport_setup()
    {
        tree.set_panel(viewport, scroll_layout{});
        return viewport;
    } ///< Bind a portable scroll owner before constructing the realization controller.

    double sample(unsigned iteration)
    {
        const auto start = std::chrono::steady_clock::now();
        const float position = iteration % 2 == 0 ? 2000.0F : 4000.0F;
        tree.set_panel(viewport, scroll_layout{false, true, {0, position}});
        list.update(source, 600);
        const auto snapshot = tree.snapshot();
        const auto geometry = layout.calculate(snapshot);
        const auto painted = paint.calculate(snapshot, geometry);
        input.begin_frame(++frame, snapshot, geometry);
        input.process({});
        input.tick();
        input.late_tick();
        input.end_frame();
        tree.apply(tree.commands().seal());
        if (painted.quads().size() > 30 || tree.size() > 36 || list.statistics().admitted_jobs ||
            list.rows().size() > 34)
        {
            throw std::runtime_error("virtualization exceeded deterministic realization bounds");
        }

        return std::chrono::duration<double, std::micro>{std::chrono::steady_clock::now() - start}
            .count();
    } ///< Include realization, capture, layout, paint, input and cleanup, excluding
      ///< presentation/GPU.

    std::shared_ptr<probe_resource> memory = std::make_shared<probe_resource>(); ///< Shared census.
    element_tree tree;                         ///< Fixed 256-slot native-free tree.
    input_session input;                       ///< Retired bindings cleaned at every sampled frame.
    layout_engine layout;                      ///< Host layout pipeline over realized nodes only.
    paint_engine paint;                        ///< Shared clipping and ordered paint.
    std::shared_ptr<const item_source> source; ///< Constant-storage fixed-height logical source.
    element_handle viewport;                   ///< Scroll owner.
    virtual_list list;                         ///< Bounded shared-dispatcher controller.
    std::uint64_t frame = 0;                   ///< Increasing input frame identity.
};
} // namespace

/// @brief Report hardware-local timings and separate PMR/global requests; never impose timing
/// gates.
int run_virtual_benchmark(bool smoke)
{
    jobs::job_system pool{2};
    jobs::workloads::verify_allocation_counter();
    std::cout << "<virtual_list_benchmark profiling_compiled=\""
              << diagnostics::instrumentation_enabled << "\">\n";
    for (const std::size_t count : {1000U, 100000U, 10000000U})
    {
        virtual_probe probe{pool.dispatcher(), count};
        for (unsigned i = 0; i < 3; ++i)
        {
            (void)probe.sample(i);
        }

        probe.memory->reset();
        jobs::workloads::begin_allocation_counting();
        double total = 0;
        const unsigned samples = smoke ? 3 : 30;
        for (unsigned i = 0; i < samples; ++i)
        {
            total += probe.sample(i);
        }

        const auto allocations = jobs::workloads::end_allocation_counting();
        std::cout << "  <case logical=\"" << count << "\" realized=\"" << probe.list.rows().size()
                  << "\" nodes=\"" << probe.tree.size() << "\" mean_us=\"" << total / samples
                  << "\" pmr_live=\"" << probe.memory->live << "\" pmr_peak=\""
                  << probe.memory->peak << "\" pmr_calls=\"" << probe.memory->calls
                  << "\" cpp_calls=\"" << allocations.calls << "\" cpp_requested=\""
                  << allocations.requested_bytes << "\" jobs=\""
                  << probe.list.statistics().admitted_jobs << "\"/>\n";
    }

    std::cout << "</virtual_list_benchmark>\n";
    return 0;
}
} // namespace atlas::ui::benchmarks
