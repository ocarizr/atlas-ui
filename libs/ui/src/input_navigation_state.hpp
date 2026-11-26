/** @file @brief Sparse navigation metadata and shared action indexing without per-frame jobs. */
#pragma once
#include <atlas/ui/focus_navigation.hpp>

namespace atlas::ui::detail
{
/// @brief Preserve full identities so recycled nodes cannot inherit explicit connections.
struct focus_node
{
    element_handle target;            ///< Configured live input binding.
    focus_navigation_options options; ///< Optional sparse author overrides.
};

/// @brief One independent group remembers its last eligible focus without owning widgets.
struct focus_scope
{
    element_handle target;          ///< Captured hierarchy root.
    focus_navigation_scope options; ///< Wrap policy and optional group action routes.
    element_handle remembered{};    ///< Last focused member; stale values are revalidated on use.
};

/// @brief Return direction-indexed IDs using a fixed stack value.
inline std::array<input::action_id, 6> focus_actions(focus_navigation_actions actions)
{
    return {actions.left, actions.right, actions.up, actions.down, actions.next, actions.previous};
}
} // namespace atlas::ui::detail
