/** @file @brief Shared immutable ancestry and local-coordinate helpers for focus anchors. */
#pragma once
#include <algorithm>
#include <atlas/ui/element_snapshot.hpp>

namespace atlas::ui::detail
{
/// @brief Binary lookup respects full session/generation identity.
inline const element_record* find_element(const element_snapshot& source, element_handle target)
{
    const auto nodes = source.elements();
    const auto found = std::lower_bound(nodes.begin(), nodes.end(), target.index,
                                        [](const element_record& node, std::uint32_t index)
                                        {
                                            return node.target.index < index;
                                        });
    return found != nodes.end() && found->target == target ? &*found : nullptr;
}

/// @brief Bounded ancestry avoids recursion and stale parent traversal.
inline bool is_descendant(const element_snapshot& source, element_handle target,
                          element_handle owner, std::size_t depth)
{
    for (std::size_t step = 0; target.generation != 0 && step < depth; ++step)
    {
        if (target == owner)
        {
            return true;
        }

        const auto* node = find_element(source, target);
        if (!node)
        {
            return false;
        }

        target = node->parent;
    }

    return false;
}
} // namespace atlas::ui::detail
