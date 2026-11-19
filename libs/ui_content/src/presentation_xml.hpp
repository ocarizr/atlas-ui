/** @file @brief Versioned presentation metadata shared by UI XML reader and writer. */
#pragma once
#include "xml_fields.hpp"

namespace atlas::ui_content::detail
{
/// @brief Retain v1 as legacy UI and require an explicit kind in v2.
presentation_options read_presentation(pugi::xml_node root);
/// @brief Emit v1 for unchanged legacy documents and v2 for explicit kinds.
void write_presentation(pugi::xml_node root, presentation_options source);
} // namespace atlas::ui_content::detail
