/** @file @brief Finite checked contain/cover math shared by serial and joined paint preparation. */
#include <algorithm>
#include <atlas/ui/image_fit.hpp>
#include <cmath>

namespace atlas::ui
{
/// @brief Never infer source dimensions by accessing graphics resources or storage.
image_placement fit_image(layout_rect bounds, image_sampling sampling, layout_rect uv)
{
    const auto finite = [](float v)
    {
        return std::isfinite(v);
    };
    if (!finite(bounds.x) || !finite(bounds.y) || !finite(bounds.width) || !finite(bounds.height) ||
        bounds.width < 0 || bounds.height < 0 || !finite(sampling.source.width) ||
        !finite(sampling.source.height) || sampling.source.width <= 0 ||
        sampling.source.height <= 0 || sampling.fit > image_fit::cover || !finite(uv.x) ||
        !finite(uv.y) || !finite(uv.width) || !finite(uv.height) || uv.x < 0 || uv.y < 0 ||
        uv.width < 0 || uv.height < 0 || static_cast<double>(uv.x) + uv.width > 1 ||
        static_cast<double>(uv.y) + uv.height > 1)
    {
        throw std::invalid_argument("invalid UI image fitting inputs");
    }

    if (std::abs(static_cast<double>(bounds.x) + bounds.width) >
            std::numeric_limits<float>::max() ||
        std::abs(static_cast<double>(bounds.y) + bounds.height) > std::numeric_limits<float>::max())
    {
        throw std::overflow_error("UI image border overflow");
    }

    image_placement result{bounds, uv};
    if (bounds.width == 0 || bounds.height == 0 || sampling.fit == image_fit::stretch)
    {
        return result;
    }

    if (uv.width == 0 || uv.height == 0)
    {
        result.bounds.width = result.bounds.height = 0;
        return result;
    }

    const double source_width = static_cast<double>(sampling.source.width) * uv.width;
    const double source_height = static_cast<double>(sampling.source.height) * uv.height;
    const double x = bounds.width / source_width;
    const double y = bounds.height / source_height;
    if (sampling.fit == image_fit::contain)
    {
        const auto scale = std::min(x, y);
        result.bounds.width = static_cast<float>(source_width * scale);
        result.bounds.height = static_cast<float>(source_height * scale);
        result.bounds.x += (bounds.width - result.bounds.width) / 2;
        result.bounds.y += (bounds.height - result.bounds.height) / 2;
    }
    else
    {
        const auto scale = std::max(x, y);
        const auto width = static_cast<float>(bounds.width / (source_width * scale));
        const auto height = static_cast<float>(bounds.height / (source_height * scale));
        result.uv.x += uv.width * (1 - width) / 2;
        result.uv.y += uv.height * (1 - height) / 2;
        result.uv.width *= width;
        result.uv.height *= height;
    }

    return result;
}
} // namespace atlas::ui
