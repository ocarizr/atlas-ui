/** @file @brief Editor persistence boundary with exact streamer reads and atomic replacement. */
#pragma once
#include "document_workspace.hpp"
#include <atlas/ui_content/instance.hpp>

namespace atlas::editor
{
/// @brief Compose native synchronous persistence with streamer-backed reads.
[[nodiscard]] document_storage make_document_storage(jobs::job_system& jobs,
                                                     diagnostics::services sinks);
/// @brief Remove a workspace-owned recovery file, surfacing failures except absence.
void remove_document(const std::filesystem::path& path);
/// @brief Locate installed editor tools alongside the executable.
[[nodiscard]] std::filesystem::path installed_tools_root();
/// @brief Test whether the selected file exists, surfacing errors other than absence.
[[nodiscard]] bool document_exists(const std::filesystem::path& path);
/// @brief Acquire a bounded text source via the streamer from a host-selected file.
[[nodiscard]] std::string read_editor_source(const std::filesystem::path& path,
                                             jobs::job_system& jobs,
                                             const diagnostics::services& sinks);
/// @brief Read authored XML through a temporary typed mount for the selected file.
[[nodiscard]] ui_content::document open_document(const std::filesystem::path& path,
                                                 jobs::job_system& jobs,
                                                 const diagnostics::services& sinks);
/// @brief Serialize and replace the destination only after all bytes have been written.
void save_document(const std::filesystem::path& path, const ui_content::document& source,
                   const diagnostics::services& sinks);
} // namespace atlas::editor
