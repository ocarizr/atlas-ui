/** @file @brief Convert packed atlas placements into logical ordered paint quads. */
#include <algorithm>
#include <atlas/ui_text/paint.hpp>
#include <cmath>
#include <limits>

namespace atlas::ui_text
{
namespace
{
/// @brief Add rectangle edges without overflowing the finite renderer input contract.
std::array<ui::paint_vertex, 6> vertices(ui::layout_rect bounds, ui::layout_rect uv,
                                         const std::array<float, 4>& color)
{
    const ui::paint_vertex a{{bounds.x, bounds.y}, {uv.x, uv.y}, color};
    const ui::paint_vertex b{{bounds.x + bounds.width, bounds.y}, {uv.x + uv.width, uv.y}, color};
    const ui::paint_vertex c{{bounds.x + bounds.width, bounds.y + bounds.height},
                             {uv.x + uv.width, uv.y + uv.height},
                             color};
    const ui::paint_vertex d{{bounds.x, bounds.y + bounds.height}, {uv.x, uv.y + uv.height}, color};
    return {a, b, c, a, c, d};
}
} // namespace

/// @brief The caller controls painter order and the reserved key range for this atlas.
std::vector<ui::paint_quad> glyph_quads(const glyph_atlas& atlas, const paint_label& label)
{
    if (!label.first_texture_key ||
        (!atlas.pages().empty() &&
         atlas.pages().size() - 1 >
             std::numeric_limits<std::uint64_t>::max() - label.first_texture_key) ||
        !std::isfinite(label.origin.x) || !std::isfinite(label.origin.y) ||
        !std::ranges::all_of(label.color,
                             [](float value)
                             {
                                 return value >= 0 && value <= 1;
                             }))
    {
        throw std::invalid_argument("Invalid UI text paint placement or texture key range");
    }

    const auto glyphs = atlas.label(label.label);
    std::vector<ui::paint_quad> output;
    output.reserve(glyphs.size());
    for (const auto& glyph : glyphs)
    {
        auto bounds = glyph.bounds;
        bounds.x += label.origin.x;
        bounds.y += label.origin.y;
        const auto clip = ui::intersect_clip(bounds, label.clip);
        if (clip.width > 0 && clip.height > 0)
        {
            output.push_back({{},
                              bounds,
                              label.clip,
                              label.first_texture_key + glyph.page,
                              vertices(bounds, glyph.uv, label.color)});
        }
    }

    return output;
}
} // namespace atlas::ui_text
