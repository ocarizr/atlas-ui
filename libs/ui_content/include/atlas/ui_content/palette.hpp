/**
 * @file
 * @brief Strict palette XML, lossless prepared colors and revision-checked storage publication.
 */
#pragma once
#include <atlas/ui/palette.hpp>
#include <atlas/ui_content/instance.hpp>

namespace atlas::ui_content
{
inline constexpr std::size_t max_palette_bytes = 65536; ///< Independent source/output budget.
[[nodiscard]] std::shared_ptr<const ui::palette>
parse_palette_xml(xml_source source, const diagnostics::services& sinks = {}); ///< sRGB hex input.
[[nodiscard]] std::string
write_palette_xml(const ui::palette& source); ///< Canonical sRGB8 authoring; rounds native colors.
[[nodiscard]] ui::rgba parse_color(std::string_view value); ///< Hex sRGB8 to linear RGBA.
[[nodiscard]] std::string format_color(ui::rgba value);     ///< Linear RGBA to nearest sRGB8 hex.
[[nodiscard]] std::vector<std::byte>
encode_palette(const ui::palette& source); ///< Lossless validated native/prepared definition.
[[nodiscard]] std::shared_ptr<const ui::palette>
decode_palette(std::span<const std::byte> bytes); ///< Version, extent and checksum validation.

/// @brief Retain the exact catalog generation until the owner commits the loaded palette.
struct palette_asset
{
    streaming::read_stamp stamp;              ///< Source identity, revision and mount generation.
    std::shared_ptr<const ui::palette> value; ///< Fully validated, independent immutable candidate.
};

[[nodiscard]] palette_asset load_palette(assets::asset_ref source,
                                         const construction& options); ///< Streamer-only read.
void publish_palette(ui::palette_context& context, const palette_asset& candidate,
                     const streaming::catalog_streamer& storage); ///< Reject stale publication.
} // namespace atlas::ui_content
