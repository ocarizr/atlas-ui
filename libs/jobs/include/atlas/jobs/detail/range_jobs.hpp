/**
 * @file
 * @brief Disjoint borrowed range chunks with explicit input lifetime and scoped joins.
 */

#pragma once

namespace atlas::jobs::detail
{
/// @brief One disjoint borrowed span chunk and its copied capture-free range function.
template <typename T, typename Function> struct range_chunk
{
    /// @brief Metric/workload storage owned by this scope or borrowed span input where specified.
    std::span<T> values;
    /// @brief Capture-free callable retained with its explicit payload.
    Function function;
};

/// @brief Execute one borrowed range chunk with its declared access guard held.
///
/// @param context Executor-provided access and cancellation capabilities.
/// @param payload Borrowed chunk and copied range callable.
template <typename T, typename Function>
void run_chunk(job_context& context, range_chunk<T, Function>& payload)
{
    const auto region = [&]
    {
        if constexpr (std::is_const_v<T>)
        {
            return read_access(payload.values);
        }
        else
        {
            return write_access(payload.values);
        }
    }();
    auto guard = context.access(region);
    std::invoke(payload.function, context, payload.values);
}

/// @brief Owned range-dispatch metadata retained until all chunks join.
template <typename T, typename Function> struct range_payload
{
    /// @brief Owned diagnostic label used when publishing the task.
    job_label label;
    /// @brief Metric/workload storage owned by this scope or borrowed span input where specified.
    std::span<T> values;
    /// @brief Caller-selected number of elements per dispatched range chunk.
    std::size_t grain;
    /// @brief Capture-free callable retained with its explicit payload.
    Function function;
};

/// @brief Validate span/grain inputs and join all admitted chunks, preserving the small-range path.
///
/// @param context Executor-provided child-dispatch and cancellation capabilities.
/// @param payload Borrowed range, chunk size and copied range callable.
template <typename T, typename Function>
void run_ranges(job_context& context, range_payload<T, Function>& payload)
{
    context.throw_if_cancelled();
    if (payload.values.empty())
    {
        return;
    }

    if (payload.values.size() <= payload.grain)
    {
        range_chunk<T, Function> chunk{payload.values, payload.function};
        run_chunk(context, chunk);
        return;
    }

    for (std::size_t start = 0; start < payload.values.size();)
    {
        context.throw_if_cancelled();
        const auto count = std::min(payload.grain, payload.values.size() - start);
        context.spawn(
            job_label{payload.label.name + " [" + std::to_string(start) + "]",
                      payload.label.category},
            range_chunk<T, Function>{payload.values.subspan(start, count), payload.function},
            &run_chunk<T, Function>);
        start += count;
    }
}
} // namespace atlas::jobs::detail

namespace atlas::jobs
{
/// @brief Split a borrowed span into disjoint grains and return a handle joining every admitted
/// chunk.
///
/// @param label Owned diagnostic name and optional category for this work.
/// @param values Borrowed range; keep its elements alive until the returned handle joins all
/// chunks.
/// @param grain Positive maximum elements per dispatched chunk.
/// @param function Capture-free callback invoked with an executor-created context.
template <typename T, typename Function>
    requires job_function<Function, std::span<T>>
task_handle job_dispatcher::parallel_for(job_label label, std::span<T> values, std::size_t grain,
                                         Function function) const
{
    if (grain == 0)
    {
        throw std::invalid_argument{"ParallelFor grain must be positive"};
    }

    detail::range_payload<T, Function> payload{label, values, grain, std::move(function)};
    detail::require_function(payload.function);
    return run(std::move(label), std::move(payload), &detail::run_ranges<T, Function>);
}

/// @brief Split a borrowed span into disjoint grains and return a handle joining every admitted
/// chunk.
///
/// @param label Owned diagnostic name and optional category for this work.
/// @param values Borrowed range; keep its elements alive until the returned handle joins all
/// chunks.
/// @param grain Positive maximum elements per dispatched chunk.
/// @param function Capture-free callback invoked with an executor-created context.
template <typename T, typename Function>
    requires job_function<Function, std::span<T>>
task_handle job_context::parallel_for(job_label label, std::span<T> values, std::size_t grain,
                                      Function function) const
{
    if (grain == 0)
    {
        throw std::invalid_argument{"ParallelFor grain must be positive"};
    }

    detail::range_payload<T, Function> payload{label, values, grain, std::move(function)};
    detail::require_function(payload.function);
    return spawn(std::move(label), std::move(payload), &detail::run_ranges<T, Function>);
}
} // namespace atlas::jobs
