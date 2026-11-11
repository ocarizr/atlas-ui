/** @file @brief Private DirectWrite text renderer that copies shaped glyph coverage. */
#pragma once
#include <atlas/ui_text/text_layout.hpp>
#include <dwrite_3.h>
#include <exception>
#include <map>
#include <tuple>
#include <wrl/implements.h>

namespace atlas::ui_text::detail
{
/// @brief Cached ink offset and optional mask index for one native glyph.
struct rasterized_glyph
{
    RECT ink{};             ///< Raster bounds relative to a pixel-aligned baseline.
    std::size_t bitmap = 0; ///< Mask index when the ink is nonempty.
};

/// @brief Coverage is rasterized once for each face, scaled size, and glyph index per layout.
rasterized_glyph rasterize(IDWriteFactory4& factory, const DWRITE_GLYPH_RUN& run, float density,
                           std::vector<glyph_bitmap>& bitmaps, std::size_t& coverage_bytes);

/// @brief Native layout callbacks run synchronously on the calling CPU thread.
class glyph_capture final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          Microsoft::WRL::ChainInterfaces<IDWriteTextRenderer, IDWritePixelSnapping>>
{
  public:
    /// @brief Borrow the layout output for the synchronous DirectWrite draw call.
    glyph_capture(IDWriteFactory4& factory, float density, text_layout& result);
    /// @brief Keep native metrics independent of monitor DPI.
    HRESULT __stdcall IsPixelSnappingDisabled(void*, BOOL* disabled) noexcept override;
    /// @brief Origins are in untransformed DIPs.
    HRESULT __stdcall GetCurrentTransform(void*, DWRITE_MATRIX* transform) noexcept override;
    /// @brief Return the rasterization density for this label.
    HRESULT __stdcall GetPixelsPerDip(void*, FLOAT* density) noexcept override;
    /// @brief Capture and rasterize each shaped run, forwarding native or allocation failures.
    HRESULT __stdcall DrawGlyphRun(void*, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE mode,
                                   const DWRITE_GLYPH_RUN* run, const DWRITE_GLYPH_RUN_DESCRIPTION*,
                                   IUnknown*) noexcept override;
    /// @brief The current request has no underline ranges.
    HRESULT __stdcall DrawUnderline(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*,
                                    IUnknown*) noexcept override;
    /// @brief The current request has no strikeout ranges.
    HRESULT __stdcall DrawStrikethrough(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*,
                                        IUnknown*) noexcept override;
    /// @brief Inline objects are outside the text label contract.
    HRESULT __stdcall DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL,
                                       IUnknown*) noexcept override;
    void rethrow_failure() const; ///< Restore an exception captured across the COM callback.

  private:
    /// @brief Copy ink quads from a run while the SDK data remains live.
    void capture(D2D1_POINT_2F origin, DWRITE_MEASURING_MODE mode, const DWRITE_GLYPH_RUN& run);
    IDWriteFactory4& factory_; ///< Retained by the platform entry point.
    float density_;            ///< Physical pixels per DIP.
    text_layout& result_;      ///< Caller-owned output.
    std::vector<Microsoft::WRL::ComPtr<IDWriteFontFace>> faces_; ///< Retain pointer identities.
    std::map<std::tuple<std::size_t, float, std::uint16_t>, rasterized_glyph>
        cache_;                      ///< Glyph masks.
    std::size_t coverage_bytes_ = 0; ///< Bound the aggregate raster allocation.
    std::size_t visited_ = 0;        ///< Count all glyphs, including whitespace.
    std::exception_ptr failure_;     ///< Never unwind a C++ exception through DirectWrite.
};

void check(HRESULT status, const char* action); ///< Convert native failures to contextual errors.
} // namespace atlas::ui_text::detail
