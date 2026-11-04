/** @file @brief Compact bounded frontier descriptors borrow one immutable preorder source. */
#pragma once
#include "layout_state.hpp"

namespace atlas::ui::detail
{
struct measure_interval
{
    std::uint32_t first;   ///< Inclusive raw-preorder start.
    std::uint32_t last;    ///< Exclusive end; contains only complete sibling subtrees.
    bool terminal = false; ///< No further legal grain-preserving panel split was found.
}; ///< No subtree copies or per-node callables.

/// @brief Temporary metadata is released after joined measurement, not retained in the root cache.
struct measure_partition
{
    explicit measure_partition(std::pmr::memory_resource* memory)
        : ends(0, memory), ranges(0, memory)
    {
    } ///< Counted construction propagates debug-iterator allocation failures.

    std::pmr::vector<std::uint32_t> ends; ///< One subtree endpoint per node, four requested bytes.
    std::pmr::vector<measure_interval> ranges; ///< At most max_chunks lightweight descriptors.
};

measure_partition partition_measure(const layout_source& source, const layout_limits& limits,
                                    const std::stop_token& stop); ///< Iterative panel refinement.
} // namespace atlas::ui::detail
