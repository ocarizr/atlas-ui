/** @file @brief Automatic/explicit focus-request latency and allocation census after configuration.
 */
#include "../jobs/allocation_counter.hpp"
#include "layout_probe.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/input_session.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <chrono>
#include <iostream>

namespace atlas::ui::benchmarks
{
namespace
{
/// @brief A no-op callback avoids attributing application event-recording allocations to
/// navigation.
class focus_handler final : public input_handler
{
  public:
    /// @brief No-op application handler keeps the allocation census scoped to library work.
    event_response on_event(const routed_event&, command_buffer&) override
    {
        return {};
    }

    ///< No extra application work or activation is introduced by this measurement.
};

/// @brief Each policy owns independent tree/session state and shares only the application pool.
class focus_probe
{
  public:
    focus_probe(const jobs::job_dispatcher& dispatcher, unsigned side, bool explicit_edges)
        : tree({side * side + 1}), layout(dispatcher), input(tree)
    {
        const auto root = tree.create();
        grid_definition grid;
        grid.columns.assign(side, {grid_unit::fixed, 16});
        tree.set_panel(root, grid_layout{std::move(grid)});
        auto handler = std::make_shared<focus_handler>();
        for (unsigned i = 0; i < side * side; ++i)
        {
            const auto node = tree.create(root);
            layout_style style;
            style.intrinsic = {16, 16};
            tree.set_layout(node, style);
            input.bind(node, handler, {.focusable = true});
            if (i == 0)
            {
                first = node;
            }
            else if (i == 1)
            {
                second = node;
            }
        }

        if (explicit_edges)
        {
            focus_navigation_options options;
            options.destinations[1] = second;
            input.configure_navigation(first, options);
            options = {};
            options.destinations[0] = first;
            input.configure_navigation(second, options);
        }

        tree.set_viewport({0, 0, 1024, 1024});
        const auto source = tree.snapshot();
        input.begin_frame(1, source, layout.calculate(source));
        input.process({});
        input.focus(first);
    } ///< Startup, geometry preparation and callback registration are outside the timing window.

    void step(unsigned iteration)
    {
        if (!input.navigate(iteration % 2 ? focus_direction::left : focus_direction::right) ||
            input.focused() != (iteration % 2 ? first : second))
        {
            throw std::runtime_error("focus reference mismatch");
        }
    } ///< Both policies must produce the same requested focus sequence.

    element_tree tree;                ///< Exact capacity, reused for all observations.
    layout_engine layout;             ///< Injected pool, with geometry prepared once.
    input_session input;              ///< Persistent owner interaction metadata.
    element_handle first{}, second{}; ///< Alternating expected targets.
};
} // namespace

/// @brief No speed threshold; warmed navigation must not allocate with no-op handlers/destinations.
int run_focus_benchmark(bool smoke)
{
    jobs::workloads::verify_allocation_counter();
    jobs::job_system workers{2};
    std::cout << "<focus_benchmark profiling_compiled=\"" << diagnostics::instrumentation_enabled
              << "\">\n";
    for (unsigned side : {8U, 32U})
    {
        for (bool explicit_edges : {false, true})
        {
            focus_probe probe{workers.dispatcher(), side, explicit_edges};
            for (unsigned i = 0; i < 32; ++i)
            {
                probe.step(i);
            }

            const unsigned samples = smoke ? 16 : 200;
            const auto visits = probe.input.statistics().navigation_candidates;
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < samples; ++i)
            {
                probe.step(i);
            }

            const auto elapsed =
                std::chrono::duration<double, std::micro>{std::chrono::steady_clock::now() - start}
                    .count();
            const auto scanned = probe.input.statistics().navigation_candidates - visits;
            jobs::workloads::begin_allocation_counting();
            probe.step(0);
            probe.step(1);
            const auto allocations = jobs::workloads::end_allocation_counting();
            if (allocations.calls)
            {
                throw std::runtime_error("warmed focus navigation allocated");
            }

            std::cout << "  <case focusable=\"" << side * side << "\" policy=\""
                      << (explicit_edges ? "explicit" : "automatic") << "\" mean_us=\""
                      << elapsed / samples << "\" candidates_per_request=\"" << scanned / samples
                      << "\" cpp_calls_two_requests=\"" << allocations.calls << "\"/>\n";
            probe.input.tick();
            probe.input.late_tick();
            probe.input.end_frame();
            probe.tree.apply(probe.tree.commands().seal());
        }
    }

    std::cout << "</focus_benchmark>\n";
    return 0;
}
} // namespace atlas::ui::benchmarks
