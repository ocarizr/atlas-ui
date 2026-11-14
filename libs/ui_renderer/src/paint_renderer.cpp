/** @file @brief Coordinator-only upload and order-preserving adjacent draw batching. */
#include "material_shader.hpp"
#include "paint_draws.hpp"
#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>

namespace atlas::ui_renderer
{
class paint_renderer::impl
{
  public:
    impl(renderer::resource_store& store, renderer::pass_renderer& renderer,
         std::shared_ptr<std::pmr::memory_resource> resource, diagnostics::services services)
        : resources(store), passes(renderer), memory(std::move(resource)),
          destinations(std::move(services))
    {
    } ///< Service lifetime is supplied by the composition root.

    void check() const
    {
        if (owner != std::this_thread::get_id() || busy)
        {
            throw std::logic_error("UI renderer requires its non-reentrant GPU coordinator");
        }
    } ///< Check before touching borrowed graphics services.

    /// @brief Compile material pipelines on first use and retain them until graphics teardown.
    void prepare_materials(paint_list list)
    {
        for (const auto& quad : list.quads)
        {
            if (!quad.material)
            {
                continue;
            }

            const auto kind = quad.material->kind;
            auto& pipeline = kind == ui::material_kind::shape ? shape_material : texture_material;
            if (pipeline.owner == 0)
            {
                renderer::pipeline_description description;
                description.program = renderer::shader_program::custom;
                description.source = detail::material_shader;
                description.blend = renderer::blend_mode::straight_alpha;
                description.pixel_parameters = true;
                description.pixel_entry =
                    kind == ui::material_kind::shape ? "shape_main" : "texture_main";
                pipeline = passes.create_pipeline(description);
            }
        }
    }

    renderer::resource_store& resources;               ///< Outlives this adapter.
    renderer::pass_renderer& passes;                   ///< Outlives this adapter.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< CPU upload preparation allocation.
    diagnostics::services destinations;                ///< Optional logging/profiling endpoints.
    const std::thread::id owner = std::this_thread::get_id(); ///< Native graphics owner.
    renderer::pipeline_handle solid;            ///< Reused straight-alpha solid pipeline.
    renderer::pipeline_handle textured;         ///< Reused straight-alpha sampled pipeline.
    renderer::pipeline_handle texture_material; ///< Optional texture parameter shader.
    renderer::pipeline_handle shape_material;   ///< Optional signed-distance shader.
    std::size_t vertex_bytes = 0;               ///< Last successful vertex upload size.
    std::size_t draws = 0;                      ///< Last successful submitted batch count.
    bool busy = false;                          ///< Reject diagnostic callback reentry.
}; ///< No GPU resources cross into immutable UI packets.

/// @brief Roll back the first pipeline if the second pipeline cannot be created.
paint_renderer::paint_renderer(renderer::resource_store& resources, renderer::pass_renderer& passes,
                               std::shared_ptr<std::pmr::memory_resource> memory,
                               diagnostics::services destinations)
{
    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](std::pmr::memory_resource*)
                  {
                  }};
    }

    impl_ = std::make_unique<impl>(resources, passes, std::move(memory), std::move(destinations));
    impl_->busy = true;
    renderer::pipeline_description description;
    description.blend = renderer::blend_mode::straight_alpha;
    impl_->solid = passes.create_pipeline(description);
    try
    {
        description.program = renderer::shader_program::textured;
        impl_->textured = passes.create_pipeline(description);
    }
    catch (...)
    {
        passes.release(impl_->solid);
        throw;
    }

    diagnostics::write(impl_->destinations, diagnostics::log_level::debug, "ui.renderer",
                       "UI pipelines initialized");
    impl_->busy = false;
}

/// @brief Renderer fences retain native pipeline objects after their public handles are released.
paint_renderer::~paint_renderer()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy)
    {
        std::terminate();
    }

    impl_->busy = true;
    if (impl_->shape_material.owner != 0)
    {
        impl_->passes.release(impl_->shape_material);
    }

    if (impl_->texture_material.owner != 0)
    {
        impl_->passes.release(impl_->texture_material);
    }

    impl_->passes.release(impl_->textured);
    impl_->passes.release(impl_->solid);
}

/// @brief Upload one contiguous vertex buffer and release its handle after fenced submission.
renderer::completion_token paint_renderer::submit(const ui::paint_snapshot& snapshot,
                                                  renderer::texture_handle target,
                                                  renderer::surface_extent extent,
                                                  std::span<const texture_binding> textures,
                                                  renderer::recording_executor* recorder)
{
    return submit_quads({snapshot.viewport(), snapshot.quads()}, target, extent, textures,
                        recorder);
}

/// @brief Submit caller-ordered quads without borrowing a mutable UI tree.
renderer::completion_token paint_renderer::submit_quads(paint_list list,
                                                        renderer::texture_handle target,
                                                        renderer::surface_extent extent,
                                                        std::span<const texture_binding> textures,
                                                        renderer::recording_executor* recorder)
{
    impl_->check();
    impl_->busy = true;

    /// @brief Restore the non-reentrant owner scope on every exit.
    struct reset
    {
        bool& busy; ///< Restore after callbacks and cleanup unwind.

        ~reset()
        {
            busy = false;
        } ///< No allocation during cleanup.
    } guard{impl_->busy};

    const diagnostics::operation_scope scope{impl_->destinations, {"ui.renderer.submit", "ui.cpu"}};
    detail::draw_preparation prepared{impl_->memory.get()};
    impl_->prepare_materials(list);
    const detail::paint_pipelines pipelines{impl_->solid, impl_->textured, impl_->texture_material,
                                            impl_->shape_material};
    detail::prepare_draws(list, target, extent, textures, pipelines, impl_->memory.get(), prepared);
    auto& vertices = prepared.vertices;
    auto& pass = prepared.pass;
    renderer::buffer_handle buffer;
    try
    {
        if (!vertices.empty())
        {
            const diagnostics::operation_scope upload{impl_->destinations,
                                                      {"ui.renderer.upload", "ui.cpu"}};
            buffer = impl_->resources.create_buffer({vertices.size() * sizeof(renderer::vertex)});
            impl_->resources.upload(buffer, std::as_bytes(std::span{vertices}));
            for (auto& draw : pass.draws)
            {
                draw.vertices = buffer;
            }
        }

        const auto count = pass.draws.size();
        const std::array descriptions{std::move(pass)};
        const renderer::frame_plan plan{descriptions};
        const auto completion =
            recorder ? impl_->passes.submit(plan, *recorder) : impl_->passes.submit(plan);
        if (buffer.owner != 0)
        {
            impl_->resources.release(buffer);
        }

        impl_->draws = count;
        impl_->vertex_bytes = vertices.size() * sizeof(renderer::vertex);
        return completion;
    }
    catch (...)
    {
        if (buffer.owner != 0)
        {
            impl_->resources.release(buffer);
        }

        diagnostics::write(impl_->destinations, diagnostics::log_level::warning, "ui.renderer",
                           "UI submission failed");
        throw;
    }
}

/// @brief Statistics follow the same coordinator/reentrancy rule as submission.
std::size_t paint_renderer::last_draws() const
{
    impl_->check();
    return impl_->draws;
}

/// @brief Report uploaded geometry independently of root constants.
std::size_t paint_renderer::last_vertex_bytes() const
{
    impl_->check();
    return impl_->vertex_bytes;
}
} // namespace atlas::ui_renderer
