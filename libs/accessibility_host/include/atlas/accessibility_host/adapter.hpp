/**
 * @file
 * @brief Platform-selected accessibility endpoint attached to an application window.
 */
#pragma once
#include <atlas/accessibility/channel.hpp>
#include <atlas/window/window.hpp>

namespace atlas::accessibility_host
{
/// @brief Physical client origin and logical-unit scale, captured only by the native owner.
struct screen_transform
{
    double x = 0;     ///< Physical screen client origin.
    double y = 0;     ///< Physical screen client origin.
    double scale = 1; ///< Physical pixels per logical unit.
};

bool available() noexcept; ///< Whether a supported native adapter was compiled.

/// @brief Window owner attachment; destroy before its borrowed window and close its channel first.
class adapter
{
  public:
    adapter(window::window& window, std::shared_ptr<accessibility::channel> source,
            diagnostics::services sinks = {}); ///< Attach without exposing SDK types.
    ~adapter();                       ///< Disconnect all retained providers on the creating thread.
    adapter(const adapter&) = delete; ///< One native attachment.
    adapter& operator=(const adapter&) = delete; ///< One native attachment.
    void notify(); ///< After semantic publication, raise committed-state change events.
    screen_transform coordinates() const; ///< Refresh after window movement/DPI changes.
  private:
    class impl;                  ///< Selected platform implementation.
    std::unique_ptr<impl> impl_; ///< Native resources remain private.
};
} // namespace atlas::accessibility_host
