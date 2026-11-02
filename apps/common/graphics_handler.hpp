/** @file @brief Render-thread graphics composition shared by game and editor. */
#pragma once
#include "primitive_scene.hpp"
#include <atlas/render_host/render_loop.hpp>
#include <atlas/renderer_jobs/executor.hpp>
#include <atomic>

namespace atlas::application
{
/// @brief Atomic presentation feedback without transferring mutable scene ownership to main.
struct presentation_feedback
{
    std::atomic<std::uint64_t> presented{0}; ///< Completed textured presentations.
    std::atomic<renderer::frame_result> result{
        renderer::frame_result::suspended}; ///< Visibility hint.
};

/// @brief Own all thread-affine graphics and streamer lifetime on the dedicated coordinator.
class graphics_handler final : public render_host::frame_handler
{
  public:
    graphics_handler(jobs::job_system& workers, const window::presentation_target& target,
                     const std::filesystem::path& content_root, bool software,
                     const std::shared_ptr<std::pmr::memory_resource>& memory,
                     const diagnostics::services& destinations, presentation_feedback& presented)
        : destinations_(destinations), graphics_(options(software), memory, destinations),
          surface_(graphics_, target),
          streamer_(
              workers,
              {{"renderer.checker", streaming::loose_file_asset{content_root / "checker.bmp"}},
               {"renderer.quad", streaming::loose_file_asset{content_root / "quad.hlsl"}}},
              {}, memory, destinations),
          content_(streamer_, renderer_content::make_image_decoder(), memory, destinations),
          recording_(workers.dispatcher()), scene_(graphics_, recording_, content_, destinations),
          presented_(presented)
    {
        const auto info = graphics_.info();
        diagnostics::write(destinations_, diagnostics::log_level::info, "renderer",
                           info.adapter_name + (info.debug_layer_active
                                                    ? "; debug layer active"
                                                    : "; debug layer unavailable or disabled"));
    } ///< Construct only on the render thread; main retains window/events and the one worker pool.

    void render(const render_host::frame_snapshot& frame) override
    {
        if (frame.window.close_requested)
        {
            return;
        }

        const renderer::surface_extent extent =
            frame.window.minimized
                ? renderer::surface_extent{}
                : renderer::surface_extent{frame.window.width, frame.window.height};
        surface_.resize(extent);
        if (extent.width != 0 && extent.height != 0)
        {
            const auto result = scene_.render(surface_, extent, frame.id);
            presented_.result.store(result, std::memory_order_relaxed);
            if (result == renderer::frame_result::presented && scene_.content_presented())
            {
                presented_.presented.fetch_add(1, std::memory_order_relaxed);
            }
        }
    } ///< Render accepted snapshots in FIFO order without accessing the main-thread facade.

    void shutdown() override
    {
        graphics_.wait_idle();
        scene_.collect();
        streamer_.shutdown();
        if (graphics_.debug_error_count() != 0)
        {
            throw std::runtime_error("native graphics validation reported errors");
        }
    } ///< Drain and validate before releasing graphics and before main releases its window.
  private:
    static renderer::device_options options(bool software)
    {
        renderer::device_options value;
        if (software)
        {
            value.adapter = renderer::adapter_preference::software;
        }

        return value;
    } ///< Select only device policy; thread policy belongs to the composition root.

    diagnostics::services destinations_; ///< Shared optional diagnostics endpoints.
    renderer::device graphics_;          ///< Creating-thread-only GPU ownership.
    renderer::presentation_surface
        surface_;                        ///< Cross-thread window endpoint, never its main facade.
    streaming::asset_streamer streamer_; ///< IOCP owner is created/drained on this coordinator.
    renderer_content::content_loader content_; ///< Ready-job decoding over the application pool.
    renderer_jobs::executor recording_;        ///< Reuse the application-owned worker dispatcher.
    primitive_scene scene_;            ///< Thread-affine GPU content and immutable draw packets.
    presentation_feedback& presented_; ///< Only atomic visibility/completion hints cross to main.
};
} // namespace atlas::application
