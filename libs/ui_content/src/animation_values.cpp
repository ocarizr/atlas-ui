/** @file @brief Read animation bases without exposing mutable registry components. */
#include "animation_values.hpp"
#include <algorithm>
#include <cmath>

namespace atlas::ui_content::detail
{
/// @brief Only cubic controls are consumed by the easing kernel.
bool valid_curve(animation_curve curve, std::array<float, 2> controls)
{
    return curve <= animation_curve::spring &&
           (curve != animation_curve::cubic || std::ranges::all_of(controls,
                                                                   [](float value)
                                                                   {
                                                                       return value >= 0 &&
                                                                              value <= 1;
                                                                   }));
}

/// @brief Use one bounded easing implementation for authored clips and transitions.
double ease(animation_curve curve, std::array<float, 2> controls, double fraction)
{
    switch (curve)
    {
        case animation_curve::step:
            return fraction < 1 ? 0 : 1;
        case animation_curve::linear:
            return fraction;
        case animation_curve::smoothstep:
            return fraction * fraction * (3 - 2 * fraction);
        case animation_curve::cubic:
        {
            const auto inverse = 1 - fraction;
            return std::clamp(3 * inverse * inverse * fraction * controls[0] +
                                  3 * inverse * fraction * fraction * controls[1] +
                                  fraction * fraction * fraction,
                              0., 1.);
        }
        case animation_curve::spring:
        {
            const auto response = [](double time)
            {
                constexpr double damping = 8;
                return 1 - (1 + damping * time) * std::exp(-damping * time);
            };
            return std::clamp(response(fraction) / response(1), 0., 1.);
        }
    }

    throw std::invalid_argument("Unknown animation curve");
}

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
