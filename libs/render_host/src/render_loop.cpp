/** @file @brief Persistent application coordinator and bounded snapshot admission. */
#include "loop_state.hpp"
#include <algorithm>

namespace atlas::render_host
{
/// @brief Validate before starting the persistent thread or publishing any borrowed state.
render_loop::render_loop(frame_factory factory, loop_options options,
                         diagnostics::services destinations)
{
    if (!factory || options.capacity == 0 || options.capacity > 64 || options.drain_timeout_ms == 0)
    {
        throw std::invalid_argument("invalid render loop factory or limits");
    }

    impl_ = std::make_unique<impl>(options, std::move(destinations));
    impl_->thread = std::jthread{[state = impl_.get(), factory = std::move(factory)]() mutable
                                 {
                                     state->run(factory);
                                 }};
}

/// @brief Explicit message-pumping drain must precede destruction for native presentation users.
render_loop::~render_loop()
{
    if (impl_->owner != std::this_thread::get_id())
    {
        std::terminate();
    }

    try
    {
        request_stop();
        if (impl_->thread.joinable())
        {
            impl_->thread.join();
        }
    }
    catch (...)
    {
        std::terminate();
    }
}

/// @brief Never block the message owner or overwrite/drop an accepted snapshot on saturation.
bool render_loop::try_submit(frame_snapshot frame)
{
    impl_->check_owner();
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"render_host.admit", "host.main"}};
    const std::lock_guard lock{impl_->mutex};
    if (impl_->stopping)
    {
        return false;
    }

    if (frame.id == 0 || frame.id <= impl_->last_id)
    {
        throw std::invalid_argument("frame IDs must increase");
    }

    if (impl_->counts.queued == impl_->queue.size())
    {
        return false;
    }

    impl_->last_id = frame.id;
    impl_->queue[impl_->tail] = std::move(frame);
    impl_->tail = (impl_->tail + 1) % impl_->queue.size();
    ++impl_->counts.accepted;
    ++impl_->counts.queued;
    impl_->counts.peak_queued = std::max(impl_->counts.peak_queued, impl_->counts.queued);
    impl_->wake.notify_one();
    return true;
}

/// @brief Close admission while preserving already accepted FIFO order.
void render_loop::request_stop()
{
    impl_->check_owner();
    const std::lock_guard lock{impl_->mutex};
    impl_->stopping = true;
    impl_->wake.notify_one();
}

/// @brief Observing completion is safe without acquiring the handoff mutex.
bool render_loop::finished() const noexcept
{
    return impl_->done.load(std::memory_order_acquire);
}

/// @brief Pump outside all locks, then join only after graphics destruction has finished.
void render_loop::stop_and_join(const std::function<void()>& pump)
{
    impl_->check_owner();
    if (!pump)
    {
        throw std::invalid_argument("render drain needs an event pump");
    }

    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"render_host.drain", "host.main"}};
    request_stop();
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds{impl_->options.drain_timeout_ms};
    while (!finished())
    {
        pump();
        if (std::chrono::steady_clock::now() >= deadline)
        {
            diagnostics::write(impl_->destinations, diagnostics::log_level::critical, "render_host",
                               "Render shutdown deadline exceeded");
            std::terminate();
        }

        std::this_thread::yield();
    }

    if (impl_->thread.joinable())
    {
        impl_->thread.join();
    }

    if (impl_->failure)
    {
        std::rethrow_exception(impl_->failure);
    }
}

/// @brief Statistics are snapshots rather than synchronization guarantees for admission.
loop_statistics render_loop::statistics() const
{
    const std::lock_guard lock{impl_->mutex};
    return impl_->counts;
}
} // namespace atlas::render_host
