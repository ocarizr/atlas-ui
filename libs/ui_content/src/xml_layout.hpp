/** @file @brief Shared XML codecs for typed element layout and paint sections. */
#pragma once
#include "xml_fields.hpp"

namespace atlas::ui_content::detail
{
void element_fields(fields& xml, element& data); ///< Core identity and requested properties.
void layout_fields(fields& xml, ui::layout_style& data); ///< Intrinsic sizing and alignment.
void paint_fields(fields& xml, element& data);           ///< Visual appearance and durable texture.
void canvas_fields(fields& xml, ui::canvas_slot& data);  ///< Independent anchor axes.
void stack_fields(fields& xml, ui::stack_slot& data);    ///< Sibling ordering and weights.
void boundary_fields(fields& xml, sibling_boundary& data);           ///< Authored edge constraint.
void interaction_fields(fields& xml, ui::interaction_options& data); ///< Input policy.
} // namespace atlas::ui_content::detail
