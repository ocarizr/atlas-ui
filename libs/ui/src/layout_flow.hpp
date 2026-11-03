/** @file @brief Stack-only flow descriptors leave canvas placement in its own numeric policy. */
#pragma once
#include <atlas/ui/panel_layout.hpp>

namespace atlas::ui::detail
{
struct flow_policy
{
    bool horizontal = false; ///< Sum child outer widths and advance horizontal slots.
    bool vertical = false;   ///< Sum child outer heights and advance vertical slots.
    stack_distribution distribution = stack_distribution::start; ///< Free-space packing.
    stack_fill fill = stack_fill::none;                          ///< Opt-in automatic growth.
    stack_overflow overflow = stack_overflow::allow;             ///< Opt-in automatic shrink.
    float spacing = 0; ///< Selected stack gap; other policies have no gap.
}; ///< Lightweight descriptor without virtual allocation or widget-specific switches.

/// @brief Only stack alternatives own axis and gap configuration.
inline flow_policy describe_flow(const panel_layout& panel)
{
    if (const auto* row = std::get_if<horizontal_layout>(&panel))
    {
        return {true, false, row->distribution, row->fill, row->overflow, row->spacing};
    }

    if (const auto* column = std::get_if<vertical_layout>(&panel))
    {
        return {false, true, column->distribution, column->fill, column->overflow, column->spacing};
    }

    return {};
}
} // namespace atlas::ui::detail
