/** @file @brief DirectWrite shaping and logical layout over installed system fonts. */
#include "../../../ui_text/src/layout_backend.hpp"
#include "glyph_capture.hpp"
#include <stdexcept>
#include <string>

namespace atlas::ui_text::detail
{
namespace
{
using Microsoft::WRL::ComPtr;

/// @brief Convert exact UTF-8, rejecting invalid input before passing it to the layout engine.
std::wstring utf16(std::string_view input)
{
    const auto needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                            static_cast<int>(input.size()), nullptr, 0);
    if (needed <= 0)
    {
        throw std::invalid_argument("Invalid UTF-8 in UI text or font options");
    }

    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                            static_cast<int>(input.size()), result.data(), needed) != needed)
    {
        throw std::invalid_argument("Cannot convert UI text to UTF-16");
    }

    return result;
}

/// @brief Use the shared OS font cache without retaining application-specific resources.
const ComPtr<IDWriteFactory4>& factory()
{
    static const auto value = []
    {
        ComPtr<IDWriteFactory4> result;
        check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory4),
                                  reinterpret_cast<IUnknown**>(result.GetAddressOf())),
              "create DirectWrite factory");
        return result;
    }();
    return value;
}

/// @brief Replace an unavailable requested family with the tool UI's default family.
std::wstring font_family(IDWriteFactory4& source, std::wstring name, text_layout& result)
{
    ComPtr<IDWriteFontCollection> fonts;
    check(source.GetSystemFontCollection(fonts.GetAddressOf()), "load system fonts");
    UINT32 index = 0;
    BOOL exists = FALSE;
    check(fonts->FindFamilyName(name.c_str(), &index, &exists), "find requested family");
    result.family_substituted = exists == FALSE;
    return exists ? std::move(name) : std::wstring{L"Segoe UI"};
}
} // namespace

/// @brief Preserve the failing DirectWrite operation and HRESULT in diagnostics.
void check(HRESULT status, const char* action)
{
    if (FAILED(status))
    {
        throw std::runtime_error(std::string{action} + " failed (" + std::to_string(status) + ")");
    }
}

/// @brief The selected Windows build supports native shaping.
bool backend_available() noexcept
{
    return true;
}

/// @brief DirectWrite chooses runs, fallback, wrapping and base-direction positioning.
text_layout shape_native(const text_request& request)
{
    const auto text = utf16(request.text);
    const auto locale = utf16(request.locale);
    auto& source = *factory().Get();
    text_layout result;
    const auto family = font_family(source, utf16(request.family), result);

    ComPtr<IDWriteTextFormat> format;
    check(source.CreateTextFormat(family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                  DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                  request.size, locale.c_str(), format.GetAddressOf()),
          "create text format");
    check(format->SetWordWrapping(request.wrap ? DWRITE_WORD_WRAPPING_WRAP
                                               : DWRITE_WORD_WRAPPING_NO_WRAP),
          "set text wrapping");
    if (request.right_to_left)
    {
        check(format->SetReadingDirection(DWRITE_READING_DIRECTION_RIGHT_TO_LEFT),
              "set paragraph direction");
    }

    ComPtr<IDWriteTextLayout> layout;
    check(source.CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format.Get(),
                                  request.width, 32768.f, layout.GetAddressOf()),
          "shape UI text");
    DWRITE_TEXT_METRICS metrics{};
    check(layout->GetMetrics(&metrics), "measure UI text");
    result.size = {metrics.widthIncludingTrailingWhitespace, metrics.height};
    result.lines = metrics.lineCount;
    const auto capture = Microsoft::WRL::Make<glyph_capture>(source, request.dpi / 96.f, result);
    if (!capture)
    {
        throw std::bad_alloc{};
    }

    const auto drawn = layout->Draw(nullptr, capture.Get(), 0, 0);
    capture->rethrow_failure();
    check(drawn, "capture shaped glyphs");
    return result;
}
} // namespace atlas::ui_text::detail
