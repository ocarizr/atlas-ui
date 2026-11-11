/** @file @brief Map layout and paint authoring to typed viewport UI data. */
#include "xml_layout.hpp"

namespace atlas::ui_content::detail
{
namespace
{
/// @brief Stack-only fields share the same vocabulary for either orientation.
template <class Stack> void stack_panel(fields& xml, Stack& data)
{
    xml.value("spacing", data.spacing);
    xml.choice(
        "distribution", data.distribution,
        std::array{"start", "center", "end", "space-between", "space-around", "space-evenly"});
    xml.choice("fill", data.fill, std::array{"none", "weighted"});
    xml.choice("overflow", data.overflow, std::array{"allow", "shrink"});
}

/// @brief Resolve panel kind without persisting a variant index.
void panel(fields& xml, ui::panel_layout& data)
{
    /// @brief V1 panel names corresponding to the closed native panel vocabulary.
    enum class kind : std::uint8_t
    {
        overlay,    ///< Shared content rectangle.
        leaf,       ///< Intrinsic content without children.
        size_box,   ///< Single content slot.
        horizontal, ///< Horizontal stack.
        vertical,   ///< Vertical stack.
        canvas      ///< Anchored placement.
    };
    auto selected = static_cast<kind>(data.index());
    xml.choice("panel", selected,
               std::array{"overlay", "leaf", "size-box", "horizontal", "vertical", "canvas"});
    if (xml.reading())
    {
        switch (selected)
        {
            case kind::overlay:
                data = ui::overlay_layout{};
                break;
            case kind::leaf:
                data = ui::leaf_layout{};
                break;
            case kind::size_box:
                data = ui::size_box_layout{};
                break;
            case kind::horizontal:
                data = ui::horizontal_layout{};
                break;
            case kind::vertical:
                data = ui::vertical_layout{};
                break;
            case kind::canvas:
                data = ui::canvas_layout{};
                break;
        }
    }

    std::visit(
        [&](auto& current)
        {
            using type = std::remove_cvref_t<decltype(current)>; ///< Selected typed panel.
            if constexpr (std::is_same_v<type, ui::horizontal_layout> ||
                          std::is_same_v<type, ui::vertical_layout>)
            {
                stack_panel(xml, current);
            }
            else if constexpr (std::is_same_v<type, ui::canvas_layout>)
            {
                xml.choice("measure", current.measure,
                           std::array{"intrinsic", "absolute-children"});
            }
        },
        data);
}
} // namespace

/// @brief Omission of width/height represents automatic sizing, including after a round trip.
void element_fields(fields& xml, element& data)
{
    xml.value("id", data.id, true);
    xml.optional("parent", data.parent, !data.parent.empty());
    panel(xml, data.panel);
    auto& requested = data.requested;
    if (xml.reading())
    {
        requested.width_fixed = static_cast<bool>(xml.node().attribute("width"));
        requested.height_fixed = static_cast<bool>(xml.node().attribute("height"));
    }

    xml.optional("width", requested.width, requested.width_fixed);
    xml.optional("height", requested.height, requested.height_fixed);
    xml.value("opacity", requested.opacity);
    xml.value("visible", requested.visible);
}

/// @brief Omitted maxima are unbounded; serialized numbers are always finite.
void layout_fields(fields& xml, ui::layout_style& data)
{
    xml.value("intrinsic-width", data.intrinsic.width);
    xml.value("intrinsic-height", data.intrinsic.height);
    xml.value("padding-left", data.padding.left);
    xml.value("padding-top", data.padding.top);
    xml.value("padding-right", data.padding.right);
    xml.value("padding-bottom", data.padding.bottom);
    xml.value("margin-left", data.margin.left);
    xml.value("margin-top", data.margin.top);
    xml.value("margin-right", data.margin.right);
    xml.value("margin-bottom", data.margin.bottom);
    xml.value("min-width", data.constraints.minimum.width);
    xml.value("min-height", data.constraints.minimum.height);
    const auto unbounded = std::numeric_limits<float>::infinity();
    xml.optional("max-width", data.constraints.maximum.width,
                 data.constraints.maximum.width != unbounded);
    xml.optional("max-height", data.constraints.maximum.height,
                 data.constraints.maximum.height != unbounded);
    const std::array alignments{"stretch", "start", "center", "end"};
    xml.choice("horizontal", data.horizontal, alignments);
    xml.choice("vertical", data.vertical, alignments);
}

/// @brief Persist authored color, placement and texture identity without residency keys.
void paint_fields(fields& xml, element& data)
{
    auto& paint = data.paint;
    xml.value("fill", paint.fill);
    xml.value("clip", paint.clip);
    xml.value("layer", paint.layer);
    xml.value("r", paint.color[0]);
    xml.value("g", paint.color[1]);
    xml.value("b", paint.color[2]);
    xml.value("a", paint.color[3]);
    xml.value("u", paint.uv.x);
    xml.value("v", paint.uv.y);
    xml.value("uv-width", paint.uv.width);
    xml.value("uv-height", paint.uv.height);
    xml.value("translate-x", paint.translation.x);
    xml.value("translate-y", paint.translation.y);
    xml.value("origin-x", data.visual.origin.x);
    xml.value("origin-y", data.visual.origin.y);
    xml.reference("texture", data.texture, assets::asset_type::texture);
}

/// @brief Canvas slots preserve distinct per-axis anchors, offsets, pivots and allocation.
void canvas_fields(fields& xml, ui::canvas_slot& data)
{
    const std::array sizing{"desired", "stretch"};
    xml.value("x-anchor-min", data.horizontal.anchor_min);
    xml.value("x-anchor-max", data.horizontal.anchor_max);
    xml.value("x-offset-min", data.horizontal.offset_min);
    xml.value("x-offset-max", data.horizontal.offset_max);
    xml.value("x-pivot", data.horizontal.pivot);
    xml.choice("x-sizing", data.horizontal.sizing, sizing);
    xml.value("y-anchor-min", data.vertical.anchor_min);
    xml.value("y-anchor-max", data.vertical.anchor_max);
    xml.value("y-offset-min", data.vertical.offset_min);
    xml.value("y-offset-max", data.vertical.offset_max);
    xml.value("y-pivot", data.vertical.pivot);
    xml.choice("y-sizing", data.vertical.sizing, sizing);
}

/// @brief Stack slots carry ordered growth and shrink weights.
void stack_fields(fields& xml, ui::stack_slot& data)
{
    xml.value("order", data.order);
    xml.value("grow", data.grow);
    xml.value("shrink", data.shrink);
}

/// @brief A sibling constraint encodes its stable source name and edge policy.
void boundary_fields(fields& xml, sibling_boundary& data)
{
    xml.value("source", data.source, true);
    xml.choice("side", data.side, std::array{"after", "before"});
    xml.value("gap", data.gap);
    xml.choice("sizing", data.sizing, std::array{"desired", "remaining"});
    xml.choice("overflow", data.overflow, std::array{"allow", "reject"});
}

/// @brief Explicit interaction participates in input even without actions or a script.
void interaction_fields(fields& xml, ui::interaction_options& data)
{
    xml.value("enabled", data.enabled);
    xml.value("hit-test", data.hit_test);
    xml.value("focusable", data.focusable);
}
} // namespace atlas::ui_content::detail
