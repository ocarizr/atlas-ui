/** @file @brief Weak script observers retain no Lua references after reload or teardown. */
#include "session_state.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace atlas::scripting
{
/// @brief Compile a typed observer with a weak VM owner and retained handler name.
std::uint64_t
session::impl::watch(std::variant<mvvm::field_handle, mvvm::action_handle, mvvm::event_handle> slot,
                     std::string_view handler)
{
    check();
    if (!vm || stats.failed || watches.size() == 1024 ||
        sequence == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::logic_error("invalid scripting observer admission");
    }

    mvvm::validate_text(handler, 128);
    const detail::busy_guard guard{busy};
    if (!vm->has(handler))
    {
        throw std::invalid_argument("script observer handler is missing");
    }

    /// @brief Weak observer prevents the model from extending interpreter lifetime.
    struct listener final : mvvm::observer
    {
        std::weak_ptr<impl> owner; ///< No model/VM ownership cycle.
        std::string handler;       ///< Copied stable handler identity.

        /// @brief Copy values through the protected serial behavior boundary.
        void on_notify(const mvvm::notification& event, mvvm::view_model&) override
        {
            if (const auto state = owner.lock())
            {
                (void)state->call(handler, event.arguments);
            }
        }
    };

    auto observer = std::make_shared<listener>();
    observer->owner = shared_from_this();
    observer->handler = handler;
    const auto token = std::visit(
        [&](auto handle)
        {
            return model->subscribe(handle, observer);
        },
        slot);
    watches.push_back({++sequence, token});
    return sequence;
}

/// @brief Subscribe a resolved model slot to a named protected Lua handler.
std::uint64_t session::watch(mvvm::field_handle field, std::string_view handler)
{
    return impl_->watch(field, handler);
}

/// @brief Subscribe a resolved model slot to a named protected Lua handler.
std::uint64_t session::watch(mvvm::action_handle action, std::string_view handler)
{
    return impl_->watch(action, handler);
}

/// @brief Subscribe a resolved model slot to a named protected Lua handler.
std::uint64_t session::watch(mvvm::event_handle event, std::string_view handler)
{
    return impl_->watch(event, handler);
}

/// @brief Idempotently release a model subscription before script state is destroyed.
void session::unwatch(std::uint64_t token)
{
    impl_->check();
    const detail::busy_guard guard{impl_->busy};
    const auto found = std::find_if(impl_->watches.begin(), impl_->watches.end(),
                                    [&](const auto& entry)
                                    {
                                        return entry.id == token;
                                    });
    if (found != impl_->watches.end())
    {
        impl_->model->unsubscribe(found->token);
        impl_->watches.erase(found);
    }
}
} // namespace atlas::scripting
