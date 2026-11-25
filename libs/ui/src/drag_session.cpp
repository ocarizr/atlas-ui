/** @file @brief Window-scoped admission, weak leases and explicit transfer confirmation. */
#include "drag_state.hpp"
#include <exception>
#include <limits>

namespace atlas::ui
{
/// @brief Validate independent work budgets and reserve bounded view leases.
drag_drop_session::impl::impl(drag_limits policy,
                              std::shared_ptr<std::pmr::memory_resource> resource,
                              diagnostics::services services)
    : limits(policy), memory(std::move(resource)), sinks(std::move(services)),
      views(0, memory.get())
{
    if (!limits.max_views || limits.max_views > 1024 || !limits.max_queries ||
        limits.max_queries > 16777216)
    {
        throw std::invalid_argument("invalid drag domain limits");
    }

    views.reserve(limits.max_views);
}

/// @brief Check thread affinity and non-reentrant frame admission before mutable access.
void drag_drop_session::impl::check(bool require_open) const
{
    jobs::require_coordinator_thread();
    if (owner != std::this_thread::get_id() || busy || (require_open && !open))
    {
        throw std::logic_error("drag domain requires its non-reentrant open owner");
    }
}

/// @brief Own one window interaction domain without a private pool or native dependencies.
drag_drop_session::drag_drop_session(drag_limits limits,
                                     std::shared_ptr<std::pmr::memory_resource> memory,
                                     diagnostics::services sinks)
{
    jobs::require_coordinator_thread();
    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](auto*)
                  {
                  }};
    }

    impl_ = std::make_unique<impl>(limits, std::move(memory), std::move(sinks));
}

/// @brief Own one window interaction domain without a private pool or native dependencies.
drag_drop_session::~drag_drop_session()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy)
    {
        std::terminate();
    }

    if (auto lease = impl_->source.lifetime.lock(); lease && lease->owner)
    {
        auto& state = *lease->owner->impl_;
        if (state.capture_target == impl_->source.target)
        {
            state.capture_target = {};
            state.dragging.reset();
        }
    }

    impl_->release_views();
}

/// @brief Validate unambiguous semantic bindings at an idle boundary.
void drag_drop_session::configure(drag_actions actions, bool keyboard)
{
    impl_->check(false);
    if (impl_->open || impl_->feedback.phase != drag_phase::idle)
    {
        throw std::logic_error("configure drag controls between idle frames");
    }

    const std::array ids{actions.lift,     actions.confirm, actions.cancel, actions.next,
                         actions.previous, actions.copy,    actions.move};
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        for (std::size_t j = i + 1; j < ids.size(); ++j)
        {
            if (ids[i] && ids[i] == ids[j])
            {
                throw std::invalid_argument("duplicate drag action");
            }
        }
    }

    impl_->actions = actions;
    impl_->keys = keyboard;
}

/// @brief Lease matching open input phases in explicit back-to-front window order.
void drag_drop_session::begin_frame(std::uint64_t frame, std::span<input_session* const> views)
{
    impl_->check(false);
    if (impl_->open || !frame || frame <= impl_->frame || views.size() > impl_->limits.max_views)
    {
        throw std::invalid_argument("invalid drag domain frame");
    }

    for (std::size_t i = 0; i < views.size(); ++i)
    {
        if (!views[i])
        {
            throw std::invalid_argument("null drag view");
        }

        auto& state = *views[i]->impl_;
        state.open();
        if (state.stage != 2 || state.drag_frame)
        {
            throw std::logic_error("drag view must be in its unborrowed input phase");
        }

        for (std::size_t j = 0; j < i; ++j)
        {
            if (views[j] == views[i])
            {
                throw std::invalid_argument("duplicate drag view");
            }
        }
    }

    for (auto* input : views)
    {
        impl_->views.emplace_back(input->impl_->lifetime);
        input->impl_->drag_frame = impl_.get();
    }

    impl_->frame = frame;
    impl_->queries = impl_->packets = 0;
    impl_->open = true;
    impl_->run(
        [&]
        {
            if (impl_->feedback.phase != drag_phase::idle && !impl_->valid_source())
            {
                impl_->cancel(impl_->view(impl_->source) ? drag_cancel_reason::source_unavailable
                                                         : drag_cancel_reason::context_lost);
            }
            else if (impl_->feedback.phase == drag_phase::pending &&
                     !impl_->preflight(impl_->ticket))
            {
                impl_->cancel(drag_cancel_reason::target_unavailable);
            }
            else if (impl_->feedback.phase == drag_phase::dragging && !impl_->feedback.keyboard)
            {
                impl_->point(impl_->feedback.position);
            }
        });
}

/// @brief Release borrowing guards even when an input owner has been retired.
void drag_drop_session::impl::release_views() noexcept
{
    for (const auto& weak : views)
    {
        if (auto lease = weak.lock();
            lease && lease->owner && lease->owner->impl_->drag_frame == this)
        {
            lease->owner->impl_->drag_frame = nullptr;
        }
    }

    views.clear();
    open = false;
}

/// @brief Release every view lease before participants advance their system stages.
void drag_drop_session::end_frame()
{
    impl_->check();
    impl_->release_views();
}

/// @brief Validate and route one replayable packet through the window drag state machine.
bool drag_drop_session::process(const input_event& event)
{
    return impl_->run(
        [&]
        {
            validate_input_events(std::span{&event, 1});
            return impl_->process(event);
        });
}

/// @brief Lift a configured source through keyboard or application semantic input.
bool drag_drop_session::lift(input_session& owner, element_handle source, drag_operation preferred)
{
    return impl_->run(
        [&]
        {
            return impl_->start(impl_->capture(owner, source), preferred, true);
        });
}

/// @brief Visit eligible destinations in deterministic presented order.
void drag_drop_session::next_target(bool previous)
{
    impl_->run(
        [&]
        {
            impl_->next(previous);
        });
}

/// @brief Attempt every terminal cleanup while preserving the first callback exception.
void drag_drop_session::cancel(drag_cancel_reason reason)
{
    if (reason <= drag_cancel_reason::none || reason > drag_cancel_reason::callback_failure)
    {
        throw std::invalid_argument("invalid drag cancellation reason");
    }

    impl_->run(
        [&]
        {
            impl_->cancel(reason);
        });
}

/// @brief Return retained transfer values only while a confirmation ticket is pending.
std::optional<drop_request> drag_drop_session::pending_drop() const
{
    impl_->check();
    if (impl_->feedback.phase != drag_phase::pending)
    {
        return {};
    }

    return drop_request{impl_->ticket, impl_->source.target, impl_->target.target, impl_->payload,
                        impl_->feedback.operation};
}

/// @brief Check the exact pending ticket before beginning an atomic application transaction.
bool drag_drop_session::can_commit(std::uint64_t transfer) const
{
    impl_->check();
    return impl_->preflight(transfer);
}

/// @brief Record the application outcome before terminal callbacks can fail.
void drag_drop_session::resolve(std::uint64_t transfer, bool committed)
{
    impl_->check();
    if (!transfer || impl_->feedback.phase != drag_phase::pending || transfer != impl_->ticket)
    {
        throw std::invalid_argument("stale drag transfer ticket");
    }

    impl_->run(
        [&]
        {
            if (!committed)
            {
                impl_->cancel(drag_cancel_reason::rejected);
                return;
            }

            // The app already committed after preflight. Completion errors cannot undo user data.
            impl_->completion = {transfer, true, drag_cancel_reason::none};
            std::exception_ptr failed;
            const auto attempt = [&](auto&& callback)
            {
                try
                {
                    callback();
                }
                catch (...)
                {
                    if (!failed)
                    {
                        failed = std::current_exception();
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
                    (void)impl_->emit(impl_->target, event_kind::drag_completed);
                });
            attempt(
                [&]
                {
                    (void)impl_->emit(impl_->source, event_kind::drag_completed);
                });
            impl_->reset();
            if (failed)
            {
                std::rethrow_exception(failed);
            }
        });
}

/// @brief Observe lightweight render feedback on the owning thread.
drag_feedback drag_drop_session::feedback() const
{
    impl_->check(false);
    return impl_->feedback;
}

/// @brief Observe the last terminal outcome without retaining payload memory.
drag_completion drag_drop_session::completion() const
{
    impl_->check(false);
    return impl_->completion;
}
} // namespace atlas::ui
