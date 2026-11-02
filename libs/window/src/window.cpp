/** @file @brief Window lifecycle and platform selection. */
#include "window_backend.hpp"
#include <atlas/window/window.hpp>

namespace atlas::window
{
/// @brief Owner-thread guard shared by all facade methods.
class window::impl
{
  public:
    std::thread::id owner = std::this_thread::get_id(); ///< Native owner.
    std::shared_ptr<detail::window_backend> backend;    ///< Retained platform implementation.

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

    impl_->backend->close();
}

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
window_state window::poll_events()
{
    impl_->check();
    return impl_->backend->poll();
}

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
window_state window::state() const
{
    impl_->check();
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
}

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
void window::request_close()
{
    impl_->check();
    impl_->backend->close();
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
} // namespace atlas::window
