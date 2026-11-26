/** @file @brief Host-only per-user theme persistence through existing editor storage services. */
#pragma once
#include "document_file.hpp"
#include "editor_theme.hpp"

namespace atlas::editor
{
void open_theme_preferences(editor_theme& theme, jobs::job_system& jobs,
                            const diagnostics::services& sinks); ///< Preserve defaults on error.
} // namespace atlas::editor
