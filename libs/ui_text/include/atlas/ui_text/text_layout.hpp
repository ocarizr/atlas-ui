/**
 * @file
 * @brief SDK-free shaping, logical measurement and monochrome glyph coverage.
 */
#pragma once
#include <atlas/ui/layout_types.hpp>
#include <string_view>
#include <vector>

namespace atlas::ui_text
{
/// @brief Layout dimensions are logical DIPs; DPI affects coverage density.
struct text_request
{
    std::string_view text;                ///< UTF-8 including line breaks.
    std::string_view family = "Segoe UI"; ///< Installed font family, with system fallback.
    std::string_view locale = "en-US";    ///< DirectWrite locale name.
    float size = 16;                      ///< Font size in DIPs.
    float width = 4096;                   ///< Layout width in DIPs.
    float dpi = 96;                       ///< Physical pixels per logical 96 DIPs.
    bool wrap = true;                     ///< Wrap using native line break opportunities.
    bool right_to_left = false; ///< Base paragraph direction; mixed runs are shaped natively.
};

/// @brief One reusable grayscale glyph mask.
struct glyph_bitmap
{
    std::uint32_t width = 0;            ///< Physical pixel width.
    std::uint32_t height = 0;           ///< Physical pixel height.
    std::vector<std::uint8_t> coverage; ///< Tight rows, 0 transparent to 255 opaque.
};

/// @brief Position of a rasterized glyph after native layout and shaping.
struct glyph_placement
{
    ui::layout_rect bounds; ///< Ink bounds relative to the label origin in DIPs.
    std::size_t bitmap = 0; ///< Index into the layout's reusable masks.
};

/// @brief Owned value-only measurement and coverage, safe to pass between owner phases.
struct text_layout
{
    ui::layout_extent size;  ///< Logical content size including trailing whitespace.
    std::uint32_t lines = 0; ///< Native line count.
    std::size_t faces = 0;   ///< Distinct native faces used for visible and invisible glyphs.
    std::size_t missing_glyphs = 0;    ///< Unmapped glyphs emitted by native fallback.
    bool family_substituted = false;   ///< Requested family was absent from the system collection.
    std::vector<glyph_bitmap> bitmaps; ///< Rasterized masks reused within this layout.
    std::vector<glyph_placement> glyphs; ///< Native run order, with empty masks omitted.
};

/// @brief Report whether this build includes the native shaping backend.
[[nodiscard]] bool available() noexcept;
/// @brief Shape and rasterize one bounded label synchronously using the installed font collection.
[[nodiscard]] text_layout shape(const text_request& request,
                                const diagnostics::services& sinks = {});
} // namespace atlas::ui_text
