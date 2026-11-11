/** @file @brief Deterministic shelf packing and mask deduplication across UI labels. */
#include <algorithm>
#include <atlas/ui_text/glyph_atlas.hpp>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <tuple>

namespace atlas::ui_text
{
namespace
{
/// @brief Atlas texture coverage budget, separate from GPU staging and native allocations.
constexpr std::size_t max_atlas_bytes = std::size_t{64} * 1024 * 1024;

/// @brief Store the region with its original mask for exact hash-collision checks.
struct packed_mask
{
    const glyph_bitmap* bitmap; ///< Valid during synchronous packing.
    ui::layout_rect uv;         ///< Normalized page coordinates.
    std::size_t page;           ///< Immutable page index.
};

/// @brief Active row in a page of square, tightly packed RGBA pixels.
struct shelf
{
    std::uint32_t x = 0;      ///< Next horizontal pixel, including gutter.
    std::uint32_t y = 0;      ///< Top of the active row.
    std::uint32_t height = 0; ///< Tallest allocation in the row.
};

/// @brief Validate tightly packed rows and construct a deterministic lookup key.
auto mask_key(const glyph_bitmap& mask, std::uint32_t side)
{
    if (!mask.width || !mask.height || mask.width > side - 2 || mask.height > side - 2 ||
        mask.coverage.size() != static_cast<std::size_t>(mask.width) * mask.height)
    {
        throw std::invalid_argument("Invalid UI glyph mask or mask does not fit the atlas");
    }

    const auto bytes =
        std::string_view{reinterpret_cast<const char*>(mask.coverage.data()), mask.coverage.size()};
    return std::tuple{mask.width, mask.height, std::hash<std::string_view>{}(bytes)};
}

/// @brief Append a new page only if both count and total pixel payload budgets allow it.
void add_page(std::vector<atlas_page>& pages, atlas_limits limits)
{
    const auto bytes = static_cast<std::size_t>(limits.side) * limits.side * 4;
    if (pages.size() == limits.max_pages || pages.size() == max_atlas_bytes / bytes)
    {
        throw std::overflow_error("UI glyph atlas page budget exhausted");
    }

    auto& rgba = pages.emplace_back().rgba;
    rgba.resize(bytes, std::byte{255});
    for (std::size_t i = 3; i < rgba.size(); i += 4)
    {
        rgba[i] = std::byte{0};
    }
}

/// @brief Place a unique mask with a transparent white gutter on all sides.
packed_mask place(const glyph_bitmap& mask, atlas_limits limits, shelf& row,
                  std::vector<atlas_page>& pages)
{
    const auto width = mask.width + 2;
    const auto height = mask.height + 2;
    if (row.x + width > limits.side)
    {
        row = {0, row.y + row.height, 0};
    }

    if (pages.empty() || row.y + height > limits.side)
    {
        add_page(pages, limits);
        row = {};
    }

    const auto x = row.x + 1;
    const auto y = row.y + 1;
    auto& rgba = pages.back().rgba;
    for (std::size_t line = 0; line < mask.height; ++line)
    {
        const auto pixel = ((y + line) * limits.side + x) * 4 + 3;
        for (std::size_t column = 0; column < mask.width; ++column)
        {
            rgba[pixel + column * 4] = std::byte{mask.coverage[line * mask.width + column]};
        }
    }

    row.x += width;
    row.height = std::max(row.height, height);
    const auto side = static_cast<float>(limits.side);
    return {&mask,
            {static_cast<float>(x) / side, static_cast<float>(y) / side,
             static_cast<float>(mask.width) / side, static_cast<float>(mask.height) / side},
            pages.size() - 1};
}

using key_type = std::tuple<std::uint32_t, std::uint32_t, std::size_t>; ///< Mask shape and hash.
using mask_index = std::multimap<key_type, packed_mask>; ///< One entry per unique pixel payload.

/// @brief Reuse matching masks even when layouts were shaped independently.
packed_mask reuse_or_place(const glyph_bitmap& mask, atlas_limits limits, shelf& row,
                           std::vector<atlas_page>& pages, mask_index& indexed)
{
    const auto key = mask_key(mask, limits.side);
    const auto [first, last] = indexed.equal_range(key);
    for (auto it = first; it != last; ++it)
    {
        if (it->second.bitmap->coverage == mask.coverage)
        {
            return it->second;
        }
    }

    const auto placed = place(mask, limits, row, pages);
    indexed.emplace(key, placed);
    return placed;
}

/// @brief An invalid placement never becomes a published atlas draw.
void validate_placement(const glyph_placement& glyph, std::size_t bitmaps)
{
    const auto& bounds = glyph.bounds;
    if (glyph.bitmap >= bitmaps || !std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
        !std::isfinite(bounds.width) || !std::isfinite(bounds.height) || bounds.width <= 0 ||
        bounds.height <= 0)
    {
        throw std::invalid_argument("Invalid shaped UI glyph placement");
    }
}
} // namespace

/// @brief Publish only a complete, bounded, immutable pack.
glyph_atlas glyph_atlas::pack(std::span<const text_layout> labels, atlas_limits limits,
                              const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"ui.text.pack", "ui.text.cpu"}};
    constexpr std::size_t max_glyphs = 65536;
    if (limits.side < 4 || limits.side > 2048 || !limits.max_pages || limits.max_pages > 64 ||
        labels.size() > 4096)
    {
        throw std::invalid_argument("Invalid UI glyph atlas limits");
    }

    glyph_atlas atlas;
    atlas.side_ = limits.side;
    atlas.labels_.reserve(labels.size());
    mask_index indexed;
    shelf row;
    std::size_t total = 0;
    for (const auto& label : labels)
    {
        if (label.glyphs.size() > max_glyphs - total || label.bitmaps.size() > max_glyphs)
        {
            throw std::overflow_error("UI glyph atlas placement limit exceeded");
        }

        total += label.glyphs.size();
        std::vector<std::optional<packed_mask>> locations(label.bitmaps.size());
        auto& output = atlas.labels_.emplace_back();
        output.reserve(label.glyphs.size());
        for (const auto& glyph : label.glyphs)
        {
            validate_placement(glyph, label.bitmaps.size());
            auto& location = locations[glyph.bitmap];
            if (!location)
            {
                location =
                    reuse_or_place(label.bitmaps[glyph.bitmap], limits, row, atlas.pages_, indexed);
            }

            output.push_back({glyph.bounds, location->uv, location->page});
        }
    }

    return atlas;
}

/// @brief All pages share one square extent.
std::uint32_t glyph_atlas::side() const noexcept
{
    return side_;
}

/// @brief Borrow immutable page uploads.
std::span<const atlas_page> glyph_atlas::pages() const noexcept
{
    return pages_;
}

/// @brief Return the glyph sequence for one label in the original input order.
std::span<const atlas_glyph> glyph_atlas::label(std::size_t index) const
{
    return labels_.at(index);
}
} // namespace atlas::ui_text
