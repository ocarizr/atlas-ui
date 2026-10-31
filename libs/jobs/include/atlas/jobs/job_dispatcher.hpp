#pragma once

#include <atlas/jobs/job_sanitizer.hpp>
#include <atlas/jobs/task_handle.hpp>

#include <algorithm>
#include <concepts>
#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

namespace atlas::jobs
{
class job_context;
class job_system;

template <typename Function, typename Data>
concept job_function =
    (std::is_empty_v<Function> ||
     (std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>)) &&
    std::invocable<Function&, job_context&, Data&> &&
    std::same_as<std::invoke_result_t<Function&, job_context&, Data&>, void>;

template <typename Function>
concept context_function =
    (std::is_empty_v<Function> ||
     (std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>)) &&
    std::invocable<Function&, job_context&> &&
    std::same_as<std::invoke_result_t<Function&, job_context&>, void>;

namespace detail
{
using task_body = std::function<void(job_context&)>;

template <typename Function> void require_function(const Function& function)
{
    if constexpr (std::is_pointer_v<Function>)
    {
        if (!function)
        {
            throw std::invalid_argument{"A job function must not be null"};
        }
    }
}

template <typename Data, typename Function> task_body bind_job(Data data, Function function)
{
    require_function(function);
    struct payload
    {
        Data data;
        Function function;
    };
    auto owned = std::make_shared<payload>(payload{std::move(data), std::move(function)});
    return [owned = std::move(owned)](job_context& context)
    { std::invoke(owned->function, context, owned->data); };
}

template <context_function Function> task_body bind_context(Function function)
{
    require_function(function);
    return function;
}
} // namespace detail

// A non-owning client of the application's one worker pool. Obtain by injection.
class job_dispatcher
{
  public:
    [[nodiscard]] job_dispatcher with_options(job_options options) const;

    template <context_function Function> task_handle run(job_label label, Function function) const
    {
        return submit(std::move(label), detail::bind_context(std::move(function)), {});
    }

    template <context_function Function>
    task_handle after(const task_handle& prerequisite, job_label label, Function function) const
    {
        return after(std::span{&prerequisite, 1}, std::move(label), std::move(function));
    }

    template <context_function Function>
    task_handle after(std::span<const task_handle> prerequisites, job_label label,
                      Function function) const
    {
        return submit(std::move(label), detail::bind_context(std::move(function)), prerequisites);
    }

    template <context_function Function>
    task_handle after(std::initializer_list<task_handle> prerequisites, job_label label,
                      Function function) const
    {
        return after(std::span{prerequisites.begin(), prerequisites.size()}, std::move(label),
                     std::move(function));
    }

    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle run(job_label label, Data data, Function function) const
    {
        return submit(std::move(label), detail::bind_job(std::move(data), std::move(function)), {});
    }

    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle after(const task_handle& prerequisite, job_label label, Data data,
                      Function function) const
    {
        return after(std::span{&prerequisite, 1}, std::move(label), std::move(data),
                     std::move(function));
    }

    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle after(std::span<const task_handle> prerequisites, job_label label, Data data,
                      Function function) const
    {
        return submit(std::move(label), detail::bind_job(std::move(data), std::move(function)),
                      prerequisites);
    }

    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle after(std::initializer_list<task_handle> prerequisites, job_label label, Data data,
                      Function function) const
    {
        return after(std::span{prerequisites.begin(), prerequisites.size()}, std::move(label),
                     std::move(data), std::move(function));
    }

    task_handle fence(job_label label, std::span<const task_handle> prerequisites) const;
    task_handle fence(job_label label, std::initializer_list<task_handle> prerequisites) const;

    template <typename T, typename Function>
        requires job_function<Function, std::span<T>>
    task_handle parallel_for(job_label label, std::span<T> values, std::size_t grain,
                             Function function) const;

  private:
    friend class job_system;
    friend class job_context;
    explicit job_dispatcher(std::weak_ptr<detail::scheduler> scheduler);
    task_handle submit(job_label label, detail::task_body body,
                       std::span<const task_handle> prerequisites,
                       const std::shared_ptr<detail::task_state>& parent = {}) const;
    std::weak_ptr<detail::scheduler> scheduler_;
    job_options options_;
};

// Executor-created capability: children join this job, including grandchildren.
// Context operations require the originating callback on its executing thread.
class job_context
{
  public:
    [[nodiscard]] bool stop_requested() const;
    void throw_if_cancelled() const;
    [[nodiscard]] job_priority priority() const;
    [[nodiscard]] std::pmr::memory_resource& scratch() const;
    [[nodiscard]] access_guard access(std::span<const memory_access> regions) const;
    [[nodiscard]] access_guard access(std::initializer_list<memory_access> regions) const;
    [[nodiscard]] access_guard access(memory_access region) const;
    [[nodiscard]] job_dispatcher dispatcher() const;

    template <context_function Function>
    task_handle continue_with(job_label label, Function function) const
    {
        return continue_task(std::move(label), detail::bind_context(std::move(function)));
    }

    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle continue_with(job_label label, Data data, Function function) const
    {
        return continue_task(std::move(label),
                             detail::bind_job(std::move(data), std::move(function)));
    }

    template <context_function Function> task_handle spawn(job_label label, Function function) const
    {
        return submit(std::move(label), detail::bind_context(std::move(function)), {});
    }

    template <context_function Function>
    task_handle after(const task_handle& prerequisite, job_label label, Function function) const
    {
        return after(std::span{&prerequisite, 1}, std::move(label), std::move(function));
    }

    template <context_function Function>
    task_handle after(std::span<const task_handle> prerequisites, job_label label,
                      Function function) const
    {
        return submit(std::move(label), detail::bind_context(std::move(function)), prerequisites);
    }

    template <context_function Function>
    task_handle after(std::initializer_list<task_handle> prerequisites, job_label label,
                      Function function) const
    {
        return after(std::span{prerequisites.begin(), prerequisites.size()}, std::move(label),
                     std::move(function));
    }

    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle spawn(job_label label, Data data, Function function) const
    {
        return submit(std::move(label), detail::bind_job(std::move(data), std::move(function)), {});
    }

    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle after(const task_handle& prerequisite, job_label label, Data data,
                      Function function) const
    {
        return after(std::span{&prerequisite, 1}, std::move(label), std::move(data),
                     std::move(function));
    }

    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle after(std::span<const task_handle> prerequisites, job_label label, Data data,
                      Function function) const
    {
        return submit(std::move(label), detail::bind_job(std::move(data), std::move(function)),
                      prerequisites);
    }

    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle after(std::initializer_list<task_handle> prerequisites, job_label label, Data data,
                      Function function) const
    {
        return after(std::span{prerequisites.begin(), prerequisites.size()}, std::move(label),
                     std::move(data), std::move(function));
    }

    template <typename T, typename Function>
        requires job_function<Function, std::span<T>>
    task_handle parallel_for(job_label label, std::span<T> values, std::size_t grain,
                             Function function) const;

  private:
    friend struct detail::scheduler;
    job_context(std::weak_ptr<detail::scheduler> scheduler,
                std::weak_ptr<detail::task_state> parent);
    std::shared_ptr<detail::task_state> active_task() const;
    task_handle continue_task(job_label label, detail::task_body body) const;
    task_handle submit(job_label label, detail::task_body body,
                       std::span<const task_handle> prerequisites) const;
    std::weak_ptr<detail::scheduler> scheduler_;
    std::weak_ptr<detail::task_state> parent_;
};
} // namespace atlas::jobs

#include <atlas/jobs/detail/range_jobs.hpp>
