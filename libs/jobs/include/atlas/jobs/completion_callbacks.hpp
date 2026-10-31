#pragma once

#include <atlas/jobs/task_handle.hpp>

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace atlas::jobs
{
class job_context;
class job_dispatcher;

enum class completion_event : std::uint8_t
{
    complete,
    success,
    failure,
    cancelled,
    skipped
};

namespace detail
{
inline void require_event(completion_event event)
{
    if (event > completion_event::skipped)
    {
        throw std::invalid_argument{"Invalid completion event"};
    }
}
using callback_body = std::function<void(job_context&)>;
bool matches(completion_event event, job_status status) noexcept;

template <typename Function>
concept empty_callable =
    std::is_empty_v<Function> ||
    (std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>);

template <typename Function>
concept completion_function =
    empty_callable<Function> && std::invocable<Function&, job_context&, const task_handle&> &&
    std::same_as<std::invoke_result_t<Function&, job_context&, const task_handle&>, void>;

template <typename Function, typename Data>
concept completion_data_function =
    empty_callable<Function> &&
    std::invocable<Function&, job_context&, const task_handle&, Data&> &&
    std::same_as<std::invoke_result_t<Function&, job_context&, const task_handle&, Data&>, void>;

template <typename Function> void require_callback(const Function& function)
{
    if constexpr (std::is_pointer_v<Function>)
    {
        if (!function)
        {
            throw std::invalid_argument{"A completion callback must not be null"};
        }
    }
}
template <completion_function Function>
callback_body bind_callback(completion_event event, task_handle source, Function function)
{
    require_callback(function);
    return [event, source = std::move(source),
            function = std::move(function)](job_context& context) mutable
    {
        if (matches(event, source.status()))
        {
            std::invoke(function, context, std::as_const(source));
        }
    };
}

template <typename Data, typename Function>
    requires completion_data_function<Function, Data>
callback_body bind_callback(completion_event event, task_handle source, Data data,
                            Function function)
{
    require_callback(function);
    struct payload
    {
        Data data;
        Function function;
    };
    auto owned = std::make_shared<payload>(payload{std::move(data), std::move(function)});
    return [event, source = std::move(source), owned = std::move(owned)](job_context& context)
    {
        if (matches(event, source.status()))
        {
            std::invoke(owned->function, context, source, owned->data);
        }
    };
}
} // namespace detail

class completion_callbacks
{
  public:
    template <detail::completion_function Function>
    task_handle on(completion_event event, const task_handle& source, job_label label,
                   Function function) const
    {
        detail::require_event(event);
        return subscribe(source, std::move(label),
                         detail::bind_callback(event, source, std::move(function)));
    }

    template <typename Data, typename Function>
        requires detail::completion_data_function<Function, Data>
    task_handle on(completion_event event, const task_handle& source, job_label label, Data data,
                   Function function) const
    {
        detail::require_event(event);
        return subscribe(
            source, std::move(label),
            detail::bind_callback(event, source, std::move(data), std::move(function)));
    }

    template <detail::completion_function Function>
    task_handle on_complete(const task_handle& source, job_label label, Function function) const
    {
        return on(completion_event::complete, source, std::move(label), std::move(function));
    }

    template <typename Data, typename Function>
        requires detail::completion_data_function<Function, Data>
    task_handle on_complete(const task_handle& source, job_label label, Data data,
                            Function function) const
    {
        return on(completion_event::complete, source, std::move(label), std::move(data),
                  std::move(function));
    }

    template <detail::completion_function Function>
    task_handle on_success(const task_handle& source, job_label label, Function function) const
    {
        return on(completion_event::success, source, std::move(label), std::move(function));
    }

    template <typename Data, typename Function>
        requires detail::completion_data_function<Function, Data>
    task_handle on_success(const task_handle& source, job_label label, Data data,
                           Function function) const
    {
        return on(completion_event::success, source, std::move(label), std::move(data),
                  std::move(function));
    }

    template <detail::completion_function Function>
    task_handle on_failure(const task_handle& source, job_label label, Function function) const
    {
        return on(completion_event::failure, source, std::move(label), std::move(function));
    }

    template <typename Data, typename Function>
        requires detail::completion_data_function<Function, Data>
    task_handle on_failure(const task_handle& source, job_label label, Data data,
                           Function function) const
    {
        return on(completion_event::failure, source, std::move(label), std::move(data),
                  std::move(function));
    }

    template <detail::completion_function Function>
    task_handle on_cancelled(const task_handle& source, job_label label, Function function) const
    {
        return on(completion_event::cancelled, source, std::move(label), std::move(function));
    }

    template <typename Data, typename Function>
        requires detail::completion_data_function<Function, Data>
    task_handle on_cancelled(const task_handle& source, job_label label, Data data,
                             Function function) const
    {
        return on(completion_event::cancelled, source, std::move(label), std::move(data),
                  std::move(function));
    }

    template <detail::completion_function Function>
    task_handle on_skipped(const task_handle& source, job_label label, Function function) const
    {
        return on(completion_event::skipped, source, std::move(label), std::move(function));
    }

    template <typename Data, typename Function>
        requires detail::completion_data_function<Function, Data>
    task_handle on_skipped(const task_handle& source, job_label label, Data data,
                           Function function) const
    {
        return on(completion_event::skipped, source, std::move(label), std::move(data),
                  std::move(function));
    }

  private:
    friend class job_dispatcher;
    friend class job_context;
    completion_callbacks(std::weak_ptr<detail::scheduler> scheduler, job_priority priority);
    task_handle subscribe(const task_handle& source, job_label label,
                          detail::callback_body body) const;
    std::weak_ptr<detail::scheduler> scheduler_;
    job_priority priority_;
};
} // namespace atlas::jobs
