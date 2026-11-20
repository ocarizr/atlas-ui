/** @file @brief Existing weak mailboxes transport registered-action jobs without Lua access. */
#include "domain_state.hpp"

namespace atlas::scripting
{
/// @brief Transfer each accepted request once in stable context/request order.
std::vector<callable_work> domain::take_callable_requests()
{
    impl_->check();
    std::vector<callable_work> result;
    std::size_t capacity = 0;
    for (const auto& [id, context] : impl_->callables.contexts)
    {
        const std::lock_guard lock{context->mail->mutex};
        capacity += context->mail->pending.size();
    }

    result.reserve(capacity);
    for (const auto& [id, context] : impl_->callables.contexts)
    {
        const std::lock_guard lock{context->mail->mutex};
        for (auto& pending : context->mail->pending)
        {
            if (pending.issued)
            {
                continue;
            }

            work_request work;
            work.id = pending.id;
            work.ticket_ = pending.id;
            work.destination_ = context->mail;
            work.cancellation = context->mail->cancellation.get_token();
            context->mail->bytes -= pending.name.size() + detail::payload_bytes(pending.arguments);
            work.name = std::move(pending.name);
            work.arguments = std::move(pending.arguments);
            result.push_back({context->requests.at(pending.id), std::move(work)});
            pending.issued = true;
        }
    }

    return result;
}

/// @brief Draining returns owned notifications; applying results remains an explicit owner phase.
std::vector<callable_result> domain::take_callable_results()
{
    impl_->check();
    std::vector<callable_result> result;
    std::size_t capacity = 0;
    for (const auto& [id, context] : impl_->callables.contexts)
    {
        const std::lock_guard lock{context->mail->mutex};
        capacity += context->mail->pending.size();
    }

    result.reserve(capacity);
    for (const auto& [id, context] : impl_->callables.contexts)
    {
        const std::lock_guard lock{context->mail->mutex};
        while (!context->mail->pending.empty() && context->mail->pending.front().ready)
        {
            auto& pending = context->mail->pending.front();
            const auto binding = context->requests.at(pending.id);
            result.push_back({binding, pending.id, pending.success, std::move(pending.result)});
            context->mail->bytes -= detail::payload_bytes(result.back().values);
            context->requests.erase(pending.id);
            context->mail->pending.erase(context->mail->pending.begin());
        }
    }

    return result;
}
} // namespace atlas::scripting
