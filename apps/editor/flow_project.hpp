/** @file @brief Compose graph persistence and optional catalog-backed Lua registry discovery. */
#pragma once
#include "editor_view.hpp"

namespace atlas::editor
{
/// @brief Open an independent XML draft or create a saveable empty viewport graph.
[[nodiscard]] std::unique_ptr<editor_flow::persistence>
open_flow_document(const std::filesystem::path& path, jobs::job_system& jobs,
                   const diagnostics::services& sinks, bool recover = false);
/// @brief Acquire only a callable entrypoint's script dependency closure via the shared streamer.
void load_flow_module(editor_view& view, assets::asset_ref entry, const assets::catalog& catalog,
                      streaming::catalog_streamer& storage);
} // namespace atlas::editor
