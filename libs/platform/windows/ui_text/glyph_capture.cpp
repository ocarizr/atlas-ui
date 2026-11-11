/** @file @brief Capture positioned DirectWrite glyphs without exposing SDK objects. */
#include "glyph_capture.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace atlas::ui_text::detail
{
/// @brief Keep every captured object valid only during the synchronous layout call.
glyph_capture::glyph_capture(IDWriteFactory4& factory, float density, text_layout& result)
    : factory_{factory}, density_{density}, result_{result}
{
}

/// @brief Positions are aligned at raster publication, independently from native layout metrics.
HRESULT __stdcall glyph_capture::IsPixelSnappingDisabled(void*, BOOL* disabled) noexcept
{
    *disabled = TRUE;
    return S_OK;
}

/// @brief The label lives in untransformed logical coordinates.
HRESULT __stdcall glyph_capture::GetCurrentTransform(void*, DWRITE_MATRIX* transform) noexcept
{
    *transform = {1, 0, 0, 1, 0, 0};
    return S_OK;
}

/// @brief Keep DPI explicit even though layout origins are unsnapped.
HRESULT __stdcall glyph_capture::GetPixelsPerDip(void*, FLOAT* density) noexcept
{
    *density = density_;
    return S_OK;
}

/// @brief Convert a shaped run to positioned masks without unwinding through COM.
HRESULT __stdcall glyph_capture::DrawGlyphRun(void*, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE mode,
                                              const DWRITE_GLYPH_RUN* run,
                                              const DWRITE_GLYPH_RUN_DESCRIPTION*,
                                              IUnknown*) noexcept
{
    try
    {
        capture({x, y}, mode, *run);
        return S_OK;
    }
    catch (...)
    {
        failure_ = std::current_exception();
        return E_FAIL;
    }
}

/// @brief Text requests have no underline spans.
HRESULT __stdcall glyph_capture::DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*,
                                               IUnknown*) noexcept
{
    return E_NOTIMPL;
}

/// @brief Text requests have no strikeout spans.
HRESULT __stdcall glyph_capture::DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*,
                                                   IUnknown*) noexcept
{
    return E_NOTIMPL;
}

/// @brief No embedded native drawing objects are accepted in a text request.
HRESULT __stdcall glyph_capture::DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL,
                                                  BOOL, IUnknown*) noexcept
{
    return E_NOTIMPL;
}

/// @brief Restore original context for allocation and native coverage errors.
void glyph_capture::rethrow_failure() const
{
    if (failure_)
    {
        std::rethrow_exception(failure_);
    }
}

/// @brief Cache each glyph at the native face and scaled em size chosen by the shaper.
void glyph_capture::capture(D2D1_POINT_2F origin, DWRITE_MEASURING_MODE mode,
                            const DWRITE_GLYPH_RUN& run)
{
    constexpr std::size_t max_glyphs = 16384;
    if (run.glyphCount > max_glyphs - visited_ || run.isSideways)
    {
        throw std::overflow_error("Unsupported orientation or shaped UI glyph limit exceeded");
    }

    visited_ += run.glyphCount;
    if (run.glyphCount == 0)
    {
        return;
    }

    const auto found = std::ranges::find_if(faces_,
                                            [&](const auto& face)
                                            {
                                                return face.Get() == run.fontFace;
                                            });
    const auto face_index = static_cast<std::size_t>(found - faces_.begin());
    if (found == faces_.end())
    {
        faces_.emplace_back(run.fontFace);
        result_.faces = faces_.size();
    }

    std::vector<D2D1_POINT_2F> positions(run.glyphCount);
    check(factory_.ComputeGlyphOrigins(&run, mode, origin, nullptr, positions.data()),
          "resolve glyph origins");
    constexpr float zero = 0;
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        const auto id = run.glyphIndices[i];
        result_.missing_glyphs += static_cast<std::size_t>(id == 0);
        const auto [entry, inserted] = cache_.try_emplace({face_index, run.fontEmSize, id});
        if (inserted)
        {
            const DWRITE_GLYPH_RUN single{run.fontFace, run.fontEmSize, 1,     &id,
                                          &zero,        nullptr,        FALSE, 0};
            entry->second = rasterize(factory_, single, density_, result_.bitmaps, coverage_bytes_);
        }

        const auto& ink = entry->second.ink;
        if (ink.right > ink.left && ink.bottom > ink.top)
        {
            const float left = std::round(positions[i].x * density_) + static_cast<float>(ink.left);
            const float top = std::round(positions[i].y * density_) + static_cast<float>(ink.top);
            result_.glyphs.push_back({{left / density_, top / density_,
                                       static_cast<float>(ink.right - ink.left) / density_,
                                       static_cast<float>(ink.bottom - ink.top) / density_},
                                      entry->second.bitmap});
        }
    }
}
} // namespace atlas::ui_text::detail
