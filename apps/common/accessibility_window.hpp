/** @file @brief Composition-root lifetime for one window's reusable assistive endpoint. */
#pragma once
#include <atlas/accessibility_host/adapter.hpp>
#include <atlas/ui/accessibility_frame.hpp>

namespace atlas::application
{
/// @brief Close requests before disconnecting providers and releasing the borrowed native window.
class accessibility_window
{
  public:
    accessibility_window(window::window& target, diagnostics::services sinks)
        : source_{std::make_shared<accessibility::channel>(accessibility::limits{}, sinks)}
    {
        source_->publish({});
        if (accessibility_host::available())
        {
            host_ =
                std::make_unique<accessibility_host::adapter>(target, source_, std::move(sinks));
        }
    } ///< Native selection remains in accessibility_host, never in application public APIs.

    ~accessibility_window()
    {
        source_->close();
    } ///< Pending native requests never survive shutdown.

    accessibility_window(const accessibility_window&) = delete; ///< One owner/window attachment.
    accessibility_window&
    operator=(const accessibility_window&) = delete; ///< Never alias lifecycle.

    std::shared_ptr<accessibility::channel> source() const
    {
        return source_;
    } ///< Shared safe ingress.

    ui::accessibility_view coordinates(bool logical_units = true) const
    {
        if (!host_)
        {
            return {};
        }

        const auto value = host_->coordinates();
        return {value.x, value.y, logical_units ? value.scale : 1};
    } ///< Refresh screen origin every frame, including movement without resizing.

    void notify()
    {
        if (host_)
        {
            host_->notify();
        }
    } ///< Only after successful owner publication.
  private:
    std::shared_ptr<accessibility::channel> source_;    ///< Provider ingress; closed on scope exit.
    std::unique_ptr<accessibility_host::adapter> host_; ///< Retires before the native window.
};
} // namespace atlas::application
