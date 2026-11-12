/** @file @brief Editor persistence boundary with exact streamer reads and atomic replacement. */
#pragma once
#include <atlas/ui_content/instance.hpp>

namespace atlas::editor
{
/// @brief Test whether the selected file exists, surfacing errors other than absence.
[[nodiscard]] bool document_exists(const std::filesystem::path& path);
/// @brief Read authored XML through a temporary typed mount for the selected file.
[[nodiscard]] ui_content::document open_document(const std::filesystem::path& path,
                                                 jobs::job_system& jobs,
                                                 const diagnostics::services& sinks);
/// @brief Serialize and replace the destination only after all bytes have been written.
void save_document(const std::filesystem::path& path, const ui_content::document& source,
                   const diagnostics::services& sinks);
} // namespace atlas::editor
