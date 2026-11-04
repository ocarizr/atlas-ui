/** @file @brief Render-thread consumer of immutable UI packets and streamer-fed resident images. */
#pragma once
#include <atlas/renderer_content/gpu_content.hpp>
#include <atlas/renderer_content/loader.hpp>
#include <atlas/ui_renderer/paint_renderer.hpp>

namespace atlas::application
{
class primitive_scene
{
  public:
    primitive_scene(renderer::device& graphics, renderer::recording_executor& executor,
                    renderer_content::content_loader& content,
                    std::shared_ptr<std::pmr::memory_resource> memory,
                    diagnostics::services destinations = {})
        : resources_(graphics), passes_(resources_), executor_(executor),
          ui_(resources_, passes_, std::move(memory), destinations),
          destinations_(std::move(destinations))
    {
        image_.emplace(content.read_image({"renderer.checker", 0, ATLAS_DEMO_IMAGE_SIZE}));
    } ///< Construct graphics and initiate content on their creating coordinator.

    /// @brief A real textured UI frame has reached presentation.
    /// @brief A real textured UI frame has reached presentation.
    bool content_presented() const noexcept
    {
        return content_presented_;
    }

    ///< Smoke completion requires a real textured UI frame reaching presentation.

    void collect()
    {
        passes_.collect();
        resources_.collect();
    } ///< Reclaim completed frame resources and timings.

    renderer::frame_result render(renderer::presentation_surface& surface,
                                  renderer::surface_extent extent, std::uint64_t frame,
                                  const ui::paint_snapshot* packet)
    {
        const diagnostics::operation_scope scope{destinations_,
                                                 {"application.ui.render", "application"}};
        const auto slot = frame % in_flight_.size();
        resources_.wait(in_flight_[slot]);
        collect();
        if (image_ && image_->ready())
        {
            texture_ = std::make_unique<renderer_content::texture_upload>(resources_, image_->get(),
                                                                          destinations_);
            image_.reset();
        }

        if (output_.owner == 0 || extent_.width != extent.width || extent_.height != extent.height)
        {
            if (output_.owner != 0)
            {
                resources_.release(output_);
            }

            resources_.collect();
            output_ = resources_.create_texture(
                {extent.width, extent.height, renderer::texture_role::color_target});
            extent_ = extent;
        }

        const bool ready = packet && texture_ && texture_->ready();
        if (ready)
        {
            const std::array bindings{ui_renderer::texture_binding{1, texture_->binding().view}};
            in_flight_[slot] = ui_.submit(*packet, output_, extent, bindings, &executor_);
        }
        else
        {
            renderer::pass_description clear;
            clear.color.target = output_;
            clear.color.clear_color = {0.04f, 0.05f, 0.08f, 1};
            const std::array descriptions{clear};
            in_flight_[slot] = passes_.submit(renderer::frame_plan{descriptions});
        }

        const auto result = surface.present(resources_, output_);
        content_presented_ =
            content_presented_ || (ready && result == renderer::frame_result::presented);
        return result;
    } ///< Bound CPU lead and retain graphics resources through their submission fences.
  private:
    renderer::resource_store resources_;     ///< Native resource ownership and retirement.
    renderer::pass_renderer passes_;         ///< Generic ordered pass implementation.
    renderer::recording_executor& executor_; ///< Application-shared recording adapter.
    ui_renderer::paint_renderer ui_;         ///< Platform-neutral UI packet adapter.
    diagnostics::services destinations_;     ///< Optional runtime instrumentation.
    std::optional<renderer_content::image_request>
        image_; ///< Nonblocking streamed image preparation.
    std::unique_ptr<renderer_content::texture_upload> texture_; ///< Resident image lifecycle.
    renderer::texture_handle output_;                           ///< Resized presentation source.
    renderer::surface_extent extent_;                           ///< Allocated target size.
    std::array<renderer::completion_token, 2> in_flight_{};     ///< Maximum two submissions ahead.
    bool content_presented_ = false;                            ///< Smoke-test completion gate.
}; ///< Shared technology demonstration without editor or game semantics.
} // namespace atlas::application
