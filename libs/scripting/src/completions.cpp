/** @file @brief Native worker completion transport and deterministic owner-phase acceptance. */
#include "session_state.hpp"
#include <algorithm>
#include <stdexcept>

namespace atlas::scripting::detail
{
/// @brief Retire transport under lock, then notify stop callbacks without the lock.
void mailbox::close()
{
    {
        const std::lock_guard lock{mutex};
        closed = true;
        pending.clear();
        bytes = 0;
    }
    // Stop callbacks may synchronously attempt completion; never hold the transport lock here.
    cancellation.request_stop();
}
} // namespace atlas::scripting::detail

namespace atlas::scripting
{
/// @brief Copy validated values before single acceptance into a live generation mailbox.
bool work_request::complete(std::span<const mvvm::value> values, bool success) const
{
    const auto mail = destination_.lock();
    if (!mail || cancellation.stop_requested())
    {
        return false;
    }

    if (values.size() > 30)
    {
        throw std::invalid_argument("completion reserves two protocol arguments for id/status");
    }

    detail::validate_values(values, mail->max_text);
    std::vector<mvvm::value> result{values.begin(), values.end()};
    const std::lock_guard lock{mail->mutex};
    const auto found = std::find_if(mail->pending.begin(), mail->pending.end(),
                                    [&](const auto& entry)
                                    {
                                        return entry.id == ticket_;
                                    });
    if (mail->closed || found == mail->pending.end() || found->ready)
    {
        return false;
    }

    const auto bytes = detail::payload_bytes(result);
    if (bytes > mail->limit - mail->bytes)
    {
        throw std::overflow_error("native completion payload budget exhausted");
    }

    mail->bytes += bytes;
    found->result = std::move(result);
    found->success = success;
    found->ready = true;
    return true;
}

/// @brief Move each unissued payload once without granting workers access to the VM.
std::vector<work_request> session::take_requests()
{
    impl_->check();
    if (!impl_->vm || impl_->stats.failed)
    {
        throw std::logic_error("native requests need a healthy loaded session");
    }

    const detail::busy_guard guard{impl_->busy};
    const std::lock_guard lock{impl_->mail->mutex};
    std::vector<work_request> result;
    result.reserve(impl_->mail->pending.size());
    for (auto& pending : impl_->mail->pending)
    {
        if (pending.issued)
        {
            continue;
        }

        work_request work;
        work.id = pending.id;
        work.ticket_ = pending.id;
        impl_->mail->bytes -= pending.name.size() + detail::payload_bytes(pending.arguments);
        work.name = std::move(pending.name);
        work.arguments = std::move(pending.arguments);
        work.cancellation = impl_->mail->cancellation.get_token();
        work.destination_ = impl_->mail;
        result.push_back(std::move(work));
        pending.issued = true;
    }

    return result;
}

/// @brief Drain the ready submission-order prefix at an explicit serial boundary.
void session::deliver_results()
{
    impl_->check();
    if (!impl_->vm || impl_->stats.failed || !impl_->stats.frame)
    {
        throw std::logic_error("native results need an admitted healthy script frame");
    }

    const auto mail = impl_->mail;
    const detail::busy_guard guard{impl_->delivering};
    std::size_t count = 0;
    {
        const std::lock_guard lock{mail->mutex};
        count = mail->pending.size();
    }
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"script.results", "script.dispatch"}};
    for (std::size_t i = 0; i < count; ++i)
    {
        std::vector<mvvm::value> arguments;
        std::uint64_t request_id = 0;
        {
            const std::lock_guard lock{mail->mutex};
            if (mail->pending.empty() || !mail->pending.front().ready)
            {
                break;
            }

            const auto& front = mail->pending.front();
            request_id = front.id;
            arguments.reserve(front.result.size() + 2);
            arguments.emplace_back(static_cast<std::int64_t>(front.id));
            arguments.emplace_back(front.success);
            arguments.insert(arguments.end(), front.result.begin(), front.result.end());
        }
        (void)impl_->call("on_result", arguments, request_id);
        const std::lock_guard lock{mail->mutex};
        mail->bytes -= detail::payload_bytes(mail->pending.front().result);
        mail->pending.erase(mail->pending.begin());
    }
}
} // namespace atlas::scripting
