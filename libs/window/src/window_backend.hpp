/** @file @brief Private window-to-platform adapter. */
#pragma once
#include <atlas/window/window.hpp>
#include <thread>

namespace atlas::window::detail
{
/// @brief Retained implementation, never part of the installed API.
class window_backend
{
  public:
    /// @brief Release retained resources on the owner thread after outstanding work completes.
    virtual ~window_backend() = default;
    virtual window_state poll() = 0;        ///< Dispatch messages.
    virtual window_state state() const = 0; ///< Query physical client state.
    virtual void resize(std::uint32_t width, std::uint32_t height) = 0; ///< Resize client area.
    virtual void close() = 0;                                           ///< Hide and mark closed.
    virtual void wait(std::uint32_t milliseconds) = 0;                  ///< Wait for messages.
    virtual void* native_handle() const = 0; ///< Backend-only native handle.
};

std::shared_ptr<window_backend>
make_window_backend(const window_options& options); ///< Select compiled backend.

/// @brief Private bridge used by the presentation backend.
class window_access
{
  public:
    static std::shared_ptr<window_backend>
    retain(window& value); ///< Check owner and retain lifetime.
};
} // namespace atlas::window::detail
