/** @file @brief Streamed textured primitive shared by both application composition roots. */
#pragma once
#include <atlas/renderer/passes.hpp>
#include <atlas/renderer_content/gpu_content.hpp>
#include <atlas/renderer_content/loader.hpp>

namespace atlas::application
{
/// @brief Minimal renderer consumer; owns no UI semantics or storage access.
class primitive_scene
{
  public:
    /// @brief Create reusable geometry and pipeline on the application's existing device.
    explicit primitive_scene(renderer::device& graphics, renderer::recording_executor& executor,
                             renderer_content::content_loader& content,
                             diagnostics::services destinations = {})
        : resources_(graphics), passes_(resources_), executor_(executor),
          pipelines_(passes_, 4, destinations), destinations_(std::move(destinations))
    {
        const std::array<renderer::vertex, 6> quad{{{{-0.7F, 0.7F, 0}, {1, 1, 1, 1}, {0, 0}},
                                                    {{0.7F, 0.7F, 0}, {1, 1, 1, 1}, {1, 0}},
                                                    {{-0.7F, -0.7F, 0}, {1, 1, 1, 1}, {0, 1}},
                                                    {{-0.7F, -0.7F, 0}, {1, 1, 1, 1}, {0, 1}},
                                                    {{0.7F, 0.7F, 0}, {1, 1, 1, 1}, {1, 0}},
                                                    {{0.7F, -0.7F, 0}, {1, 1, 1, 1}, {1, 1}}}};
        geometry_ = resources_.create_buffer({sizeof(quad)});
        resources_.upload(geometry_, std::as_bytes(std::span{quad}));
        image_.emplace(content.read_image({"renderer.checker", 0, ATLAS_DEMO_IMAGE_SIZE}));
        shader_.emplace(content.read_shader({"renderer.quad", 0, ATLAS_DEMO_SHADER_SIZE}));
    }

    bool content_ready()
    {
        return texture_ && texture_->ready() && pipeline_.owner != 0;
    } ///< Observe upload and shader readiness on the coordinator.

    bool content_presented() const noexcept
    {
        return content_presented_;
    } ///< Whether a textured frame reached presentation.

    void collect()
    {
        passes_.collect();
        resources_.collect();
    } ///< Collect completed GPU timings and retired resources on the render coordinator.

    /// @brief Bound CPU lead, resize the output after surface synchronization, then submit/present.
    renderer::frame_result render(renderer::presentation_surface& surface,
                                  renderer::surface_extent extent, std::uint64_t frame)
    {
        resources_.wait(in_flight_[frame % in_flight_.size()]);
        passes_.collect();
        resources_.collect();
        if (image_ && image_->ready())
        {
            texture_ = std::make_unique<renderer_content::texture_upload>(resources_, image_->get(),
                                                                          destinations_);
            image_.reset();
        }

        if (shader_ && shader_->ready())
        {
            renderer::pipeline_description description;
            description.program = renderer::shader_program::custom;
            description.source = shader_->get();
            description.linear_sampling = false;
            pipeline_ = pipelines_.get(description);
            shader_.reset();
        }

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
        const bool draw_content = content_ready();
        if (draw_content)
        {
            renderer::draw_packet draw;
            draw.pipeline = pipeline_;
            draw.vertices = geometry_;
            draw.texture = texture_->binding().view;
            draw.vertex_count = 6;
            pass.draws.push_back(draw);
        }

        const std::array descriptions{pass};
        in_flight_[frame % in_flight_.size()] =
            passes_.submit(renderer::frame_plan{descriptions}, executor_);
        const auto result = surface.present(resources_, output_);
        content_presented_ =
            content_presented_ || (draw_content && result == renderer::frame_result::presented);
        return result;
    }

  private:
    renderer::resource_store resources_;         ///< Fence-retained geometry and output storage.
    renderer::pass_renderer passes_;             ///< Bounded generic pass executor.
    renderer::recording_executor& executor_;     ///< Borrowed application scheduling policy.
    renderer_content::pipeline_cache pipelines_; ///< Exact-source device-local PSO reuse.
    diagnostics::services destinations_; ///< Shared library logging and profiling endpoints.
    std::optional<renderer_content::image_request>
        image_; ///< Nonblocking streamed image acquisition.
    std::optional<renderer_content::shader_request>
        shader_; ///< Ready-job shader source preparation.
    std::unique_ptr<renderer_content::texture_upload>
        texture_;                        ///< Retained decoded/uploaded image ownership.
    renderer::buffer_handle geometry_;   ///< Immutable uploaded quad vertices.
    renderer::pipeline_handle pipeline_; ///< Borrowed cached textured pipeline.
    renderer::texture_handle output_;    ///< Current offscreen color target.
    renderer::surface_extent extent_;    ///< Current allocated output dimensions.
    std::array<renderer::completion_token, 2> in_flight_{}; ///< At most two CPU submissions ahead.
    bool content_presented_ = false; ///< Smoke completion requires a real textured presentation.
};
} // namespace atlas::application
