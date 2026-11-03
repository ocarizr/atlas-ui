/** @file @brief Joined numeric phases over independent dirty roots on the shared pool. */
#include "layout_state.hpp"
#include <algorithm>

namespace atlas::ui
{
/// @brief Every accepted job joins before returning or propagating the original failure.
std::pmr::vector<detail::arrange_output>
layout_engine::impl::evaluate(std::span<const detail::measure_input> pending, layout_rect viewport,
                              const jobs::job_options& options, bool parallel)
{
    std::pmr::vector<detail::measure_output> measured(pending.size(), {}, memory.get());
    std::pmr::vector<detail::arrange_input> arrange_inputs(pending.size(), {}, memory.get());
    std::pmr::vector<detail::arrange_output> arranged(pending.size(), {}, memory.get());
    if (parallel)
    {
        if (frame == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("UI layout phase identities exhausted");
        }

        frames.begin_frame(++frame, options);
        try
        {
            const auto result = frames.run_phase<detail::measure_output>(
                "ui.layout.measure", pending, detail::measure_roots);
            std::copy(result.values().begin(), result.values().end(), measured.begin());
            for (std::size_t index = 0; index < measured.size(); ++index)
            {
                arrange_inputs[index] = {measured[index], viewport};
            }

            const auto output = frames.run_phase<detail::arrange_output>(
                "ui.layout.arrange", std::span<const detail::arrange_input>{arrange_inputs},
                detail::arrange_roots);
            std::copy(output.values().begin(), output.values().end(), arranged.begin());
            frames.end_frame();
            stats.admitted_jobs = frames.statistics().admitted_jobs;
        }
        catch (...)
        {
            const auto state = frames.statistics();
            stats.admitted_jobs = state.admitted_jobs;
            if (!state.failed && !state.closed)
            {
                frames.end_frame();
            }

            throw;
        }
    }
    else if (!pending.empty())
    {
        {
            const diagnostics::operation_scope scope{destinations, {"ui.layout.measure", "ui.cpu"}};
            detail::measure_roots({pending, measured, 0, options.cancellation});
        }
        for (std::size_t index = 0; index < measured.size(); ++index)
        {
            arrange_inputs[index] = {measured[index], viewport};
        }

        {
            const diagnostics::operation_scope scope{destinations, {"ui.layout.arrange", "ui.cpu"}};
            detail::arrange_roots({arrange_inputs, arranged, 0, options.cancellation});
        }
    }

    return arranged;
}
} // namespace atlas::ui
