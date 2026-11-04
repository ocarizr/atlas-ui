/** @file @brief Bounded joined tessellation over resolved independent painter ranges. */
#include "paint_state.hpp"
#include <atomic>

namespace atlas::ui::detail
{
/// @brief Cooperative checks preserve frame-local all-or-nothing publication.
void check_paint_cancel(const std::stop_token& stop)
{
    if (stop.stop_requested())
    {
        throw jobs::job_cancelled{};
    }
}

/// @brief No worker performs ordering, registry access, resource loading or GPU submission.
void tessellate(std::span<const paint_input> inputs, std::span<paint_quad> output,
                const std::stop_token& stop)
{
    constexpr std::array<layout_point, 6> corners{{{0, 0}, {1, 0}, {0, 1}, {0, 1}, {1, 0}, {1, 1}}};
    for (std::size_t i = 0; i < inputs.size(); ++i)
    {
        check_paint_cancel(stop);
        const auto& source = inputs[i];
        auto& quad = output[i];
        quad.target = source.target;
        quad.bounds = source.bounds;
        quad.clip = source.clip;
        quad.texture = source.style.texture;
        for (std::size_t v = 0; v < corners.size(); ++v)
        {
            const auto p = corners[v];
            quad.vertices[v] = {{source.bounds.x + p.x * source.bounds.width,
                                 source.bounds.y + p.y * source.bounds.height},
                                {source.style.uv.x + p.x * source.style.uv.width,
                                 source.style.uv.y + p.y * source.style.uv.height},
                                source.style.color};
        }
    }
}

namespace
{
struct interval
{
    std::size_t first; ///< Inclusive candidate index.
    std::size_t last;  ///< Exclusive candidate index.
}; ///< Adjacent ranges preserve painter order without a merge sort.

struct work
{
    std::span<const paint_input> input;        ///< Borrowed until attached completion joins.
    std::span<paint_quad> output;              ///< Exactly one writer per range.
    std::span<const interval> ranges;          ///< Bounded coordinator-created frontier.
    std::stop_token stop;                      ///< Calculation cancellation policy.
    const diagnostics::services* destinations; ///< Retained by engine through join.
    std::atomic<std::size_t> admitted{0}; ///< Counts successful admission under partial failure.
}; ///< One shared frame-local descriptor, not one shared subtree per task.

struct chunk
{
    work* owner;    ///< Coordinator retains storage through join.
    interval range; ///< Exclusive write slice.
}; ///< Capture-free lightweight job payload.

/// @brief Access guards document actual borrowed input and exclusive output regions.
void run_chunk(jobs::job_context& context, chunk& value)
{
    auto& owner = *value.owner;
    const diagnostics::operation_scope scope{*owner.destinations, {"ui.paint.chunk", "ui.cpu"}};
    const auto input = owner.input.subspan(value.range.first, value.range.last - value.range.first);
    const auto output =
        owner.output.subspan(value.range.first, value.range.last - value.range.first);
    auto access = context.access({{input.data(), input.size_bytes(), jobs::access_mode::read},
                                  {output.data(), output.size_bytes(), jobs::access_mode::write}});
    context.throw_if_cancelled();
    tessellate(input, output, owner.stop);
}

/// @brief Attached children finish and release payloads even after a later admission fails.
void run_root(jobs::job_context& context, work*& value)
{
    for (const auto range : value->ranges)
    {
        context.throw_if_cancelled();
        (void)context.spawn({"ui.paint.chunk", "ui.cpu"}, chunk{value, range}, run_chunk);
        value->admitted.fetch_add(1, std::memory_order_relaxed);
    }
}
} // namespace

/// @brief Worker selection is explicit runtime policy, never automatic timing calibration.
void execute_paint(const paint_preparation& prepared, paint_data& output,
                   const jobs::job_dispatcher& dispatcher, paint_limits limits,
                   const jobs::job_options& options, paint_statistics& stats,
                   const diagnostics::services& destinations)
{
    std::pmr::vector<interval> ranges(0, output.memory.get());
    if (limits.execution != paint_execution::sequential && limits.max_chunks > 1 &&
        prepared.inputs.size() >= limits.min_parallel_quads)
    {
        const auto units = limits.execution == paint_execution::roots ? prepared.roots.size()
                                                                      : prepared.inputs.size();
        const auto count = std::min<std::size_t>(limits.max_chunks, units);
        ranges.reserve(count);
        std::size_t begin = 0;
        for (std::size_t i = 1; i <= count; ++i)
        {
            const auto unit_end = units * i / count;
            const auto end = limits.execution == paint_execution::roots
                                 ? prepared.roots[unit_end - 1]
                                 : unit_end;
            ranges.push_back({begin, end});
            begin = end;
        }
    }

    if (ranges.size() < 2)
    {
        tessellate(prepared.inputs, output.values, options.cancellation);
        return;
    }

    work state{prepared.inputs, output.values, ranges, options.cancellation, &destinations};
    try
    {
        const auto completion = dispatcher.with_options(options).run(
            {"ui.paint.prepare", "ui.phase"}, &state, run_root);
        state.admitted.fetch_add(1, std::memory_order_relaxed);
        const diagnostics::operation_scope scope{destinations, {"ui.paint.join", "ui.phase"}};
        completion.wait();
    }
    catch (const jobs::job_error& error)
    {
        stats.admitted_jobs = state.admitted.load(std::memory_order_relaxed);
        if (error.cause())
        {
            std::rethrow_exception(error.cause());
        }

        throw;
    }
    catch (...)
    {
        stats.admitted_jobs = state.admitted.load(std::memory_order_relaxed);
        throw;
    }

    stats.admitted_jobs = state.admitted.load(std::memory_order_relaxed);
}
} // namespace atlas::ui::detail
