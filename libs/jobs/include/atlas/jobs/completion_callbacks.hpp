/**
 * @file
 * @brief Outcome-filtered notifications with explicit origin-thread affinity.
 */

#pragma once

#include <atlas/jobs/task_handle.hpp>

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace atlas::jobs
{
/// @brief Executor-created callback scope; valid only on its executing thread while the body
/// remains open.
///
/// Never retain this context for use on another thread or after body return. Independent roots and
/// continuations do not inherit parent input lifetime; attached children do.
class job_context;
/// @brief Copyable non-owning scheduling client of one application-owned pool.
///
/// This weak client does not extend runtime lifetime. Roots use their own policy and owned data;
/// callers retain borrowed targets through completion.
class job_dispatcher;

/// @brief Terminal source-result filters for separately observable origin-thread notifications.
enum class completion_event : std::uint8_t
{
    /// @brief Observe every terminal result of the completed source.
    complete,
    /// @brief Observe only successful source completion.
    success,
    /// @brief Observe only a failed source result.
    failure,
    /// @brief Terminal cooperative cancellation, distinct from a real callback failure.
    cancelled,
    /// @brief Body suppressed because its source/prerequisite outcome did not allow execution.
    skipped
};

namespace detail
{
/// @brief Reject completion-event values outside the supported filter set.
///
/// @param event Terminal outcome filter; notification failures never replace source outcomes.
inline void require_event(completion_event event)
{
    if (event > completion_event::skipped)
    {
        throw std::invalid_argument{"Invalid completion event"};
    }
}

/// @brief Type-erased notification body retained until origin-affine delivery/cleanup.
using callback_body = std::function<void(job_context&)>;
/// @brief Determine whether a terminal source outcome satisfies this notification filter.
///
/// @param event Terminal outcome filter; notification failures never replace source outcomes.
bool matches(completion_event event, job_status status) noexcept;

/// @brief Stateless callable object or pointer to a free function; closures carrying state are
/// rejected.
template <typename Function>
concept empty_callable =
    std::is_empty_v<Function> ||
    (std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>);

/// @brief Capture-free notification accepting its context and immutable completed source handle.
template <typename Function>
concept completion_function =
    empty_callable<Function> && std::invocable<Function&, job_context&, const task_handle&> &&
    std::same_as<std::invoke_result_t<Function&, job_context&, const task_handle&>, void>;

/// @brief Capture-free notification additionally receiving its explicit retained input.
template <typename Function, typename Data>
concept completion_data_function =
    empty_callable<Function> &&
    std::invocable<Function&, job_context&, const task_handle&, Data&> &&
    std::same_as<std::invoke_result_t<Function&, job_context&, const task_handle&, Data&>, void>;

/// @brief Reject null completion callback pointers before registration.
///
/// @param function Capture-free callback invoked with an executor-created context.
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

/// @brief Retain the completed source and explicit notification input, invoking only the selected
/// outcome filter.
///
/// @param event Terminal outcome filter; notification failures never replace source outcomes.
/// @param source Completed-source handle belonging to the originating runtime/thread.
/// @param function Capture-free callback invoked with an executor-created context.
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

/// @brief Retain the completed source and explicit notification input, invoking only the selected
/// outcome filter.
///
/// @param event Terminal outcome filter; notification failures never replace source outcomes.
/// @param source Completed-source handle belonging to the originating runtime/thread.
/// @param data Explicit input retained until this scope and attached descendants finish; borrowed
/// targets must remain valid.
/// @param function Capture-free callback invoked with an executor-created context.
template <typename Data, typename Function>
    requires completion_data_function<Function, Data>
callback_body bind_callback(completion_event event, task_handle source, Data data,
                            Function function)
{
    require_callback(function);

    /// @brief Explicit callback input and function retained together until scoped cleanup.
    struct payload
    {
        /// @brief Borrowed region address or explicit owned payload, as specified by the enclosing
        /// type.
        Data data;
        /// @brief Capture-free callable retained with its explicit payload.
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

/// @brief Origin-thread notification registrar; source results and notification outcomes remain
/// independent.
class completion_callbacks
{
  public:
    /// @brief Register an origin-thread notification for the selected terminal outcome filter.
    ///
    /// @param event Terminal outcome filter; notification failures never replace source outcomes.
    /// @param source Completed-source handle belonging to the originating runtime/thread.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <detail::completion_function Function>
    task_handle on(completion_event event, const task_handle& source, job_label label,
                   Function function) const
    {
        detail::require_event(event);
        return subscribe(source, std::move(label),
                         detail::bind_callback(event, source, std::move(function)));
    }

    /// @brief Register an origin-thread notification for the selected terminal outcome filter.
    ///
    /// @param event Terminal outcome filter; notification failures never replace source outcomes.
    /// @param source Completed-source handle belonging to the originating runtime/thread.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
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

    /// @brief Register a notification for every terminal source result, including failure,
    /// cancellation and skips.
    ///
    /// @param source Completed-source handle belonging to the originating runtime/thread.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <detail::completion_function Function>
    task_handle on_complete(const task_handle& source, job_label label, Function function) const
    {
        return on(completion_event::complete, source, std::move(label), std::move(function));
    }

    /// @brief Register a notification for every terminal source result, including failure,
    /// cancellation and skips.
    ///
    /// @param source Completed-source handle belonging to the originating runtime/thread.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <typename Data, typename Function>
        requires detail::completion_data_function<Function, Data>
    task_handle on_complete(const task_handle& source, job_label label, Data data,
                            Function function) const
    {
        return on(completion_event::complete, source, std::move(label), std::move(data),
                  std::move(function));
    }

    /// @brief Register a notification only for successful source completion.
    ///
    /// @param source Completed-source handle belonging to the originating runtime/thread.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <detail::completion_function Function>
    task_handle on_success(const task_handle& source, job_label label, Function function) const
    {
        return on(completion_event::success, source, std::move(label), std::move(function));
    }

    /// @brief Register a notification only for successful source completion.
    ///
    /// @param source Completed-source handle belonging to the originating runtime/thread.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <typename Data, typename Function>
        requires detail::completion_data_function<Function, Data>
    task_handle on_success(const task_handle& source, job_label label, Data data,
                           Function function) const
    {
        return on(completion_event::success, source, std::move(label), std::move(data),
                  std::move(function));
    }

    /// @brief Register a notification only for a failed source; notification failures remain
    /// separate.
    ///
    /// @param source Completed-source handle belonging to the originating runtime/thread.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <detail::completion_function Function>
    task_handle on_failure(const task_handle& source, job_label label, Function function) const
    {
        return on(completion_event::failure, source, std::move(label), std::move(function));
    }

    /// @brief Register a notification only for a failed source; notification failures remain
    /// separate.
    ///
    /// @param source Completed-source handle belonging to the originating runtime/thread.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <typename Data, typename Function>
        requires detail::completion_data_function<Function, Data>
    task_handle on_failure(const task_handle& source, job_label label, Data data,
                           Function function) const
    {
        return on(completion_event::failure, source, std::move(label), std::move(data),
                  std::move(function));
    }

    /// @brief Register a notification only for cooperative cancellation of the source.
    ///
    /// @param source Completed-source handle belonging to the originating runtime/thread.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <detail::completion_function Function>
    task_handle on_cancelled(const task_handle& source, job_label label, Function function) const
    {
        return on(completion_event::cancelled, source, std::move(label), std::move(function));
    }

    /// @brief Register a notification only for cooperative cancellation of the source.
    ///
    /// @param source Completed-source handle belonging to the originating runtime/thread.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <typename Data, typename Function>
        requires detail::completion_data_function<Function, Data>
    task_handle on_cancelled(const task_handle& source, job_label label, Data data,
                             Function function) const
    {
        return on(completion_event::cancelled, source, std::move(label), std::move(data),
                  std::move(function));
    }

    /// @brief Register a notification only when a failed/cancelled prerequisite suppressed the
    /// source body.
    ///
    /// @param source Completed-source handle belonging to the originating runtime/thread.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <detail::completion_function Function>
    task_handle on_skipped(const task_handle& source, job_label label, Function function) const
    {
        return on(completion_event::skipped, source, std::move(label), std::move(function));
    }

    /// @brief Register a notification only when a failed/cancelled prerequisite suppressed the
    /// source body.
    ///
    /// @param source Completed-source handle belonging to the originating runtime/thread.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
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
    /// @brief Bind notification registration to the source runtime and optional executing scope.
    completion_callbacks(std::weak_ptr<detail::scheduler> scheduler, job_priority priority);
    /// @brief Validate source ownership and origin affinity, then attach a separately observable
    /// notification task.
    ///
    /// @param source Completed-source handle belonging to the originating runtime/thread.
    /// @param label Owned diagnostic name and optional category for this work.
    task_handle subscribe(const task_handle& source, job_label label,
                          detail::callback_body body) const;
    /// @brief Weak reference to the shared runtime; this client never creates a worker pool.
    std::weak_ptr<detail::scheduler> scheduler_;
    /// @brief Effective weighted dispatch class; it does not imply callback preemption.
    job_priority priority_;
};
} // namespace atlas::jobs
