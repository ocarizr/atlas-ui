/** @file @brief Bounded normalized slots and typed panel policy validation. */
#include "canvas_validation.hpp"
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace atlas::ui::detail
{
namespace
{
/// @brief Anchors and pivots deliberately disallow extrapolation beyond the normalized domain.
bool normalized(float value)
{
    return std::isfinite(value) && value >= 0 && value <= 1;
}

/// @brief Absolute-child measurement accepts no parent-dependent axis equation.
void validate_axis(canvas_axis value, canvas_measure measure)
{
    if (!normalized(value.anchor_min) || !normalized(value.anchor_max) ||
        value.anchor_max < value.anchor_min || !normalized(value.pivot) ||
        !std::isfinite(value.offset_min) || !std::isfinite(value.offset_max) ||
        (value.sizing != canvas_sizing::desired && value.sizing != canvas_sizing::stretch))
    {
        throw std::invalid_argument("invalid canvas anchors, offsets, pivot or sizing");
    }

    if (measure == canvas_measure::absolute_children &&
        (value.anchor_min != 0 || value.anchor_max != 0 || value.sizing != canvas_sizing::desired))
    {
        throw std::invalid_argument("content-sized canvas requires absolute desired-size slots");
    }
}
} // namespace

/// @brief Only the selected typed policy owns its configuration.
void validate_panel(const panel_layout& panel)
{
    std::visit(
        [](const auto& value)
        {
            using policy =
                std::decay_t<decltype(value)>; ///< Selected typed container configuration.
            if constexpr (std::is_same_v<policy, horizontal_layout> ||
                          std::is_same_v<policy, vertical_layout>)
            {
                if (!std::isfinite(value.spacing) || value.spacing < 0)
                {
                    throw std::invalid_argument("invalid panel spacing");
                }
            }
            else if constexpr (std::is_same_v<policy, canvas_layout>)
            {
                if (value.measure != canvas_measure::intrinsic &&
                    value.measure != canvas_measure::absolute_children)
                {
                    throw std::invalid_argument("invalid canvas measurement policy");
                }
            }
        },
        panel);
}

/// @brief Both axes validate before any sparse component can be changed.
void validate_slot(const canvas_slot& slot, canvas_layout panel)
{
    validate_axis(slot.horizontal, panel.measure);
    validate_axis(slot.vertical, panel.measure);
}

/// @brief A visual origin is independent from layout placement pivots.
void validate_visual(visual_style visual)
{
    if (!normalized(visual.origin.x) || !normalized(visual.origin.y))
    {
        throw std::invalid_argument("invalid normalized UI transform origin");
    }
}
} // namespace atlas::ui::detail
