/** @file @brief Inspector selection of literal or preview-contract semantic tint sources. */
#pragma once
#include "panels.hpp"

namespace atlas::editor
{
void append_color_controls(panel_plan& plan,
                           const document_session& session);       ///< Show current source.
void cycle_color_source(document_session& session, bool material); ///< Literal plus known keys.
} // namespace atlas::editor
