/** @file @brief Device-independent ordered list focus movement with automatic scroll reveal. */
#include "input_state.hpp"
#include "snapshot_lookup.hpp"
#include <atlas/input/key.hpp>
#include <tuple>

namespace atlas::ui
{
namespace
{
/// @brief The same operations are addressed by keys or semantic gamepad actions.
enum class movement : std::uint8_t
{
    none,     ///< Leave focus unchanged.
    previous, ///< Previous eligible layout item.
    next,     ///< Next eligible layout item.
    first,    ///< Leading eligible item.
    last      ///< Trailing eligible item.
};

/// @brief Performed semantic actions and portable navigation keys never expose native key codes.
movement direction(input_event raw, const scroll_navigation_actions& actions,
                   const scroll_layout& panel)
{
    if (raw.kind == input_kind::action && raw.action &&
        raw.action->phase == input::action_phase::performed)
    {
        const auto id = raw.action->action;
        if (id == actions.previous)
        {
            return movement::previous;
        }

        if (id == actions.next)
        {
            return movement::next;
        }

        if (id == actions.first)
        {
            return movement::first;
        }

        if (id == actions.last)
        {
            return movement::last;
        }
    }

    if (raw.kind != input_kind::key_down)
    {
        return movement::none;
    }

    const auto key = raw.key;
    if (key == static_cast<unsigned>(input::key::home))
    {
        return movement::first;
    }

    if (key == static_cast<unsigned>(input::key::end))
    {
        return movement::last;
    }

    if ((panel.vertical && key == static_cast<unsigned>(input::key::up)) ||
        (panel.horizontal && key == static_cast<unsigned>(input::key::left)))
    {
        return movement::previous;
    }

    if ((panel.vertical && key == static_cast<unsigned>(input::key::down)) ||
        (panel.horizontal && key == static_cast<unsigned>(input::key::right)))
    {
        return movement::next;
    }

    return movement::none;
}
} // namespace

/// @brief Distinct IDs avoid ambiguous bindings; no physical controller SDK is required.
void input_session::configure_scroll_navigation(scroll_navigation_actions actions)
{
    impl_->idle();
    const std::array ids{actions.previous, actions.next, actions.first, actions.last};
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        for (std::size_t other = index + 1; other < ids.size(); ++other)
        {
            if (ids[index] && ids[index] == ids[other])
            {
                throw std::invalid_argument("duplicate scroll navigation action");
            }
        }
    }

    impl_->navigation = actions;
}

/// @brief Numeric layout order plus stable slot ties handles reordered lists deterministically.
bool input_session::impl::scroll_navigation(element_handle current, input_event raw)
{
    const auto* node = detail::find_element(scrolling->source, current);
    const auto* panel = node ? std::get_if<scroll_layout>(&node->panel) : nullptr;
    if (!panel)
    {
        return false;
    }

    const auto move = direction(raw, navigation, *panel);
    if (move == movement::none)
    {
        return false;
    }

    const auto focused = scrolling->geometry.find(focus_target);
    if (!focused)
    {
        return false;
    }

    const diagnostics::operation_scope scope{destinations,
                                             {"ui.input.scroll.navigate", "ui.dispatch"}};
    const auto key = [&](layout_rect bounds, element_handle target)
    {
        return panel->vertical ? std::tuple{bounds.y, bounds.x, target.index}
                               : std::tuple{bounds.x, bounds.y, target.index};
    };
    const auto origin = key(focused->bounds, focus_target);
    auto selected = focus_target;
    auto selected_key = origin;
    bool found = false;
    for (const auto& candidate : bindings)
    {
        if (candidate.target == current || !candidate.options.focusable ||
            !eligible(candidate.target) ||
            !detail::is_descendant(scrolling->source, candidate.target, current,
                                   limits.max_route_depth))
        {
            continue;
        }

        const auto geometry = scrolling->geometry.find(candidate.target);
        if (!geometry)
        {
            continue;
        }

        const auto candidate_key = key(geometry->bounds, candidate.target);
        const bool allowed =
            move == movement::first || move == movement::last ||
            (move == movement::next ? candidate_key > origin : candidate_key < origin);
        const bool lower = move == movement::first || move == movement::next;
        if (allowed &&
            (!found || (lower ? candidate_key < selected_key : candidate_key > selected_key)))
        {
            selected = candidate.target;
            selected_key = candidate_key;
            found = true;
        }
    }

    if (selected == focus_target)
    {
        return false;
    }

    set_focus(selected);
    return true;
}
} // namespace atlas::ui
