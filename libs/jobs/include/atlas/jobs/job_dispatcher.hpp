/**
 * @file
 * @brief Direct root/child/continuation dispatch and executor-created context contracts.
 */

#pragma once

#include <atlas/jobs/completion_callbacks.hpp>
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
/// @brief Executor-created callback scope; valid only on its executing thread while the body
/// remains open.
///
/// Never retain this context for use on another thread or after body return. Independent roots and
/// continuations do not inherit parent input lifetime; attached children do.
class job_context;
/// @brief Application-owned persistent CPU worker pool; inject dispatchers instead of creating
/// consumer pools.
///
/// External producers must stop before destruction. Jobs, notifications and terminal observers must
/// not wait or shut down the pool. Shutdown drains accepted contexts and allows their attached
/// children.
class job_system;

/// @brief Capture-free void callback receiving the injected context and explicit mutable payload.
template <typename Function, typename Data>
concept job_function =
    (std::is_empty_v<Function> ||
     (std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>)) &&
    std::invocable<Function&, job_context&, Data&> &&
    std::same_as<std::invoke_result_t<Function&, job_context&, Data&>, void>;

/// @brief Capture-free void callback receiving only the executor-created context.
template <typename Function>
concept context_function =
    (std::is_empty_v<Function> ||
     (std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>)) &&
    std::invocable<Function&, job_context&> &&
    std::same_as<std::invoke_result_t<Function&, job_context&>, void>;

namespace detail
{
/// @brief Type-erased executor callback; explicit input ownership is established before admission.
using task_body = std::function<void(job_context&)>;

/// @brief Reject null callback pointers before any work is admitted.
///
/// @param function Capture-free callback invoked with an executor-created context.
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

/// @brief Retain explicit input with its capture-free function until scoped execution cleanup.
///
/// @param data Explicit input retained until this scope and attached descendants finish; borrowed
/// targets must remain valid.
/// @param function Capture-free callback invoked with an executor-created context.
template <typename Data, typename Function> task_body bind_job(Data data, Function function)
{
    require_function(function);

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
    return [owned = std::move(owned)](job_context& context)
    {
        std::invoke(owned->function, context, owned->data);
    };
}

/// @brief Validate and adapt a capture-free context-only callback.
///
/// @param function Capture-free callback invoked with an executor-created context.
template <context_function Function> task_body bind_context(Function function)
{
    require_function(function);
    return function;
}
} // namespace detail

/// A non-owning client of the application's one worker pool. Obtain by injection.
class job_dispatcher
{
  public:
    /// @brief Return the origin-thread registration interface for completion notifications.
    [[nodiscard]] completion_callbacks callbacks() const;
    /// @brief Create a non-owning dispatch view with explicit priority and cooperative cancellation
    /// policy.
    ///
    /// @param options Explicit validated configuration/policy for this instance.
    [[nodiscard]] job_dispatcher with_options(job_options options) const;

    /// @brief Dispatch an independent root on the shared worker pool; it does not inherit a
    /// caller's child lifetime.
    ///
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <context_function Function> task_handle run(job_label label, Function function) const
    {
        return submit(std::move(label), detail::bind_context(std::move(function)), {});
    }

    /// @brief Dispatch work only after every prerequisite succeeds; failed prerequisites suppress
    /// its body.
    ///
    /// @param prerequisite Earlier task that must complete successfully before this body may run.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <context_function Function>
    task_handle after(const task_handle& prerequisite, job_label label, Function function) const
    {
        return after(std::span{&prerequisite, 1}, std::move(label), std::move(function));
    }

    /// @brief Dispatch work only after every prerequisite succeeds; failed prerequisites suppress
    /// its body.
    ///
    /// @param prerequisites Earlier tasks whose successful completion makes this body eligible.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <context_function Function>
    task_handle after(std::span<const task_handle> prerequisites, job_label label,
                      Function function) const
    {
        return submit(std::move(label), detail::bind_context(std::move(function)), prerequisites);
    }

    /// @brief Dispatch work only after every prerequisite succeeds; failed prerequisites suppress
    /// its body.
    ///
    /// @param prerequisites Earlier tasks whose successful completion makes this body eligible.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <context_function Function>
    task_handle after(std::initializer_list<task_handle> prerequisites, job_label label,
                      Function function) const
    {
        return after(std::span{prerequisites.begin(), prerequisites.size()}, std::move(label),
                     std::move(function));
    }

    /// @brief Dispatch an independent root on the shared worker pool; it does not inherit a
    /// caller's child lifetime.
    ///
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle run(job_label label, Data data, Function function) const
    {
        return submit(std::move(label), detail::bind_job(std::move(data), std::move(function)), {});
    }

    /// @brief Dispatch work only after every prerequisite succeeds; failed prerequisites suppress
    /// its body.
    ///
    /// @param prerequisite Earlier task that must complete successfully before this body may run.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle after(const task_handle& prerequisite, job_label label, Data data,
                      Function function) const
    {
        return after(std::span{&prerequisite, 1}, std::move(label), std::move(data),
                     std::move(function));
    }

    /// @brief Dispatch work only after every prerequisite succeeds; failed prerequisites suppress
    /// its body.
    ///
    /// @param prerequisites Earlier tasks whose successful completion makes this body eligible.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle after(std::span<const task_handle> prerequisites, job_label label, Data data,
                      Function function) const
    {
        return submit(std::move(label), detail::bind_job(std::move(data), std::move(function)),
                      prerequisites);
    }

    /// @brief Dispatch work only after every prerequisite succeeds; failed prerequisites suppress
    /// its body.
    ///
    /// @param prerequisites Earlier tasks whose successful completion makes this body eligible.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle after(std::initializer_list<task_handle> prerequisites, job_label label, Data data,
                      Function function) const
    {
        return after(std::span{prerequisites.begin(), prerequisites.size()}, std::move(label),
                     std::move(data), std::move(function));
    }

    /// @brief Create a named fan-in task that completes after its prerequisites succeed.
    ///
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param prerequisites Earlier tasks whose successful completion makes this body eligible.
    task_handle fence(job_label label, std::span<const task_handle> prerequisites) const;
    /// @brief Create a named fan-in task that completes after its prerequisites succeed.
    ///
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param prerequisites Earlier tasks whose successful completion makes this body eligible.
    task_handle fence(job_label label, std::initializer_list<task_handle> prerequisites) const;

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
    task_handle parallel_for(job_label label, std::span<T> values, std::size_t grain,
                             Function function) const;

  private:
    friend class job_system;
    friend class job_context;
    /// @brief Bind a non-owning client to an existing shared pool.
    explicit job_dispatcher(std::weak_ptr<detail::scheduler> scheduler);
    /// @brief Validate the shared runtime and admit a root or explicit parent-linked task with this
    /// client's policy.
    ///
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param body Owned callable retained until execution and joined cleanup.
    /// @param prerequisites Earlier tasks whose successful completion makes this body eligible.
    /// @param parent Optional parent scope whose completion joins this task.
    task_handle submit(job_label label, detail::task_body body,
                       std::span<const task_handle> prerequisites,
                       const std::shared_ptr<detail::task_state>& parent = {}) const;
    /// @brief Weak reference to the shared runtime; this client never creates a worker pool.
    std::weak_ptr<detail::scheduler> scheduler_;
    /// @brief Immutable policy/configuration retained by this object.
    job_options options_;
};

/// Executor-created capability: children join this job, including grandchildren.
/// Context operations require the originating callback on its executing thread.
class job_context
{
  public:
    /// @brief Return the origin-thread registration interface for completion notifications.
    [[nodiscard]] completion_callbacks callbacks() const;
    /// @brief Inspect this scope's cooperative cancellation token without cancelling running code
    /// forcibly.
    [[nodiscard]] bool stop_requested() const;
    /// @brief Throw the cancellation cause when requested; prior writes are not rolled back.
    void throw_if_cancelled() const;
    /// @brief Read this task's effective scheduling class; categories do not set priority.
    [[nodiscard]] job_priority priority() const;
    /// @brief Borrow the task-owned monotonic resource until scoped descendants and input cleanup
    /// finish.
    [[nodiscard]] std::pmr::memory_resource& scratch() const;
    /// @brief Register declared memory regions atomically and retain the returned guard through the
    /// actual accesses.
    ///
    /// @param regions Borrowed read/write descriptors declared before the actual accesses.
    [[nodiscard]] access_guard access(std::span<const memory_access> regions) const;
    /// @brief Register declared memory regions atomically and retain the returned guard through the
    /// actual accesses.
    ///
    /// @param regions Borrowed read/write descriptors declared before the actual accesses.
    [[nodiscard]] access_guard access(std::initializer_list<memory_access> regions) const;
    /// @brief Register declared memory regions atomically and retain the returned guard through the
    /// actual accesses.
    ///
    /// @param region Borrowed read/write descriptor retained by the returned scope guard.
    [[nodiscard]] access_guard access(memory_access region) const;
    /// @brief Return an independent default-policy client of the same pool; it does not attach new
    /// roots to this scope.
    [[nodiscard]] job_dispatcher dispatcher() const;

    /// @brief Dispatch an independent success-gated continuation after this task and its
    /// descendants finish.
    ///
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <context_function Function>
    task_handle continue_with(job_label label, Function function) const
    {
        return continue_task(std::move(label), detail::bind_context(std::move(function)));
    }

    /// @brief Dispatch an independent success-gated continuation after this task and its
    /// descendants finish.
    ///
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle continue_with(job_label label, Data data, Function function) const
    {
        return continue_task(std::move(label),
                             detail::bind_job(std::move(data), std::move(function)));
    }

    /// @brief Attach a child to the currently executing scope; parent completion joins it before
    /// releasing input.
    ///
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <context_function Function> task_handle spawn(job_label label, Function function) const
    {
        return submit(std::move(label), detail::bind_context(std::move(function)), {});
    }

    /// @brief Attach a child gated by successful prerequisites; the current parent joins it.
    /// Failed prerequisites suppress the child body.
    ///
    /// @param prerequisite Earlier task that must complete successfully before this body may run.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <context_function Function>
    task_handle after(const task_handle& prerequisite, job_label label, Function function) const
    {
        return after(std::span{&prerequisite, 1}, std::move(label), std::move(function));
    }

    /// @brief Attach a child gated by successful prerequisites; the current parent joins it.
    /// Failed prerequisites suppress the child body.
    ///
    /// @param prerequisites Earlier tasks whose successful completion makes this body eligible.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <context_function Function>
    task_handle after(std::span<const task_handle> prerequisites, job_label label,
                      Function function) const
    {
        return submit(std::move(label), detail::bind_context(std::move(function)), prerequisites);
    }

    /// @brief Attach a child gated by successful prerequisites; the current parent joins it.
    /// Failed prerequisites suppress the child body.
    ///
    /// @param prerequisites Earlier tasks whose successful completion makes this body eligible.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <context_function Function>
    task_handle after(std::initializer_list<task_handle> prerequisites, job_label label,
                      Function function) const
    {
        return after(std::span{prerequisites.begin(), prerequisites.size()}, std::move(label),
                     std::move(function));
    }

    /// @brief Attach a child to the currently executing scope; parent completion joins it before
    /// releasing input.
    ///
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle spawn(job_label label, Data data, Function function) const
    {
        return submit(std::move(label), detail::bind_job(std::move(data), std::move(function)), {});
    }

    /// @brief Attach a child gated by successful prerequisites; the current parent joins it.
    /// Failed prerequisites suppress the child body.
    ///
    /// @param prerequisite Earlier task that must complete successfully before this body may run.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle after(const task_handle& prerequisite, job_label label, Data data,
                      Function function) const
    {
        return after(std::span{&prerequisite, 1}, std::move(label), std::move(data),
                     std::move(function));
    }

    /// @brief Attach a child gated by successful prerequisites; the current parent joins it.
    /// Failed prerequisites suppress the child body.
    ///
    /// @param prerequisites Earlier tasks whose successful completion makes this body eligible.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle after(std::span<const task_handle> prerequisites, job_label label, Data data,
                      Function function) const
    {
        return submit(std::move(label), detail::bind_job(std::move(data), std::move(function)),
                      prerequisites);
    }

    /// @brief Attach a child gated by successful prerequisites; the current parent joins it.
    /// Failed prerequisites suppress the child body.
    ///
    /// @param prerequisites Earlier tasks whose successful completion makes this body eligible.
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param data Input value retained by the newly submitted task and its attached descendants;
    /// borrowed targets must independently remain valid through that task completion.
    /// @param function Capture-free callback invoked with an executor-created context.
    template <typename Data, typename Function>
        requires job_function<Function, Data>
    task_handle after(std::initializer_list<task_handle> prerequisites, job_label label, Data data,
                      Function function) const
    {
        return after(std::span{prerequisites.begin(), prerequisites.size()}, std::move(label),
                     std::move(data), std::move(function));
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
    task_handle parallel_for(job_label label, std::span<T> values, std::size_t grain,
                             Function function) const;

  private:
    friend struct detail::scheduler;
    /// @brief Bind an executor-created context to its runtime and currently executing parent scope.
    job_context(std::weak_ptr<detail::scheduler> scheduler,
                std::weak_ptr<detail::task_state> parent);
    /// @brief Validate that this executor-created context is used by its still-executing callback.
    std::shared_ptr<detail::task_state> active_task() const;
    /// @brief Submit a separate continuation gated on the active scope's terminal publication.
    ///
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param body Owned continuation callable.
    task_handle continue_task(job_label label, detail::task_body body) const;
    /// @brief Admit an attached child with optional prerequisites while the current parent body is
    /// executing.
    ///
    /// @param label Owned diagnostic name and optional category for this work.
    /// @param body Owned child callable.
    /// @param prerequisites Earlier tasks whose successful completion makes this body eligible.
    task_handle submit(job_label label, detail::task_body body,
                       std::span<const task_handle> prerequisites) const;
    /// @brief Weak reference to the shared runtime; this client never creates a worker pool.
    std::weak_ptr<detail::scheduler> scheduler_;
    /// @brief Weak reference to the executing parent scope; it must not be reused after body
    /// return.
    std::weak_ptr<detail::task_state> parent_;
};
} // namespace atlas::jobs

#include <atlas/jobs/detail/range_jobs.hpp>
