/**
 * @file
 * @brief Thread-affine graphics device and presentation surfaces, independent of UI content.
 */
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <string>
#include <vector>

namespace atlas::window
{
/// @brief Forward declaration of the platform-neutral native window facade.
class window;
} // namespace atlas::window

namespace atlas::renderer
{
/// @brief Device adapter selection policy.
enum class adapter_preference : std::uint8_t
{
    /// @brief Try hardware adapters first and fall back to WARP if none can create a D3D12 device.
    prefer_hardware,
    /// @brief Select the WARP software adapter explicitly.
    software
};
/// @brief Optional native validation, with an explicit required mode for development.
enum class debug_validation : std::uint8_t
{
    /// @brief Do not request the native debug layer.
    disabled,
    /// @brief Enable native validation when the installed SDK provides it.
    when_available,
    /// @brief Fail initialization if native validation cannot be enabled.
    required
};

/// @brief Device-wide initialization and bounded coordinator waits.
struct device_options
{
    adapter_preference adapter =
        adapter_preference::prefer_hardware; ///< Fall back to software if needed.
    debug_validation debug =
        debug_validation::when_available;     ///< Report whether validation was enabled.
    std::uint32_t gpu_wait_timeout_ms = 5000; ///< Diagnostic deadline, never a worker wait.
    void validate() const;                    ///< Reject zero timeout and invalid enum values.
};

/// @brief Bounded buffering for one native presentation surface.
struct surface_options
{
    std::uint32_t frame_count = 2; ///< Two or three independently fenced frame slots.
    bool vsync = true;             ///< Synchronize presentation to display refresh.
    void validate() const;         ///< Reject unsupported frame counts.
};

/// @brief Physical pixel dimensions; either zero dimension suspends rendering.
struct surface_extent
{
    std::uint32_t width = 0;  ///< Pixel width.
    std::uint32_t height = 0; ///< Pixel height.
    void validate() const;    ///< Reject dimensions greater than 16384.
};

/// @brief Temporary foundation workload, replaced by draw packets in a later phase.
struct frame_description
{
    std::array<float, 4> clear_color{0.04F, 0.06F, 0.1F, 1.0F}; ///< Linear RGBA clear values.
    bool draw_triangle = true; ///< Exercise a real graphics pipeline without UI dependencies.
    void validate() const;     ///< Reject nonfinite or out-of-range normalized color values.
};
/// @brief Submission outcome; occluded and suspended surfaces should not busy-spin.
enum class frame_result : std::uint8_t
{
    /// @brief A graphics frame was submitted and presented.
    presented,
    /// @brief Presentation is occluded; the coordinator should wait before trying again.
    occluded,
    /// @brief The surface is minimized or has a zero extent; no commands were submitted.
    suspended,
    /// @brief The native owner requested close; no commands were submitted.
    surface_closed
};

/// @brief Diagnostic readback, with tightly packed RGBA8 rows from top to bottom.
struct frame_image
{
    surface_extent extent;          ///< Image extent.
    std::vector<std::uint8_t> rgba; ///< Owned CPU bytes, copied after GPU completion.
};

/// @brief Observable device selection and validation state.
struct device_info
{
    std::string adapter_name;        ///< UTF-8 adapter name.
    bool software = false;           ///< Whether the selected adapter is software.
    bool debug_layer_active = false; ///< Whether native validation was actually enabled.
};

bool backend_available() noexcept; ///< Whether the native graphics backend was compiled.

namespace detail
{
/// @brief Opaque graphics device implementation retained by presentation surfaces.
class device_backend;
/// @brief Opaque presentation implementation with exclusive coordinator access.
class surface_backend;
} // namespace detail

/// @brief One graphics queue and fence timeline, used exclusively on its creating thread.
class device
{
  public:
    /// @brief Validate device options and retain the caller-selected CPU allocation resource.
    explicit device(device_options options = {},
                    std::shared_ptr<std::pmr::memory_resource> memory = {});
    ~device(); ///< Drain retained GPU work on the owner thread; device removal is terminal in phase
               ///< one.
    /// @brief Preserve unique, thread-affine ownership; copying is forbidden.
    device(const device&) = delete;
    /// @brief Preserve unique, thread-affine ownership; copying is forbidden.
    device& operator=(const device&) = delete;
    device_info info() const; ///< Query adapter and validation state.
    void wait_idle();         ///< Explicit coordinator sync point; never call from a worker job.
    std::uint64_t debug_error_count()
        const; ///< Count native error/corruption messages when validation is active.
  private:
    std::shared_ptr<detail::device_backend> backend_; ///< Shared lifetime retained by surfaces.
    friend class presentation_surface;
};

/// @brief Native swapchain whose lifetime retains both device and window implementations.
class presentation_surface
{
  public:
    /// @brief Create presentation storage while retaining both native device and window lifetimes.
    presentation_surface(device& graphics, window::window& target, surface_options options = {});
    ~presentation_surface(); ///< Drain before releasing backbuffers.
    /// @brief Preserve unique, thread-affine ownership; copying is forbidden.
    presentation_surface(const presentation_surface&) = delete;
    /// @brief Preserve unique, thread-affine ownership; copying is forbidden.
    presentation_surface& operator=(const presentation_surface&) = delete;
    void resize(surface_extent extent); ///< Drain only when changing a nonzero backing extent.
    frame_result render_frame(const frame_description& frame = {}); ///< Submit on the owner thread.
    frame_image
    readback_last_frame(); ///< Blocking diagnostic operation, not a production frame path.
    std::uint64_t
    submitted_frames() const; ///< Count executed graphics frames, excluding readback commands.
  private:
    std::unique_ptr<detail::surface_backend> backend_; ///< Opaque native surface.
};
} // namespace atlas::renderer
