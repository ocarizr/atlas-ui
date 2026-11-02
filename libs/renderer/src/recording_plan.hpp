/** @file @brief Stable partitions of complete passes or contiguous draw sections. */
#pragma once
#include <atlas/renderer/recording.hpp>
#include <vector>

namespace atlas::renderer::detail
{
/// @brief Contiguous portion of a pass; only endpoints perform load/store work.
struct recording_section
{
    std::size_t pass = 0;  ///< Immutable pass index.
    std::size_t begin = 0; ///< First draw in this section.
    std::size_t count = 0; ///< Zero for a clear-only pass.
    bool first = false;    ///< Perform original transitions and loads.
    bool last = false;     ///< Perform original stores.
};

/// @brief One command list owns an ordered sequence of sections.
using recording_group = std::vector<recording_section>;
std::vector<recording_group>
plan_recording(std::span<const std::size_t> draw_counts,
               recording_options options); ///< Balance coarse ranges without changing pass order.
} // namespace atlas::renderer::detail
