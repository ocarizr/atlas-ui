/** @file @brief Shared policy validation for live mutation and pure geometry helpers. */
#pragma once
#include <atlas/ui/layout_slot.hpp>
#include <atlas/ui/panel_layout.hpp>
#include <atlas/ui/visual_style.hpp>

namespace atlas::ui::detail
{
void validate_panel(const panel_layout& panel); ///< Reject invalid gaps and measurement modes.
void validate_slot(const canvas_slot& slot,
                   canvas_layout panel);   ///< Enforce normalized, noncircular placement.
void validate_visual(visual_style visual); ///< Reject invalid normalized transform origins.
} // namespace atlas::ui::detail
