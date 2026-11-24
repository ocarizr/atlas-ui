/** @file @brief Stable explicit and automatic grid occupancy with bounded scratch storage. */
#include "grid_validation.hpp"
#include "layout_state.hpp"
#include <algorithm>

namespace atlas::ui::detail
{
namespace
{
/// @brief Sparse authored lookup keeps default children free of live registry components.
grid_slot authored_slot(const element_snapshot& snapshot, element_handle target)
{
    const auto entries = snapshot.grid_slots();
    const auto found = std::lower_bound(entries.begin(), entries.end(), target.index,
                                        [](const auto& value, auto index)
                                        {
                                            return value.target.index < index;
                                        });
    return found != entries.end() && found->target == target ? found->slot : grid_slot{};
}
} // namespace

/// @brief Reserve explicit cells first; auto placement never steals a later declared explicit cell.
void resolve_grid_children(layout_source& source, const element_snapshot& snapshot)
{
    for (auto& parent : source.nodes)
    {
        const auto* grid = std::get_if<grid_layout>(&parent.panel);
        if (!grid)
        {
            continue;
        }

        const auto first = source.grid_slots.size();
        for (auto child = parent.first; child != no_layout_node; child = source.nodes[child].next)
        {
            auto& node = source.nodes[child];
            node.slot = static_cast<std::uint32_t>(source.grid_slots.size());
            source.grid_slots.push_back(authored_slot(snapshot, node.target));
        }

        auto slots = std::span{source.grid_slots}.subspan(first);
        resolve_grid_slots(*grid, slots, slots, source.memory.get());
    }
}
} // namespace atlas::ui::detail
