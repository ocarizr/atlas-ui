/** @file @brief Release stale drag policy without retaining removed widgets' payloads. */
#pragma once
#include <algorithm>
#include <vector>

namespace atlas::ui::detail
{
/// @brief Handler retirement is complete before sparse payload metadata is pruned.
template <class State> void prune_drag_metadata(State& state)
{
    const auto gone = [&](const auto& item)
    {
        const auto* binding = state.find(item.first);
        return !state.tree.contains(item.first) || !binding || binding->ended || binding->retiring;
    };
    std::erase_if(state.drag_sources, gone);
    std::erase_if(state.drop_targets, gone);
}
} // namespace atlas::ui::detail
