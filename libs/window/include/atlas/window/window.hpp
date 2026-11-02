/**
 * @file
 * @brief Platform-neutral native-window lifetime and physical client extents.
 */
#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace atlas::window
{
namespace detail
{
/// @brief Private bridge retaining native window lifetime without public SDK types.
class window_access;
} // namespace detail

/// @brief Initial client-area configuration; dimensions are physical pixels.
struct window_options
{
    std::string title = "Atlas"; ///< UTF-8 window title.
    std::uint32_t width = 960;   ///< Initial client width.
    std::uint32_t height = 540;  ///< Initial client height.
    bool visible = true;         ///< Show the window after creation.
    void validate() const;       ///< Reject invalid sizes and ambiguous titles.
};

/// @brief Snapshot produced by processing native window messages.
struct window_state
{
    std::uint32_t width = 0;      ///< Current physical client width.
    std::uint32_t height = 0;     ///< Current physical client height.
    float dpi_scale = 1;          ///< Native DPI relative to 96 DPI.
    bool minimized = false;       ///< Rendering should suspend while minimized.
    bool close_requested = false; ///< Application should leave its loop.
};

bool backend_available() noexcept; ///< Whether a native backend was compiled.

/// @brief Thread-affine window; presentation surfaces can retain its native lifetime.
class window
{
  public:
    explicit window(const window_options& options = {}); ///< Create on the calling thread.
    ~window(); ///< Release on the creating thread after presentation surfaces.
    /// @brief Preserve unique, thread-affine ownership; copying is forbidden.
    window(const window&) = delete;
    /// @brief Preserve unique, thread-affine ownership; copying is forbidden.
    window& operator=(const window&) = delete;
    window_state poll_events(); ///< Dispatch pending messages on the owner thread.
    window_state state() const; ///< Read the current native state on the owner thread.
    void resize(std::uint32_t width, std::uint32_t height); ///< Set a nonzero client extent.
    void request_close(); ///< Mark closed and hide without invalidating retained surfaces.
    void
    wait_for_events(std::uint32_t timeout_ms = 100); ///< Sleep the coordinator, never a job worker.
  private:
    class impl;                  ///< Opaque native lifetime and owner-thread state.
    std::unique_ptr<impl> impl_; ///< Owned facade implementation.
    friend class detail::window_access;
};
} // namespace atlas::window
