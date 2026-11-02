/** @file @brief Generic triangle composition shared by both application entry points. */
#pragma once
#include <atlas/renderer/passes.hpp>

namespace atlas::application
{
/// @brief Minimal renderer consumer; owns no UI semantics or storage access.
class primitive_scene
{
  public:
    /// @brief Create reusable geometry and pipeline on the application's existing device.
    explicit primitive_scene(renderer::device& graphics, renderer::recording_executor& executor)
        : resources_(graphics), passes_(resources_), executor_(executor)
    {
        const std::array<renderer::vertex, 3> triangle{{{{0, 0.7F, 0}, {1, 0, 0, 1}, {}},
                                                        {{0.7F, -0.7F, 0}, {0, 1, 0, 1}, {}},
                                                        {{-0.7F, -0.7F, 0}, {0, 0, 1, 1}, {}}}};
        geometry_ = resources_.create_buffer({sizeof(triangle)});
        resources_.upload(geometry_, std::as_bytes(std::span{triangle}));
        pipeline_ = passes_.create_pipeline({});
    }

    /// @brief Bound CPU lead, resize the output after surface synchronization, then submit/present.
    renderer::frame_result render(renderer::presentation_surface& surface,
                                  renderer::surface_extent extent, std::uint64_t frame)
    {
        resources_.wait(in_flight_[frame % in_flight_.size()]);
        passes_.collect();
        resources_.collect();
        if (output_.owner == 0 || extent_.width != extent.width || extent_.height != extent.height)
        {
            if (output_.owner != 0)
            {
                resources_.release(output_);
                resources_.collect();
            }

            output_ = resources_.create_texture(
                {extent.width, extent.height, renderer::texture_role::color_target});
            extent_ = extent;
        }

        renderer::pass_description pass;
        pass.color.target = output_;
        pass.color.clear_color = {0.04F, 0.05F, 0.08F, 1};
        renderer::draw_packet draw;
        draw.pipeline = pipeline_;
        draw.vertices = geometry_;
        draw.vertex_count = 3;
        pass.draws.push_back(draw);
        const std::array descriptions{pass};
        in_flight_[frame % in_flight_.size()] =
            passes_.submit(renderer::frame_plan{descriptions}, executor_);
        return surface.present(resources_, output_);
    }

  private:
    renderer::resource_store resources_;     ///< Fence-retained geometry and output storage.
    renderer::pass_renderer passes_;         ///< Bounded generic pass executor.
    renderer::recording_executor& executor_; ///< Borrowed application scheduling policy.
    renderer::buffer_handle geometry_;       ///< Immutable uploaded triangle vertices.
    renderer::pipeline_handle pipeline_;     ///< Reusable colored pipeline.
    renderer::texture_handle output_;        ///< Current offscreen color target.
    renderer::surface_extent extent_;        ///< Current allocated output dimensions.
    std::array<renderer::completion_token, 2> in_flight_{}; ///< At most two CPU submissions ahead.
};
} // namespace atlas::application
