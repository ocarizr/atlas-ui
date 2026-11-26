/**
 * @file
 * @brief Device-independent spatial/ordered focus navigation and sparse author overrides.
 */
#pragma once
#include <atlas/input/input_system.hpp>
#include <atlas/ui/commands.hpp>

namespace atlas::ui
{
/// @brief Four spatial directions plus logical traversal, independent of physical controls.
enum class focus_direction : std::uint8_t
{
    left,    ///< Decreasing viewport X.
    right,   ///< Increasing viewport X.
    up,      ///< Decreasing viewport Y.
    down,    ///< Increasing viewport Y.
    next,    ///< Following eligible item in explicit/captured order.
    previous ///< Preceding eligible item.
};

/// @brief Nonzero semantic IDs are unique across navigation mappings in one input session.
struct focus_navigation_actions
{
    input::action_id left = 0;     ///< Optional semantic left request.
    input::action_id right = 0;    ///< Optional semantic right request.
    input::action_id up = 0;       ///< Optional semantic up request.
    input::action_id down = 0;     ///< Optional semantic down request.
    input::action_id next = 0;     ///< Optional ordered/tab-next request.
    input::action_id previous = 0; ///< Optional ordered/tab-previous request.
    bool operator==(const focus_navigation_actions&) const = default; ///< Exact configuration.
};

/// @brief Optional per-element overrides; completely default configuration needs no sparse record.
struct focus_navigation_options
{
    std::array<element_handle, 6> destinations{}; ///< Direction-indexed; empty means automatic.
    std::array<bool, 6> blocked{}; ///< Stop that direction; cannot also supply a destination.
    std::int32_t tab_order = 0;    ///< Ascending ordered traversal; layout order breaks ties.
    bool tab_stop =
        true; ///< False excludes only ordered traversal, not directional/programmatic focus.
    bool operator==(const focus_navigation_options&) const = default; ///< Exact sparse defaults.
};

/// @brief Automatic traversal stays inside its nearest configured scope or independent root.
struct focus_navigation_scope
{
    focus_navigation_actions actions{}; ///< Can target this group from elsewhere in the view.
    bool wrap_horizontal =
        false; ///< Search the opposite horizontal edge when no forward item exists.
    bool wrap_vertical = false; ///< Search the opposite vertical edge.
    bool wrap_tab = false;      ///< Cycle ordered traversal at an endpoint.
};
} // namespace atlas::ui
