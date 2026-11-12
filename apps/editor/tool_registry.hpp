/** @file @brief Discover isolated editor-level and project-level authored tools. */
#pragma once
#include "document_file.hpp"

namespace atlas::editor
{
/// @brief The executable and project choose their own authorized content roots.
struct tool_roots
{
    std::filesystem::path editor;  ///< Installed shared tool catalog directory.
    std::filesystem::path project; ///< Optional project-local tool catalog directory.
};

/// @brief A registered tool resolves through one retained origin-specific streamer.
struct tool_entry
{
    std::string title;                    ///< Display name including the origin.
    assets::asset_ref reference;          ///< Persistent UI identity.
    streaming::catalog_streamer* storage; ///< Borrowed from the registry.
};

/// @brief Catalogs are mounted independently, so project tools cannot replace editor tools.
class tool_registry
{
  public:
    /// @brief Load trusted and project tool catalogs with independent failure handling.
    tool_registry(const tool_roots& roots, jobs::job_system& jobs,
                  const diagnostics::services& sinks);
    [[nodiscard]] const std::optional<tool_entry>& shell() const noexcept; ///< Shared chrome.
    [[nodiscard]] std::span<const tool_entry>
    tools() const noexcept; ///< Editor then project order.

  private:
    std::vector<std::unique_ptr<streaming::catalog_streamer>> mounts_; ///< Registry-owned sources.
    std::optional<tool_entry> shell_; ///< Reserved editor-level shell only.
    std::vector<tool_entry> tools_;   ///< Selectable authored contributions.
};
} // namespace atlas::editor
