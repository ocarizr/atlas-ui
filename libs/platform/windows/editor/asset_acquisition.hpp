/** @file @brief Streamer-delivered CPU preparation with independently retained callback state. */
#pragma once
#include "asset_repository.hpp"

namespace atlas::editor
{
/// @brief Hash acquired bytes using the system SHA-256 provider.
std::string asset_source_hash(std::span<const std::uint8_t> bytes);
/// @brief Return a cancelable acquisition whose callback captures no editor object.
std::unique_ptr<asset_read> acquire_editor_asset(const std::filesystem::path& path,
                                                 assets::asset_type type, jobs::job_system& jobs,
                                                 const diagnostics::services& sinks);
} // namespace atlas::editor
