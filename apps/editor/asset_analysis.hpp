/** @file @brief Buffer-only typed reference extraction for editor asset preparation. */
#pragma once
#include <atlas/ui_content/document.hpp>

namespace atlas::editor
{
/// @brief Extract typed references from an already validated UI document.
[[nodiscard]] std::vector<assets::asset_ref> asset_references(const ui_content::document& source);
/// @brief Validate authored UI/material/flow sources and extract their catalog dependencies.
[[nodiscard]] std::vector<assets::asset_ref> inspect_asset(assets::asset_type type,
                                                           std::span<const std::uint8_t> bytes);
} // namespace atlas::editor
