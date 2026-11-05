/** @file @brief Window lifecycle and platform selection. */
#include "presentation_endpoint.hpp"
#include "window_backend.hpp"
#include <atlas/window/window.hpp>

namespace atlas::window
{
/// @brief Owner-thread guard shared by all facade methods.
class window::impl
{
  public:
    std::thread::id owner = std::this_thread::get_id();      ///< Native owner.
    std::shared_ptr<detail::window_backend> backend;         ///< Retained platform implementation.
    std::shared_ptr<detail::presentation_endpoint> endpoint; ///< Optional cross-thread snapshot.

    void publish() const
    {
        if (endpoint)
        {
            endpoint->publish(backend->state());
        }
    } ///< Publish only from the native owner.

    /// @brief Throw an operation-specific diagnostic before continuing after a native failure.
    void check() const
    {
        if (owner != std::this_thread::get_id())
        {
            throw std::logic_error("window requires its owner thread");
        }
    }
};

/// @brief Reject invalid configuration before allocating or mutating native resources.
void window_options::validate() const
{
    if (width == 0 || height == 0 || width > 16384 || height > 16384 || title.empty() ||
        title.find('\0') != std::string::npos)
    {
        throw std::invalid_argument("window requires a title and client dimensions in [1, 16384]");
    }
}

/// @brief Construct the compiled platform adapter or report an unavailable backend.
window::window(const window_options& options)
{
    options.validate();
    impl_ = std::make_unique<impl>();
    impl_->backend = detail::make_window_backend(options);
}

/// @brief Release retained resources on the owner thread after outstanding work completes.
window::~window()
{
    if (impl_->owner != std::this_thread::get_id())
    {
        std::terminate();
    }

    if (impl_->endpoint && impl_->endpoint.use_count() != 1)
    {
        std::terminate();
    }

    impl_->backend->close();
    impl_->publish();
}

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
window_state window::poll_events()
{
    impl_->check();
    const auto state = impl_->backend->poll();
    impl_->publish();
    return state;
}

/// @brief Native callbacks only enqueue values; application dispatch runs after polling returns.
input_batch window::take_input()
{
    impl_->check();
    return impl_->backend->take_input();
}

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
window_state window::state() const
{
    impl_->check();
    impl_->publish();
    return impl_->backend->state();
}

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
void window::resize(std::uint32_t width, std::uint32_t height)
{
    impl_->check();
    window_options options;
    options.width = width;
    options.height = height;
    options.validate();
    impl_->backend->resize(width, height);
    impl_->publish();
}

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
void window::request_close()
{
    impl_->check();
    impl_->backend->close();
    impl_->publish();
}

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
void window::wait_for_events(std::uint32_t timeout_ms)
{
    impl_->check();
    impl_->backend->wait(timeout_ms);
}

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
std::shared_ptr<detail::window_backend> detail::window_access::retain(window& value)
{
    value.impl_->check();
    return value.impl_->backend;
}

/// @brief Grant an endpoint while preserving main-thread native lifetime ownership.
presentation_target window::share_presentation()
{
    impl_->check();
    if (!impl_->endpoint)
    {
        impl_->endpoint = std::make_shared<detail::presentation_endpoint>(impl_->backend);
    }

    return presentation_target{impl_->endpoint};
}

/// @brief Retain immutable handle state and its synchronized snapshot channel.
presentation_target::presentation_target(std::shared_ptr<detail::window_backend> backend)
    : backend_(std::move(backend))
{
}

/// @brief Lease reads do not dispatch native messages or call the window facade.
window_state presentation_target::state() const
{
    if (!backend_)
    {
        throw std::logic_error("presentation target was moved from");
    }

    return backend_->state();
}

/// @brief Retain a published endpoint without accessing the main-thread facade.
std::shared_ptr<detail::window_backend>
detail::window_access::retain(const presentation_target& value)
{
    if (!value.backend_)
    {
        throw std::logic_error("presentation target was moved from");
    }

    return value.backend_;
}
} // namespace atlas::window
