#pragma once

namespace atlas::jobs::detail
{
template <typename T, typename Function> struct range_payload
{
    std::string name;
    std::span<T> values;
    std::size_t grain;
    Function function;
};

template <typename T, typename Function>
void run_ranges(job_context& context, range_payload<T, Function>& payload)
{
    if (payload.values.empty())
    {
        return;
    }
    if (payload.values.size() <= payload.grain)
    {
        std::invoke(payload.function, context, payload.values);
        return;
    }
    for (std::size_t start = 0; start < payload.values.size();)
    {
        const auto count = std::min(payload.grain, payload.values.size() - start);
        context.spawn(payload.name + " [" + std::to_string(start) + "]",
                      payload.values.subspan(start, count), payload.function);
        start += count;
    }
}
} // namespace atlas::jobs::detail

namespace atlas::jobs
{
template <typename T, typename Function>
    requires job_function<Function, std::span<T>>
task_handle job_dispatcher::parallel_for(std::string name, std::span<T> values, std::size_t grain,
                                         Function function) const
{
    if (grain == 0)
    {
        throw std::invalid_argument{"ParallelFor grain must be positive"};
    }
    detail::range_payload<T, Function> payload{name, values, grain, std::move(function)};
    detail::require_function(payload.function);
    return run(std::move(name), std::move(payload), &detail::run_ranges<T, Function>);
}

template <typename T, typename Function>
    requires job_function<Function, std::span<T>>
task_handle job_context::parallel_for(std::string name, std::span<T> values, std::size_t grain,
                                      Function function) const
{
    if (grain == 0)
    {
        throw std::invalid_argument{"ParallelFor grain must be positive"};
    }
    detail::range_payload<T, Function> payload{name, values, grain, std::move(function)};
    detail::require_function(payload.function);
    return spawn(std::move(name), std::move(payload), &detail::run_ranges<T, Function>);
}
} // namespace atlas::jobs
