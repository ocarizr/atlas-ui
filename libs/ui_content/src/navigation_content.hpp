/** @file @brief Authored focus codecs, reference validation and runtime wiring. */
#pragma once
#include "tree_build.hpp"
#include "xml_fields.hpp"

namespace atlas::ui_content::detail
{
void focus_fields(fields& xml, focus_navigation& data); ///< Stable IDs and blocked edges.
void focus_scope_fields(fields& xml, ui::focus_navigation_scope& data); ///< Group wrap/actions.
void navigation_input_fields(fields& xml, navigation_input& data); ///< Explicit global mappings.
void validate_navigation(const document& source); ///< Reject unresolved IDs and ambiguous actions.
/// @brief Wire validated descriptors after all runtime handlers have been created.
void configure_navigation(ui::input_session& input, const elements& ids, const document& source);
///< Resolve all authored handles after binding construction succeeds.
} // namespace atlas::ui_content::detail
