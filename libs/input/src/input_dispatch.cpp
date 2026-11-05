/** @file @brief Monotonic, bounded owner dispatch with deferred mapping activation. */
#include "input_state.hpp"
#include <cmath>

namespace atlas::input
{
/// @brief All metadata validation precedes the first callback or control-state publication.
void input_system::dispatch(std::uint64_t frame, double time_seconds, std::span<const event> events,
                            input_listener& listener)
{
    impl_->check();
    if (impl_->stats.failed)
    {
        throw std::logic_error("reset failed input dispatch before continuing");
    }

    if (!frame || frame <= impl_->stats.frame || !std::isfinite(time_seconds) ||
        time_seconds < impl_->time)
    {
        throw std::invalid_argument(
            "input dispatch requires increasing frame and monotonic finite time");
    }

    if (events.size() > impl_->limits.max_events ||
        impl_->epoch >
            std::numeric_limits<std::uint64_t>::max() - events.size() - impl_->contexts.size() - 1)
    {
        throw std::overflow_error("input event/epoch capacity exhausted");
    }

    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"input.dispatch", "input.cpu"}};
    impl_->preflight(events);
    impl_->stats = {frame, 0, 0, false};
    impl_->time = time_seconds;
    impl_->listener = &listener;
    try
    {
        bool changed = false;
        for (const auto request : impl_->pending)
        {
            auto& context = impl_->context(request.context);
            if (context.enabled != request.enabled)
            {
                context.enabled = request.enabled;
                context.rearm = true;
            }
        }

        impl_->pending.clear();
        for (auto& context : impl_->contexts)
        {
            if (!context.rearm)
            {
                continue;
            }

            context.rearm = false;
            changed = true;
            if (context.enabled)
            {
                context.activated = ++impl_->epoch;
            }
        }

        if (changed)
        {
            impl_->recompute(true);
        }

        for (const auto& value : events)
        {
            ++impl_->stats.events;
            impl_->raw(value);
            impl_->apply(value);
            const bool cancel = value.kind == event_kind::focus_lost ||
                                value.kind == event_kind::source_reset ||
                                value.kind == event_kind::device_disconnected ||
                                value.kind == event_kind::pointer_cancel;
            impl_->recompute(cancel);
        }

        impl_->recompute(false);
        impl_->listener = nullptr;
    }
    catch (...)
    {
        impl_->fail();
        throw;
    }
}
} // namespace atlas::input
