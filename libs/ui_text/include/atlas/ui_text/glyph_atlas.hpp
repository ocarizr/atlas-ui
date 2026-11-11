/**
 * @file
 * @brief Immutable, bounded packing of shaped glyphs into straight-alpha texture pages.
 */
#pragma once
#include <atlas/ui_text/text_layout.hpp>
#include <span>

namespace atlas::ui_text
{
/// @brief Atlas pages have a fixed square extent with one transparent pixel between masks.
struct atlas_limits
{
    std::uint32_t side = 512;  ///< Physical page dimension in pixels, from 4 to 2048.
    std::size_t max_pages = 8; ///< Maximum number of simultaneously packed pages.
};

/// @brief Shaped geometry mapped to one immutable page.
struct atlas_glyph
{
    ui::layout_rect bounds; ///< Label-relative DIPs.
    ui::layout_rect uv;     ///< Normalized source region excluding its transparent border.
    std::size_t page = 0;   ///< Atlas page index.
};

/// @brief One tight RGBA8 page, white RGB and grayscale coverage in alpha.
struct atlas_page
{
    std::vector<std::byte> rgba; ///< Exact square texture upload payload.
};

/// @brief A successful atlas owns every input label's glyph mapping and page pixels.
class glyph_atlas
{
  public:
    /// @brief Pack in input order, deduplicate masks, and fail atomically at the page budget.
    [[nodiscard]] static glyph_atlas pack(std::span<const text_layout> labels,
                                          atlas_limits limits = {},
                                          const diagnostics::services& sinks = {});
    [[nodiscard]] std::uint32_t side() const noexcept;                ///< Shared square page size.
    [[nodiscard]] std::span<const atlas_page> pages() const noexcept; ///< Borrow immutable pixels.
    [[nodiscard]] std::span<const atlas_glyph>
    label(std::size_t index) const; ///< Borrow one label.
  private:
    std::uint32_t side_ = 0;                       ///< Square extent of every page.
    std::vector<atlas_page> pages_;                ///< Owning transparent RGBA pages.
    std::vector<std::vector<atlas_glyph>> labels_; ///< Label order matches the source span.
};
} // namespace atlas::ui_text
