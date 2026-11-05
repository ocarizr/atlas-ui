/** @file @brief Best-effort terminal lifecycle, preserving the first cleanup failure. */
#include "input_state.hpp"

namespace atlas::ui
{
/// @brief Stop every retained handler even when a previous cleanup notification throws.
void input_session::shutdown(std::uint64_t frame)
{
    impl_->check();
    if (impl_->terminal)
    {
        return;
    }

    if (!impl_->stats.closed && !impl_->stats.failed)
    {
        throw std::logic_error("end the UI input frame before shutdown");
    }

    if (!frame || frame <= impl_->stats.frame)
    {
        throw std::invalid_argument("shutdown needs a newer frame");
    }

    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"ui.input.shutdown", "ui.dispatch"}};
    impl_->tree.commands().begin_frame(frame);
    impl_->stats = {frame, 0, 0, false, false};
    impl_->terminal = true;
    std::exception_ptr failure;
    const auto attempt = [&](auto action)
    {
        try
        {
            action();
        }
        catch (...)
        {
            if (!failure)
            {
                failure = std::current_exception();
            }
        }
    };
    attempt(
        [&]
        {
            impl_->clear_capture();
        });
    attempt(
        [&]
        {
            impl_->set_focus({});
        });
    attempt(
        [&]
        {
            impl_->set_hover({});
        });
    for (auto& binding : impl_->bindings)
    {
        if (binding.enabled)
        {
            binding.enabled = false;
            attempt(
                [&]
                {
                    (void)impl_->send(binding.target, event_kind::disable, route_phase::direct,
                                      binding.target);
                });
        }

        if (binding.started && !binding.ended)
        {
            binding.ended = true;
            attempt(
                [&]
                {
                    (void)impl_->send(binding.target, event_kind::end, route_phase::direct,
                                      binding.target);
                });
        }
    }

    impl_->bindings.clear();
    impl_->stats.closed = true;
    impl_->stage = 0;
    if (failure)
    {
        impl_->fail();
        std::rethrow_exception(failure);
    }
}
} // namespace atlas::ui
