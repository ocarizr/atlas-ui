/** @file @brief Read animation bases without exposing mutable registry components. */
#include "animation_values.hpp"

namespace atlas::ui_content::detail
{
namespace
{
/// @brief Read an inset in the same order as the command vocabulary.
float edge(ui::layout_insets insets, ui::property field, ui::property first)
{
    const std::array values{insets.left, insets.top, insets.right, insets.bottom};
    return values[static_cast<unsigned>(field) - static_cast<unsigned>(first)];
}

/// @brief Canvas topology and spacing targets were checked when the clip resolved.
float layout_value(const ui::element_tree& tree, ui::element_handle target, ui::property field)
{
    using enum ui::property;
    if (field >= margin_left && field <= margin_bottom)
    {
        return edge(tree.layout(target).margin, field, margin_left);
    }

    if (field >= padding_left && field <= padding_bottom)
    {
        return edge(tree.layout(target).padding, field, padding_left);
    }

    if (field == spacing)
    {
        const auto panel = tree.panel(target);
        if (const auto* row = std::get_if<ui::horizontal_layout>(&panel))
        {
            return row->spacing;
        }

        return std::get<ui::vertical_layout>(panel).spacing;
    }

    const auto slot = tree.canvas_slot_for(target).value_or(ui::canvas_slot{});
    const std::array values{slot.horizontal.offset_min, slot.horizontal.offset_max,
                            slot.vertical.offset_min,   slot.vertical.offset_max,
                            slot.horizontal.anchor_min, slot.horizontal.anchor_max,
                            slot.vertical.anchor_min,   slot.vertical.anchor_max};
    return values.at(static_cast<unsigned>(field) - static_cast<unsigned>(canvas_x_offset_min));
}
} // namespace

/// @brief Preserve automatic dimensions as the original monostate reset command.
ui::property_value read_animation_value(const ui::element_tree& tree, ui::element_handle target,
                                        ui::property field)
{
    using enum ui::property;
    if (field >= margin_left)
    {
        return layout_value(tree, target, field);
    }

    if (field >= translate_x)
    {
        const auto style = tree.paint(target);
        const std::array values{style.translation.x, style.translation.y, style.scale.x,
                                style.scale.y, style.rotation};
        return values.at(static_cast<unsigned>(field) - static_cast<unsigned>(translate_x));
    }

    const auto base = tree.requested(target);
    switch (field)
    {
        case width:
            return base.width_fixed ? ui::property_value{base.width} : std::monostate{};
        case height:
            return base.height_fixed ? ui::property_value{base.height} : std::monostate{};
        case opacity:
            return base.opacity;
        default:
            throw std::invalid_argument("Unsupported animation base property");
    }
}
} // namespace atlas::ui_content::detail
