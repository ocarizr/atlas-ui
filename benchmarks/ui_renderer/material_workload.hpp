/** @file @brief Fixed-geometry workloads for viewport material measurements. */
#pragma once
#include <atlas/ui/element_tree.hpp>

namespace atlas::ui_renderer::benchmarks
{
inline constexpr std::uint32_t card_width = 96;  ///< Submitted rectangle width in pixels.
inline constexpr std::uint32_t card_height = 64; ///< Submitted rectangle height in pixels.
inline constexpr std::uint32_t columns = 16;     ///< Cards fit in a 1920-pixel viewport.

/// @brief Change one variable at a time from the shared-phase, nonoverlapping baseline.
struct scenario
{
    std::string_view name; ///< Stable report identity.
    unsigned layers;       ///< Precisely aligned, translucent cards per cell.
    bool staggered;        ///< An offset alternating between zero and one eighth of a cycle.
};

/// @brief Author a canvas with equal-sized SDF cards and a reproducible painter order.
std::vector<ui::element_handle> populate(ui::element_tree& tree, scenario shape,
                                         unsigned card_count);

struct phase_sample
{
    std::size_t card; ///< Identifies the card's authored clock offset.
    float time;       ///< Current normalized host time.
}; ///< A sampled material clock input.

/// @brief Apply normalized host time to one material while keeping geometry unchanged.
ui::material_style card_material(scenario shape, phase_sample sample);
/// @brief Return the intended integer-pixel rectangle for one card.
ui::layout_rect card_bounds(scenario shape, std::size_t index);
} // namespace atlas::ui_renderer::benchmarks
