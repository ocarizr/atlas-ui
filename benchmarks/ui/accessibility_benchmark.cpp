/** @file @brief Full semantic capture/publication latency and separate allocation census. */
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
/// @brief No application allocation or behavior is attributed to publication.
class accessible_handler final : public input_handler
{
  public:
    event_response on_event(const routed_event&, command_buffer&) override
    {
        return {};
    } ///< No-op.
};

/// @brief Fixed tree/pool startup is outside the capture/publication measurement.
class accessibility_probe
{
  public:
    accessibility_probe(const jobs::job_dispatcher& workers, unsigned count)
        : tree({count + 1}), layout(workers), input(tree), count_(count)
    {
        const auto root = tree.create();
        auto handler = std::make_shared<accessible_handler>();
        for (unsigned i = 0; i < count; ++i)
        {
            const auto node = tree.create(root);
            input.bind(node, handler, {.focusable = true});
            input.configure_accessibility(
                node,
                {.role = accessibility::role::button,
                 .name = "Accessible benchmark control with owned text " + std::to_string(i)});
        }

        tree.set_viewport({0, 0, 1024, 768});
        const auto source = tree.snapshot();
        input.begin_frame(1, source, layout.calculate(source));
        input.process({});
    } ///< Reuse the same allocator/configured controls throughout the sample.

    void step()
    {
        output.publish(input.accessibility_nodes());
        const auto view = output.read();
        if (!view || view->nodes().size() != count_ || view->nodes().front().name.empty())
        {
            throw std::runtime_error("accessibility publication reference mismatch");
        }
    } ///< Force a complete owner publication, not native COM querying or application callbacks.

    element_tree tree;             ///< Fixed owner capacity.
    layout_engine layout;          ///< Only startup layout may use the shared pool.
    input_session input;           ///< Frozen presented geometry.
    accessibility::channel output; ///< Previous publication retires on owner replacement.
    unsigned count_;               ///< Expected semantic node count.
};
} // namespace

/// @brief Observational timing has no brittle cross-machine speed threshold.
int run_accessibility_benchmark(bool smoke)
{
    jobs::workloads::verify_allocation_counter();
    jobs::job_system workers{2};
    std::cout << "<accessibility_benchmark profiling_compiled=\""
              << diagnostics::instrumentation_enabled << "\">\n";
    for (unsigned count : {64U, 1024U})
    {
        accessibility_probe probe{workers.dispatcher(), count};
        for (unsigned i = 0; i < 8; ++i)
        {
            probe.step();
        }

        const unsigned samples = smoke ? 4 : 100;
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < samples; ++i)
        {
            probe.step();
        }

        const auto elapsed =
            std::chrono::duration<double, std::micro>{std::chrono::steady_clock::now() - start}
                .count();
        jobs::workloads::begin_allocation_counting();
        probe.step();
        const auto census = jobs::workloads::end_allocation_counting();
        std::cout << "  <case nodes=\"" << count << "\" mean_us=\"" << elapsed / samples
                  << "\" cpp_calls=\"" << census.calls << "\" requested_bytes=\""
                  << census.requested_bytes << "\"/>\n";
        probe.output.close();
        probe.input.tick();
        probe.input.late_tick();
        probe.input.end_frame();
        probe.tree.apply(probe.tree.commands().seal());
    }

    std::cout << "</accessibility_benchmark>\n";
    return 0;
}
} // namespace atlas::ui::benchmarks
