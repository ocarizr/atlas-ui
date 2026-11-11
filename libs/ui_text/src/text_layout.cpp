/** @file @brief Bounded public entry point for text layout and grayscale coverage. */
#include "layout_backend.hpp"
#include <cmath>
#include <stdexcept>

namespace atlas::ui_text
{
/// @brief Query whether the build contains a supported backend.
bool available() noexcept
{
    return detail::backend_available();
}

/// @brief Validate work limits before entering the native font service.
text_layout shape(const text_request& request, const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"ui.text.shape", "ui.text.cpu"}};
    const auto bounded = [](float value, float low, float high)
    {
        return std::isfinite(value) && value >= low && value <= high;
    };
    if (request.text.size() > 16384 || request.family.empty() || request.family.size() > 256 ||
        request.locale.empty() || request.locale.size() > 85 ||
        request.text.find('\0') != request.text.npos ||
        request.family.find('\0') != request.family.npos ||
        request.locale.find('\0') != request.locale.npos || !bounded(request.size, 1, 256) ||
        !bounded(request.width, 1, 32768) || !bounded(request.dpi, 48, 768))
    {
        throw std::invalid_argument("UI text request exceeds its layout or source limits");
    }

    if (request.text.empty())
    {
        return {};
    }

    return detail::shape_native(request);
}
} // namespace atlas::ui_text
