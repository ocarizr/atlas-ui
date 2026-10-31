#pragma once

namespace atlas::jobs::detail
{
template <typename T, typename Function> struct range_chunk
{
    std::span<T> values;
    Function function;
};

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

template <typename T, typename Function> struct range_payload
{
    job_label label;
    std::span<T> values;
    std::size_t grain;
    Function function;
};

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
