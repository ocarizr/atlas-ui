/** @file @brief Map portable keyboard and semantic group requests after consumable user routing. */
#include "input_state.hpp"
#include <atlas/input/key.hpp>

namespace atlas::ui
{
/// @brief Preview recognition is side-effect free so legacy scroll policies can yield selectively.
bool input_session::impl::focus_navigation_packet(input_event raw, bool execute)
{
    std::optional<focus_direction> direction;
    element_handle scope{};
    if (raw.kind == input_kind::action && raw.action &&
        raw.action->phase == input::action_phase::performed)
    {
        const auto action = raw.action->action;
        const auto match = [&](focus_navigation_actions actions)
        {
            const auto ids = detail::focus_actions(actions);
            for (std::size_t i = 0; i < ids.size(); ++i)
            {
                if (action && ids[i] == action)
                {
                    direction = static_cast<focus_direction>(i);
                    return true;
                }
            }

            return false;
        };
        if (!match(focus_actions))
        {
            for (const auto& group : focus_scopes)
            {
                if (eligible(group.target) && match(group.options.actions))
                {
                    scope = group.target;
                    break;
                }
            }
        }
    }
    else if (focus_keyboard && raw.kind == input_kind::key_down && raw.key <= 65535)
    {
        switch (static_cast<input::key>(raw.key))
        {
            case input::key::left:
                direction = focus_direction::left;
                break;
            case input::key::right:
                direction = focus_direction::right;
                break;
            case input::key::up:
                direction = focus_direction::up;
                break;
            case input::key::down:
                direction = focus_direction::down;
                break;
            case input::key::tab:
                direction = focus_shift ? focus_direction::previous : focus_direction::next;
                break;
            default:
                break;
        }
    }

    if (!direction)
    {
        return false;
    }

    if (execute)
    {
        (void)move_focus(*direction, scope);
    }

    return true;
}
} // namespace atlas::ui
