/** @file @brief Dedicated render owner for immutable Atlas editor packets. */
#pragma once
#include "editor_view.hpp"
#include <atlas/renderer_jobs/executor.hpp>
#include <atomic>

namespace atlas::editor
{
/// @brief Own the native surface and atlas revisions on the render coordinator.
class editor_graphics final : public render_host::frame_handler
{
  public:
    /// @brief Initialize WARP or hardware with the application job dispatcher.
    editor_graphics(const window::presentation_target& target, jobs::job_system& jobs,
                    bool software, const diagnostics::services& sinks,
                    std::atomic<std::uint64_t>& presented);
    void
    render(const render_host::frame_snapshot& frame) override; ///< Consume one immutable frame.
    void shutdown() override; ///< Drain GPU use and check native validation.

  private:
    void
    update_atlas(std::shared_ptr<const editor_frame> incoming); ///< Poll then admit one revision.
    void prepare_target(renderer::surface_extent extent); ///< Replace the offscreen color target.

    diagnostics::services sinks_;            ///< Optional graphics instrumentation.
    renderer::device device_;                ///< Sole graphics-device owner.
    renderer::presentation_surface surface_; ///< Cross-thread presentation lease.
    renderer::resource_store resources_;     ///< Retains queued GPU objects by fence.
    renderer::pass_renderer passes_;         ///< Shared ordered UI rendering path.
    renderer_jobs::executor recording_;      ///< Application pool for eligible recording.
    ui_renderer::paint_renderer painter_;    ///< Atlas chrome and preview share one pass.
    renderer::texture_handle target_;        ///< Resizable offscreen output.
    renderer::surface_extent extent_;        ///< Current physical output extent.
    std::array<renderer::completion_token, 2> fences_{};     ///< Bounded submission lead.
    std::unique_ptr<ui_text::uploaded_atlas> active_atlas_;  ///< Last usable glyph pages.
    std::unique_ptr<ui_text::uploaded_atlas> pending_atlas_; ///< At most one replacement uploads.
    std::shared_ptr<const editor_frame> active_;             ///< Packet matching the active atlas.
    std::shared_ptr<const editor_frame> pending_; ///< Packet matching the uploading atlas.
    std::atomic<std::uint64_t>& presented_; ///< Only successful native presentations cross back.
};
} // namespace atlas::editor
