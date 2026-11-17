/** @file @brief Reproducible authored viewport workloads for the readiness probe. */
#pragma once
#include <atlas/ui_content/instance.hpp>

namespace atlas::ui_content::readiness
{
/// @brief Changes applied once per frame to the same authored layout.
enum class update
{
    idle,     ///< Unchanged viewport and authored values.
    resize,   ///< Alternate nearby viewport extents.
    material, ///< Animate the shape-local glow.
    drawer,   ///< Translate the clipped drawer visually.
    reflow    ///< Animate requested width and recalculate layout.
};

struct scenario
{
    std::string_view name; ///< Stable report identity.
    std::size_t panels;    ///< Independent sibling panels under the canvas.
    std::size_t rows;      ///< Painted children per panel.
    update change;         ///< Frame mutation shared by both policies.
}; ///< Bounded synthetic viewport without product-specific content.

struct scenario_assets
{
    assets::asset_ref material; ///< Rounded shape.
    assets::asset_ref model;    ///< Frame state.
    assets::asset_ref script;   ///< Owner-thread frame handler.
}; ///< Typed authored dependencies shared by both policies.

/// @brief Build canvas, clipped stacks and shape material instances through authoring data.
document make_document(scenario shape, const scenario_assets& dependencies);
/// @brief Begin the matching animation, if any, after composition.
void start(instance& view, update change);
/// @brief Compose a complete frame and publish the ordered owner-thread writes.
void dispatch(instance& view, const ui::layout_snapshot& input_geometry, std::uint64_t frame);
} // namespace atlas::ui_content::readiness
