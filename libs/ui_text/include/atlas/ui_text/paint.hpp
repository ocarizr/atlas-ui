/**
 * @file
 * @brief Glyph geometry for the ordered UI paint pass and fence-retained atlas uploads.
 */
#pragma once
#include <atlas/ui_renderer/paint_renderer.hpp>
#include <atlas/ui_text/glyph_atlas.hpp>

namespace atlas::ui_text
{
/// @brief Placement, color and resident texture namespace for a shaped label.
struct paint_label
{
    std::size_t label = 0;                  ///< Index in the packed atlas.
    ui::layout_point origin;                ///< Logical label origin in the viewport.
    ui::layout_rect clip;                   ///< Ancestor/viewport clip.
    std::array<float, 4> color{1, 1, 1, 1}; ///< Linear straight-alpha tint.
    std::uint64_t first_texture_key = 1;    ///< First application-reserved atlas texture key.
};

/// @brief Emit ordered textured quads that can be interleaved with other UI paint.
[[nodiscard]] std::vector<ui::paint_quad> glyph_quads(const glyph_atlas& atlas,
                                                      const paint_label& label);

/// @brief GPU-coordinator owner of one immutable atlas version.
class uploaded_atlas
{
  public:
    /// @brief Admit page uploads without waiting; roll back handles if admission fails.
    uploaded_atlas(renderer::resource_store& resources, const glyph_atlas& atlas,
                   std::uint64_t first_texture_key, const diagnostics::services& sinks = {});
    ~uploaded_atlas(); ///< Release handles on the graphics owner; store retains in-flight uses.
    uploaded_atlas(const uploaded_atlas&) = delete; ///< Unique atlas handle ownership.
    uploaded_atlas&
    operator=(const uploaded_atlas&) = delete; ///< No shared release responsibility.
    [[nodiscard]] bool ready() const;          ///< Poll the atlas's initialization without waiting.
    [[nodiscard]] std::span<const ui_renderer::texture_binding> bindings() const; ///< Borrow SRVs.
  private:
    void release(); ///< Invalidate views and textures while retaining their protecting fences.
    renderer::resource_store& resources_;            ///< Graphics coordinator outlives this owner.
    std::vector<renderer::texture_handle> textures_; ///< One upload per atlas page.
    std::vector<ui_renderer::texture_binding> bindings_; ///< Keys and page views in packing order.
};
} // namespace atlas::ui_text
