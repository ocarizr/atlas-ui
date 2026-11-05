/** @file @brief Render-thread UI consumer with bounded streamed-image residency. */
#pragma once
#include <atlas/ui_images/image_renderer.hpp>

namespace atlas::application
{
class primitive_scene
{
  public:
    primitive_scene(renderer::device& graphics, renderer::recording_executor& executor,
                    std::shared_ptr<std::pmr::memory_resource> memory,
                    diagnostics::services destinations = {})
        : resources_(graphics), passes_(resources_), executor_(executor),
          images_(resources_, passes_, {}, std::move(memory), destinations),
          destinations_(std::move(destinations))
    {
    } ///< Create only graphics state; main owns acquisition and immutable image metadata.

    /// @brief A decoded asset has actually reached presentation.
    bool content_presented() const noexcept
    {
        return content_presented_;
    }

    ///< A real decoded asset, rather than its placeholder, reached presentation.
    /// @brief Reclaim completed GPU resources without blocking.
    void collect()
    {
        images_.collect();
        passes_.collect();
        resources_.collect();
    }

    ///< Reclaim only completed resources on the render coordinator.
    renderer::frame_result render(renderer::presentation_surface& surface,
                                  renderer::surface_extent extent, std::uint64_t frame,
                                  const ui_images::frame_payload* packet)
    {
        const diagnostics::operation_scope scope{destinations_,
                                                 {"application.ui.render", "application"}};
        const auto slot = frame % in_flight_.size();
        resources_.wait(in_flight_[slot]);
        collect();
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

        bool asset_drawn = false;
        if (packet && images_.ready())
        {
            in_flight_[slot] = images_.submit(*packet, output_, extent, &executor_);
            asset_drawn = images_.statistics().drawn_images != 0;
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
            content_presented_ || (asset_drawn && result == renderer::frame_result::presented);
        return result;
    } ///< Pending/failing assets remain drawable placeholders; no decode/IO waits occur here.
  private:
    renderer::resource_store resources_;     ///< Native resource lifetime and aligned budgets.
    renderer::pass_renderer passes_;         ///< Generic ordered renderer.
    renderer::recording_executor& executor_; ///< Shared application job adapter.
    ui_images::image_renderer
        images_; ///< Logical residency, fallback and automatic fence tracking.
    diagnostics::services destinations_; ///< Optional logging/profiling destinations.
    renderer::texture_handle output_;    ///< Resized presentation source.
    renderer::surface_extent extent_;    ///< Current output dimensions.
    std::array<renderer::completion_token, 2> in_flight_{}; ///< Bounded CPU lead.
    bool content_presented_ = false; ///< Smoke-test success requires actual decoded image content.
}; ///< Shared technology demonstration without game-specific or authoring logic.
} // namespace atlas::application
