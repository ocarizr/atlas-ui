/** @file @brief Native editor asset origins behind portable storage and acquisition contracts. */
#pragma once
#include "asset_repository.hpp"

namespace atlas::editor
{
/// @brief Open or initialize a writable editor-owned typed content origin.
[[nodiscard]] std::unique_ptr<asset_repository>
open_asset_repository(const std::filesystem::path& root, jobs::job_system& jobs,
                      const diagnostics::services& sinks);
/// @brief Ask the native host for an import/relink source or an authorized move destination.
[[nodiscard]] std::optional<std::filesystem::path>
choose_asset_file(const std::filesystem::path& root, assets::asset_type type, bool destination);
/// @brief Snapshot a complete catalog into an immutable preview mount.
[[nodiscard]] std::shared_ptr<streaming::catalog_streamer>
mount_editor_assets(const assets::catalog& catalog, const std::filesystem::path& root,
                    jobs::job_system& jobs, const diagnostics::services& sinks);
} // namespace atlas::editor
