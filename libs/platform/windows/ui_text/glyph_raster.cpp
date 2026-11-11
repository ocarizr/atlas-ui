/** @file @brief DirectWrite grayscale coverage of one pixel-aligned glyph. */
#include "glyph_capture.hpp"
#include <stdexcept>

namespace atlas::ui_text::detail
{
/// @brief Create an SDK grayscale mask with finite physical dimensions and a bounded byte budget.
rasterized_glyph rasterize(IDWriteFactory4& factory, const DWRITE_GLYPH_RUN& run, float density,
                           std::vector<glyph_bitmap>& bitmaps, std::size_t& coverage_bytes)
{
    const DWRITE_MATRIX scale{density, 0, 0, density, 0, 0};
    Microsoft::WRL::ComPtr<IDWriteGlyphRunAnalysis> analysis;
    check(factory.CreateGlyphRunAnalysis(
              &run, &scale, DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC, DWRITE_MEASURING_MODE_NATURAL,
              DWRITE_GRID_FIT_MODE_DISABLED, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, 0, 0,
              analysis.GetAddressOf()),
          "analyze glyph");
    constexpr auto format = DWRITE_TEXTURE_ALIASED_1x1;
    rasterized_glyph glyph;
    check(analysis->GetAlphaTextureBounds(format, &glyph.ink), "measure glyph mask");
    const auto width = glyph.ink.right - glyph.ink.left;
    const auto height = glyph.ink.bottom - glyph.ink.top;
    if (width <= 0 || height <= 0)
    {
        return glyph;
    }

    if (width > 2046 || height > 2046)
    {
        throw std::overflow_error("UI glyph exceeds the maximum atlas mask extent");
    }

    const auto bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    constexpr std::size_t max_bytes = std::size_t{16} * 1024 * 1024;
    if (bytes > max_bytes - coverage_bytes)
    {
        throw std::overflow_error("UI glyph coverage budget exhausted");
    }

    glyph_bitmap bitmap{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                        std::vector<std::uint8_t>(bytes)};
    check(analysis->CreateAlphaTexture(format, &glyph.ink, bitmap.coverage.data(),
                                       static_cast<UINT32>(bytes)),
          "rasterize glyph mask");
    coverage_bytes += bytes;
    glyph.bitmap = bitmaps.size();
    bitmaps.push_back(std::move(bitmap));
    return glyph;
}
} // namespace atlas::ui_text::detail
