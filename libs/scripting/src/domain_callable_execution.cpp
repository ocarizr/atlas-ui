/** @file @brief Transactional owner invocation and bounded native/Lua predicate execution. */
#include "domain_state.hpp"
#include <limits>

namespace atlas::scripting
{
/// @brief Reserve all fallible work bookkeeping before admitting any model commands.
void domain::impl::admit_callable(detail::callable_context& context, callable_binding binding,
                                  detail::call_scope& scope)
{
    std::size_t text = 0, bytes = 0;
    for (const auto& command : scope.commands)
    {
        text += detail::payload_bytes(command.arguments) -
                command.arguments.size() * sizeof(mvvm::value);
    }

    for (const auto& request : scope.requests)
    {
        bytes += request.name.size() + detail::payload_bytes(request.arguments);
        text += request.name.size() + detail::payload_bytes(request.arguments) -
                request.arguments.size() * sizeof(mvvm::value);
    }

    if (text > limits.max_text_bytes)
    {
        throw std::overflow_error("Callable aggregate text budget exhausted");
    }

    if (scope.requests.empty())
    {
        context.model->submit(scope.commands);
        return;
    }

    if (scope.requests.size() > std::numeric_limits<std::uint64_t>::max() - context.next_request)
    {
        throw std::overflow_error("Callable work identities exhausted");
    }

    auto next = context.requests;
    auto next_ticket = context.next_request;
    for (auto& request : scope.requests)
    {
        request.id = ++next_ticket;
        next.emplace(request.id, binding);
    }

    const std::lock_guard lock{context.mail->mutex};
    if (context.mail->closed ||
        scope.requests.size() > limits.max_pending_work - context.mail->pending.size() ||
        bytes > context.mail->limit - context.mail->bytes)
    {
        throw std::overflow_error("Callable pending-work budget exhausted");
    }

    context.mail->pending.reserve(limits.max_pending_work);
    context.model->submit(scope.commands);
    for (auto& request : scope.requests)
    {
        context.mail->pending.push_back(std::move(request));
    }

    context.requests.swap(next);
    context.next_request = next_ticket;
    context.mail->bytes += bytes;
}

/// @brief Actions always produce demands; even native implementations cannot mutate through this
/// API.
void domain::invoke_action(callable_binding binding, std::span<const mvvm::value> arguments)
{
    impl_->check();
    auto& entry = impl_->callable(binding);
    auto& context = impl_->callable_context(binding);
    if (entry.descriptor.kind != callable_kind::action || context.failed || !impl_->frame ||
        context.model->inspect().frame != impl_->frame || context.model->inspect().failed)
    {
        throw std::logic_error("Action requires a healthy matching owner/model frame");
    }

    if (impl_->callables.calls++ >= impl_->limits.max_calls)
    {
        throw std::overflow_error("Callable frame call budget exhausted");
    }

    auto values =
        detail::callable_arguments(entry.descriptor, arguments, impl_->limits.max_text_bytes);
    const detail::busy_guard guard{impl_->busy};
    const diagnostics::operation_scope trace{impl_->destinations, {"script.action", "script.cpu"}};
    detail::call_scope scope{*context.model, impl_->limits, false, {}, {}};
    try
    {
        if (entry.module)
        {
            context.vm->callable_scope = &scope;
            const auto result = context.vm->call(entry.descriptor.id, values);
            context.vm->callable_scope = nullptr;
            if (!result.empty())
            {
                throw std::invalid_argument("Registered actions must not return values");
            }
        }
        else
        {
            action_context capability{scope};
            entry.action(capability, values);
        }

        impl_->admit_callable(context, binding, scope);
    }
    catch (...)
    {
        if (context.vm)
        {
            context.vm->callable_scope = nullptr;
            context.failed = true;
            context.mail->close();
            context.requests.clear();
        }

        diagnostics::write(impl_->destinations, diagnostics::log_level::warning,
                           "scripting.actions",
                           "Registered action failed; staged demands were discarded");
        throw;
    }
}

/// @brief Conditions return exactly one boolean and receive only committed read capabilities.
bool domain::impl::run_condition(callable_binding binding, std::span<const mvvm::value> arguments)
{
    auto& entry = callable(binding);
    auto& context = callable_context(binding);
    if (entry.descriptor.kind != callable_kind::condition || context.failed || !frame ||
        context.model->inspect().frame != frame || context.model->inspect().failed)
    {
        throw std::logic_error("Condition requires a healthy matching owner/model frame");
    }

    if (callables.calls++ >= limits.max_calls)
    {
        throw std::overflow_error("Callable frame call budget exhausted");
    }

    const auto values =
        detail::callable_arguments(entry.descriptor, arguments, limits.max_text_bytes);
    const diagnostics::operation_scope trace{destinations, {"script.condition", "script.cpu"}};
    detail::call_scope scope{*context.model, limits, true, {}, {}};
    if (!entry.module)
    {
        const condition_context capability{scope};
        return entry.condition(capability, values);
    }

    try
    {
        context.vm->callable_scope = &scope;
        const auto result = context.vm->call(entry.descriptor.id, values);
        context.vm->callable_scope = nullptr;
        if (result.size() != 1 || !std::holds_alternative<bool>(result.front()))
        {
            throw std::invalid_argument("Navigation condition must return exactly one boolean");
        }

        return std::get<bool>(result.front());
    }
    catch (...)
    {
        context.vm->callable_scope = nullptr;
        context.failed = true;
        context.mail->close();
        context.requests.clear();
        throw;
    }
}

/// @brief Direct predicate calls throw on misuse; grouped evaluation converts failures to denial.
bool domain::evaluate_condition(callable_binding binding, std::span<const mvvm::value> arguments)
{
    impl_->check();
    const detail::busy_guard guard{impl_->busy};
    return impl_->run_condition(binding, arguments);
}
} // namespace atlas::scripting
