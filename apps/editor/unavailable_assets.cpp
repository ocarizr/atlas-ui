/** @file @brief Explicit asset-infrastructure availability in SDK-free editor builds. */
#include "asset_files.hpp"

namespace atlas::editor
{
/// @brief Native source and write infrastructure are selected only for supported platforms.
std::unique_ptr<asset_repository>
open_asset_repository(const std::filesystem::path&, jobs::job_system&, const diagnostics::services&)
{
    throw std::runtime_error("Editor asset storage is unavailable in this build");
}

/// @brief Native selection requires a platform integration.
std::optional<std::filesystem::path> choose_asset_file(const std::filesystem::path&,
                                                       assets::asset_type, bool)
{
    throw std::runtime_error("Editor asset selection is unavailable in this build");
}

/// @brief Preview acquisition uses the same compiled platform boundary.
std::shared_ptr<streaming::catalog_streamer> mount_editor_assets(const assets::catalog&,
                                                                 const std::filesystem::path&,
                                                                 jobs::job_system&,
                                                                 const diagnostics::services&)
{
    throw std::runtime_error("Editor asset mounts are unavailable in this build");
}
} // namespace atlas::editor
