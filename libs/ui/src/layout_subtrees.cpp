/** @file @brief One bounded attached frontier joins before coordinator-only ancestor reduction. */
#include "layout_partition.hpp"
#include <atomic>

namespace atlas::ui::detail
{
namespace
{
struct measure_work
{
    measured_branch*
        result; ///< Owner retains the one mutable output until the complete root joins.
    std::span<const measure_interval> ranges;  ///< Stable bounded disjoint intervals.
    std::stop_token stop;                      ///< Shared cooperative calculation token.
    const diagnostics::services* destinations; ///< Owner retains optional diagnostic services.
    std::atomic<std::size_t> admitted{0};      ///< Actual accepted root/child task count.
}; ///< Borrowed descriptors do not copy the tree or retain per-job shared_ptr control blocks.

/// @brief One capture-free exclusive range descriptor, retained through attached completion.
struct measure_chunk
{
    measure_work* owner;       ///< Borrowed through attached root lifetime.
    measure_interval interval; ///< Exclusive desired-output interval.
};

/// @brief All child dependencies are inside this complete forest; no worker waits on another.
void run_measure_chunk(jobs::job_context& context, measure_chunk& chunk)
{
    const auto& work = *chunk.owner;
    const diagnostics::operation_scope scope{*work.destinations, {"ui.layout.subtree", "ui.cpu"}};
    const auto nodes = std::span{work.result->source->nodes};
    const auto output = std::span{work.result->desired}.subspan(
        chunk.interval.first, chunk.interval.last - chunk.interval.first);
    auto access = context.access(
        {jobs::memory_access{nodes.data(), nodes.size_bytes(), jobs::access_mode::read},
         jobs::memory_access{output.data(), output.size_bytes(), jobs::access_mode::write}});
    context.throw_if_cancelled();
    measure_nodes(*work.result, output, work.stop);
}

/// @brief Partial admission failure still joins every accepted child through the root handle.
void run_measure_root(jobs::job_context& context, measure_work*& work)
{
    for (const auto interval : work->ranges)
    {
        context.throw_if_cancelled();
        (void)context.spawn({"ui.layout.subtree", "ui.cpu"}, measure_chunk{work, interval},
                            run_measure_chunk);
        work->admitted.fetch_add(1, std::memory_order_relaxed);
    }
}

/// @brief Coordinator wait only: original child/admission failures propagate after attached
/// cleanup.
void join_measure(measure_work& work, const jobs::job_dispatcher& dispatcher,
                  const jobs::job_options& options)
{
    try
    {
        auto completion = dispatcher.with_options(options).run({"ui.layout.measure", "ui.phase"},
                                                               &work, run_measure_root);
        work.admitted.fetch_add(1, std::memory_order_relaxed);
        const diagnostics::operation_scope scope{*work.destinations,
                                                 {"ui.layout.join", "ui.phase"}};
        completion.wait();
    }
    catch (const jobs::job_error& error)
    {
        if (error.cause())
        {
            std::rethrow_exception(error.cause());
        }

        throw;
    }
}
} // namespace

/// @brief The frontier is a nonblocking dependency cut; ancestors read children only after join.
measure_output measure_subtrees(measure_input source, const jobs::job_dispatcher& dispatcher,
                                const layout_limits& limits, const jobs::job_options& options,
                                layout_statistics& stats, const diagnostics::services& destinations)
{
    const auto plan = [&]
    {
        const diagnostics::operation_scope scope{destinations, {"ui.layout.plan", "ui.cpu"}};
        return partition_measure(*source, limits, options.cancellation);
    }();
    stats.scheduling_bytes = plan.ends.capacity() * sizeof(std::uint32_t) +
                             plan.ranges.capacity() * sizeof(measure_interval);
    if (plan.ranges.size() < 2)
    {
        return measure_layout(std::move(source), options.cancellation);
    }

    auto result = std::make_shared<measured_branch>(std::move(source));
    stats.subtree_ranges = plan.ranges.size();
    for (const auto range : plan.ranges)
    {
        stats.subtree_nodes += range.last - range.first;
    }

    measure_work work{result.get(), plan.ranges, options.cancellation, &destinations};
    try
    {
        join_measure(work, dispatcher, options);
    }
    catch (...)
    {
        stats.admitted_jobs += work.admitted.load(std::memory_order_relaxed);
        throw;
    }

    stats.admitted_jobs += work.admitted.load(std::memory_order_relaxed);
    const diagnostics::operation_scope scope{destinations, {"ui.layout.measure.parents", "ui.cpu"}};
    auto range = plan.ranges.rbegin();
    for (auto position = result->source->nodes.size(); position != 0;)
    {
        if (range != plan.ranges.rend() && position == range->last)
        {
            position = range->first;
            ++range;
        }
        else
        {
            measure_node(*result, --position, options.cancellation);
        }
    }

    return result;
}
} // namespace atlas::ui::detail
