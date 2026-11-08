/** @file @brief Typed explicit-frame ingress between independent coordinator-owned domains. */
#include "domain_state.hpp"
#include <algorithm>
#include <stdexcept>

namespace atlas::scripting
{
/// @brief Reject stale frame/sequence identities before changing ingress state or bytes.
void domain::enqueue(domain_message message)
{
    impl_->check();
    const auto& target = impl_->get(message.destination);
    mvvm::validate_text(message.handler, 128);
    detail::validate_values(message.arguments, impl_->limits.max_text_bytes);
    if (!target.enabled || !impl_->frame || message.frame != impl_->frame ||
        message.sequence <= impl_->message_sequence || message.handler.empty())
    {
        throw std::invalid_argument("disabled destination or invalid message sequence/frame");
    }

    const auto bytes = message.handler.size() + detail::payload_bytes(message.arguments);
    if (impl_->messages.size() - impl_->message_head >= impl_->limits.max_pending_work ||
        bytes > impl_->limits.max_pending_bytes - impl_->message_bytes)
    {
        throw std::overflow_error("domain message capacity exhausted");
    }

    if (impl_->message_head)
    {
        impl_->messages.erase(impl_->messages.begin(),
                              impl_->messages.begin() +
                                  static_cast<std::ptrdiff_t>(impl_->message_head));
        impl_->message_head = 0;
    }

    const auto sequence = message.sequence;
    impl_->messages.push_back(std::move(message));
    impl_->message_sequence = sequence;
    impl_->message_bytes += bytes;
}

/// @brief Consume attempted events once; retirement after admission cannot target a new generation.
void domain::deliver_messages()
{
    impl_->check();
    const detail::busy_guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"script.domain.messages", "script.dispatch"}};
    while (impl_->message_head < impl_->messages.size())
    {
        auto message = std::move(impl_->messages[impl_->message_head++]);
        impl_->message_bytes -= message.handler.size() + detail::payload_bytes(message.arguments);
        const auto found = std::find_if(impl_->entries.begin(), impl_->entries.end(),
                                        [&](const auto& entry)
                                        {
                                            return entry.identity == message.destination;
                                        });
        if (found == impl_->entries.end() || !found->enabled)
        {
            diagnostics::write(impl_->destinations, diagnostics::log_level::warning, "scripting",
                               "discarded domain message for retired or disabled instance");
            continue;
        }

        found->behavior->call(message.handler, message.arguments);
    }

    impl_->messages.clear();
    impl_->message_head = 0;
}
} // namespace atlas::scripting
