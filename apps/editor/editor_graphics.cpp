/** @file @brief Present editor chrome and preview without crossing mutable thread ownership. */
#include "editor_graphics.hpp"

namespace atlas::editor
{
namespace
{
/// @brief Keep renderer adapter selection independent from window and document policy.
renderer::device_options graphics_options(bool software)
{
    renderer::device_options options;
    if (software)
    {
        options.adapter = renderer::adapter_preference::software;
    }

    return options;
}
} // namespace

/// @brief Compose only the resources used by the editor viewport.
editor_graphics::editor_graphics(const window::presentation_target& target, jobs::job_system& jobs,
                                 bool software, const diagnostics::services& sinks,
                                 std::atomic<std::uint64_t>& presented)
    : sinks_{sinks}, device_{graphics_options(software), {}, sinks}, surface_{device_, target},
      resources_{device_}, passes_{resources_}, recording_{jobs.dispatcher()},
      painter_{resources_, passes_, {}, sinks}, presented_{presented}
{
}

/// @brief Preserve the previous presentation while a bounded candidate atlas uploads.
void editor_graphics::update_atlas(std::shared_ptr<const editor_frame> incoming)
{
    if (pending_atlas_ && pending_atlas_->ready())
    {
        active_atlas_ = std::move(pending_atlas_);
        active_ = std::move(pending_);
    }

    if (!pending_atlas_ && (!active_ || active_->atlas != incoming->atlas))
    {
        pending_atlas_ =
            std::make_unique<ui_text::uploaded_atlas>(resources_, *incoming->atlas, 1, sinks_);
        pending_ = std::move(incoming);
    }
}

/// @brief Fence-protect the previous target during a viewport resize.
void editor_graphics::prepare_target(renderer::surface_extent extent)
{
    if (target_.owner != 0 && extent_.width == extent.width && extent_.height == extent.height)
    {
        return;
    }

    if (target_.owner != 0)
    {
        resources_.release(target_);
    }

    target_ = resources_.create_texture(
        {extent.width, extent.height, renderer::texture_role::color_target});
    extent_ = extent;
}

/// @brief Consume a CPU packet and submit its full UI with painter order intact.
void editor_graphics::render(const render_host::frame_snapshot& frame)
{
    const diagnostics::operation_scope scope{sinks_, {"editor.present", "editor"}};
    const renderer::surface_extent extent{frame.window.width, frame.window.height};
    const auto packet = std::dynamic_pointer_cast<const editor_frame>(frame.content);
    if (!packet || frame.window.minimized || frame.window.close_requested || extent.width == 0 ||
        extent.height == 0)
    {
        return;
    }

    surface_.resize(extent);
    auto& fence = fences_[frame.id % fences_.size()];
    resources_.wait(fence);
    resources_.collect();
    update_atlas(packet);
    if (!active_)
    {
        return;
    }

    prepare_target(extent);
    fence = painter_.submit_quads({active_->viewport, active_->quads}, target_, extent,
                                  active_atlas_->bindings(), &recording_);
    if (surface_.present(resources_, target_) == renderer::frame_result::presented)
    {
        presented_.fetch_add(1, std::memory_order_relaxed);
    }
}

/// @brief Drain the device before surface destruction and report native diagnostics.
void editor_graphics::shutdown()
{
    device_.wait_idle();
    if (device_.debug_error_count() != 0)
    {
        throw std::runtime_error("Native graphics validation reported editor errors");
    }
}
} // namespace atlas::editor
