/** @file @brief CPU-only adapter scratch prepared before graphics mutation. */
#pragma once
#include <atlas/ui_renderer/paint_renderer.hpp>
#include <vector>

namespace atlas::ui_renderer::detail
{
struct draw_preparation
{
    explicit draw_preparation(std::pmr::memory_resource* memory) : vertices(0, memory)
    {
    } ///< Construct scratch in place; debug PMR proxy allocation may throw.

    std::pmr::vector<renderer::vertex>
        vertices;                    ///< Contiguous upload payload, not retained on CPU.
    renderer::pass_description pass; ///< Ordered compatible adjacent batches.
}; ///< Local resources are released after submit or validation failure.

/// @brief Pipeline identities used during ordered draw preparation.
struct paint_pipelines
{
    renderer::pipeline_handle solid;            ///< Ordinary filled geometry.
    renderer::pipeline_handle textured;         ///< Ordinary sampled geometry.
    renderer::pipeline_handle texture_material; ///< Material-controlled texture sampling.
    renderer::pipeline_handle shape_material;   ///< Analytic SDF coverage.
};

void prepare_draws(paint_list list, renderer::texture_handle target,
                   renderer::surface_extent extent, std::span<const texture_binding> textures,
                   const paint_pipelines& pipelines, std::pmr::memory_resource* memory,
                   draw_preparation& output); ///< No GPU or file access.
} // namespace atlas::ui_renderer::detail
