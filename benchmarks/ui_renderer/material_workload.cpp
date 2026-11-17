/** @file @brief Aligned SDF cards with only material phase and stacking changed. */
#include "material_workload.hpp"

namespace atlas::ui_renderer::benchmarks
{
/// @brief The clock is explicit and any per-element offset is deterministic.
ui::material_style card_material(scenario shape, phase_sample sample)
{
    const float offset = shape.staggered && sample.card % 2 != 0 ? 0.125F : 0.0F;
    const auto sum = sample.time + offset;
    return {.kind = ui::material_kind::shape,
            .radius = {0.4F, 0.4F, 0.4F, 0.4F},
            .bevel = 0.25F,
            .scale = 0.8F,
            .morph = 0.3F,
            .outline = 0.15F,
            .glow = 1,
            .gradient = 0.2F,
            .phase = sum > 1.0F ? sum - 1.0F : sum};
}

/// @brief Assign each group of overlapping cards to a distinct grid cell.
ui::layout_rect card_bounds(scenario shape, std::size_t index)
{
    const auto cell = index / shape.layers;
    const auto column = cell % columns;
    const auto row = cell / columns;
    return {static_cast<float>(column * (card_width + 8)),
            static_cast<float>(row * (card_height + 8)), static_cast<float>(card_width),
            static_cast<float>(card_height)};
}

/// @brief Fix coordinates to integer physical pixels so clipped rectangle coverage is known.
std::vector<ui::element_handle> populate(ui::element_tree& tree, scenario shape,
                                         unsigned card_count)
{
    const auto root = tree.create();
    tree.set_panel(root, ui::canvas_layout{});
    std::vector<ui::element_handle> cards;
    cards.reserve(card_count);
    for (unsigned index = 0; index < card_count; ++index)
    {
        const auto node = tree.create(root);
        cards.push_back(node);
        const auto bounds = card_bounds(shape, index);
        ui::layout_style style;
        style.intrinsic = {bounds.width, bounds.height};
        tree.set_layout(node, style);
        tree.set_canvas_slot(
            node, {.horizontal = {.offset_min = bounds.x}, .vertical = {.offset_min = bounds.y}});
        ui::paint_style visual;
        visual.fill = true;
        visual.color = {0.3F, 0.7F, 0.9F, 0.5F};
        visual.material = card_material(shape, {.card = index, .time = 0});
        tree.set_paint(node, visual);
    }

    return cards;
}
} // namespace atlas::ui_renderer::benchmarks
