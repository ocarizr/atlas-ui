/** @file @brief Prepared v1 codecs for typed UI layout, appearance and bindings. */
#pragma once
#include "prepared_grid_fields.hpp"
#include "prepared_material_fields.hpp"
#include <atlas/mvvm_content/detail/prepared_definition.hpp>

namespace atlas::ui_content::detail
{
/// @brief Persist the v1 panel tag and its kind-specific values.
template <class Archive> void panel_fields(Archive& format, ui::panel_layout& panel, bool grids)
{
    auto tag = static_cast<std::uint8_t>(panel.index());
    format.field(tag);
    if constexpr (Archive::reading)
    {
        switch (tag)
        {
            case 0:
                panel = ui::overlay_layout{};
                break;
            case 1:
                panel = ui::leaf_layout{};
                break;
            case 2:
                panel = ui::size_box_layout{};
                break;
            case 3:
                panel = ui::horizontal_layout{};
                break;
            case 4:
                panel = ui::vertical_layout{};
                break;
            case 5:
                panel = ui::canvas_layout{};
                break;
            case 6:
                panel = ui::scroll_layout{};
                break;
            case 7:
                if (!grids)
                {
                    throw std::invalid_argument("Grid requires prepared UI version 7");
                }
                panel = ui::grid_layout{};
                break;
            default:
                throw std::invalid_argument("Unknown prepared UI panel");
        }
    }

    std::visit(
        [&](auto& value)
        {
            using panel_type = std::remove_cvref_t<decltype(value)>; ///< Selected panel record.
            if constexpr (std::is_same_v<panel_type, ui::horizontal_layout> ||
                          std::is_same_v<panel_type, ui::vertical_layout>)
            {
                format.field(value.spacing);
                format.choice(value.distribution, ui::stack_distribution::space_evenly);
                format.choice(value.fill, ui::stack_fill::weighted);
                format.choice(value.overflow, ui::stack_overflow::shrink);
            }
            else if constexpr (std::is_same_v<panel_type, ui::scroll_layout>)
            {
                format.fields(value.horizontal, value.vertical, value.offset.x, value.offset.y);
            }
            else if constexpr (std::is_same_v<panel_type, ui::canvas_layout>)
            {
                format.choice(value.measure, ui::canvas_measure::absolute_children);
            }
            else if constexpr (std::is_same_v<panel_type, ui::grid_layout>)
            {
                if (!grids)
                {
                    throw std::invalid_argument("Grid requires prepared UI version 7");
                }

                grid_fields(format, value);
            }
        },
        panel);
}

/// @brief Positive infinity is an explicit unbounded tag and never a generic float payload.
template <class Archive> void maximum(Archive& format, float& value)
{
    bool unbounded = value == std::numeric_limits<float>::infinity();
    format.field(unbounded);
    if (unbounded)
    {
        value = std::numeric_limits<float>::infinity();
    }
    else
    {
        format.field(value);
    }
}

/// @brief Persist authored geometry with separate margin and padding.
template <class Archive> void layout_fields(Archive& format, ui::layout_style& layout)
{
    auto& pad = layout.padding;
    auto& margin = layout.margin;
    auto& limits = layout.constraints;
    format.fields(layout.intrinsic.width, layout.intrinsic.height, pad.left, pad.top, pad.right,
                  pad.bottom, margin.left, margin.top, margin.right, margin.bottom,
                  limits.minimum.width, limits.minimum.height);
    maximum(format, limits.maximum.width);
    maximum(format, limits.maximum.height);
    format.choice(layout.horizontal, ui::layout_alignment::end);
    format.choice(layout.vertical, ui::layout_alignment::end);
}

/// @brief Persist visual data independently of layout, keeping the texture key transient.
template <class Archive> void appearance_fields(Archive& format, element& node)
{
    auto& paint = node.paint;
    auto& uv = paint.uv;
    format.fields(paint.fill, paint.clip, paint.layer);
    for (auto& channel : paint.color)
    {
        format.field(channel);
    }

    format.fields(uv.x, uv.y, uv.width, uv.height, paint.translation.x, paint.translation.y,
                  paint.scale.x, paint.scale.y, paint.rotation, node.visual.origin.x,
                  node.visual.origin.y);
    format.optional(paint.material, material_style_fields);
    format.optional(node.texture, reference);
    format.optional(node.material, reference);
}

/// @brief Encode MVVM binding and event declarations and optional Lua attachment.
template <class Archive> void binding_fields(Archive& format, element& node)
{
    format.sequence(node.properties, 4,
                    [](auto& archive, property_binding& value)
                    {
                        archive.fields(value.model, value.binding.source);
                        archive.choice(value.binding.target, ui::property::visible);
                        archive.choice(value.binding.mode, ui_mvvm::binding_mode::two_way);
                    });
    format.sequence(node.actions, 64,
                    [](auto& archive, action_binding& value)
                    {
                        auto& action = value.binding;
                        archive.fields(value.model, action.action, action.input_action);
                        archive.choice(action.event, ui::event_kind::drag_cancelled);
                        archive.choice(action.route, ui::route_phase::bubble);
                        archive.choice(action.input_phase, input::action_phase::cancelled);
                        archive.sequence(action.arguments, 32,
                                         mvvm_content::detail::prepared_scalar);
                    });
    format.optional(node.script,
                    [](auto& archive, behavior& value)
                    {
                        reference(archive, value.source);
                        archive.field(value.model);
                    });
}

/// @brief Include optional parent-owned relationships and input flags.
template <class Archive> void relationship_fields(Archive& format, element& node)
{
    format.optional(node.canvas,
                    [](auto& archive, ui::canvas_slot& value)
                    {
                        for (auto* axis : {&value.horizontal, &value.vertical})
                        {
                            archive.fields(axis->anchor_min, axis->anchor_max, axis->offset_min,
                                           axis->offset_max, axis->pivot);
                            archive.choice(axis->sizing, ui::canvas_sizing::stretch);
                        }
                    });
    format.optional(node.stack,
                    [](auto& archive, ui::stack_slot& value)
                    {
                        archive.fields(value.order, value.grow, value.shrink);
                    });
    const auto boundary = [](auto& archive, sibling_boundary& value)
    {
        archive.fields(value.source, value.gap);
        archive.choice(value.side, ui::boundary_side::before);
        archive.choice(value.sizing, ui::boundary_sizing::remaining);
        archive.choice(value.overflow, ui::boundary_overflow::reject);
    };
    format.optional(node.horizontal, boundary);
    format.optional(node.vertical, boundary);
    format.optional(node.interaction,
                    [](auto& archive, ui::interaction_options& value)
                    {
                        archive.fields(value.enabled, value.hit_test, value.focusable);
                    });
}

/// @brief Preserve parent-before-child order without constructing live registry handles.
template <class Archive> void element_fields(Archive& format, element& node, bool grids)
{
    format.fields(node.id, node.parent, node.caption);
    auto& size = node.requested;
    format.fields(size.width, size.height, size.opacity, size.visible, size.width_fixed,
                  size.height_fixed);
    panel_fields(format, node.panel, grids);
    layout_fields(format, node.layout);
    appearance_fields(format, node);
    relationship_fields(format, node);
    binding_fields(format, node);
    if (grids)
    {
        format.optional(node.grid, grid_slot_fields<Archive>);
    }
}

/// @brief Preserve the exact legacy element field order when no grid feature is authored.
inline constexpr auto prepared_element_fields = [](auto& format, element& node)
{
    element_fields(format, node, false);
};
/// @brief Version seven appends grid-owned relationships after the established element fields.
inline constexpr auto prepared_grid_element_fields = [](auto& format, element& node)
{
    element_fields(format, node, true);
};
} // namespace atlas::ui_content::detail
