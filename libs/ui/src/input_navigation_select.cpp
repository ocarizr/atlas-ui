/** @file @brief Bounded focus search and guarded owner publication using existing focus/reveal. */
#include "input_navigation_search.hpp"
#include "input_state.hpp"

namespace atlas::ui
{
/// @brief Offscreen candidates remain eligible for reveal, but empty/hidden/disabled borders do
/// not.
bool input_session::impl::focus_eligible(element_handle target) const
{
    const auto* binding = find(target);
    const auto* geometry = region(target);
    return binding && binding->options.focusable && eligible(target) && geometry &&
           geometry->bounds.width > 0 && geometry->bounds.height > 0;
}

/// @brief Missing explicit destinations deliberately block instead of picking an unintended target.
std::optional<element_handle> input_session::impl::explicit_focus(element_handle origin,
                                                                  focus_direction direction) const
{
    const auto options = focus_options(origin);
    const auto index = static_cast<std::size_t>(direction);
    if (options.blocked[index])
    {
        return element_handle{};
    }

    const auto target = options.destinations[index];
    if (!target.generation)
    {
        return std::nullopt;
    }

    if (!focus_eligible(target))
    {
        diagnostics::write(destinations, diagnostics::log_level::warning, "ui.navigation",
                           "explicit focus destination unavailable");
        return element_handle{};
    }

    return target;
}

/// @brief Count every candidate traversal before callback publication, without allocating scratch.
element_handle input_session::impl::scan_focus(focus_direction direction, element_handle origin,
                                               element_handle domain, bool wrap)
{
    const auto visit = [&]
    {
        if (stats.navigation_candidates == limits.max_navigation_candidates)
        {
            throw std::overflow_error("focus navigation candidate budget exhausted");
        }

        ++stats.navigation_candidates;
    };
    const auto options = focus_options(origin);
    detail::focus_candidate start{origin,
                                  origin.generation ? region(origin)->bounds : layout_rect{},
                                  options.tab_order, 0, options.tab_stop};
    if (origin.generation &&
        (direction == focus_direction::next || direction == focus_direction::previous))
    {
        for (const auto& item : bindings)
        {
            visit();
            if (item.target == origin)
            {
                break;
            }

            ++start.ordinal;
        }
    }

    detail::focus_search search{direction, start};
    std::size_t ordinal = 0;
    for (const auto& item : bindings)
    {
        visit();
        const auto order = ordinal++;
        if (item.target == origin || !focus_eligible(item.target) ||
            (domain.generation && focus_domain(item.target) != domain))
        {
            continue;
        }

        const auto policy = focus_options(item.target);
        search.offer(
            {item.target, region(item.target)->bounds, policy.tab_order, order, policy.tab_stop});
    }

    return search.selected(wrap);
}

/// @brief Group-targeted actions can navigate their remembered member while focus is in content.
bool input_session::impl::move_focus(focus_direction direction, element_handle requested_scope)
{
    const diagnostics::operation_scope trace{destinations, {"ui.input.navigate", "ui.dispatch"}};
    if (direction > focus_direction::previous)
    {
        throw std::invalid_argument("invalid focus direction");
    }

    auto origin = focus_target;
    const auto domain = requested_scope.generation ? requested_scope : focus_domain(origin);
    const auto* scope = focus_scope_for(domain);
    if (requested_scope.generation && (!region(domain) || !eligible(domain)))
    {
        return false;
    }

    if (requested_scope.generation && focus_domain(origin) != domain)
    {
        origin =
            scope && focus_eligible(scope->remembered) && focus_domain(scope->remembered) == domain
                ? scope->remembered
                : element_handle{};
    }

    if (!focus_eligible(origin))
    {
        origin = {};
    }

    element_handle selected{};
    const auto target = origin.generation ? explicit_focus(origin, direction) : std::nullopt;
    if (target)
    {
        selected = *target;
    }
    else
    {
        const bool ordered =
            direction == focus_direction::next || direction == focus_direction::previous;
        const bool horizontal =
            direction == focus_direction::left || direction == focus_direction::right;
        const bool wrap = scope && (ordered      ? scope->options.wrap_tab
                                    : horizontal ? scope->options.wrap_horizontal
                                                 : scope->options.wrap_vertical);
        selected = scan_focus(direction, origin, domain, wrap);
    }

    if (!selected.generation || selected == focus_target)
    {
        return false;
    }

    set_focus(selected);
    return focus_target == selected;
}

/// @brief Programmatic requests share packet handling's owner, failure and reveal contracts.
bool input_session::navigate(focus_direction direction, element_handle scope)
{
    impl_->open();
    const diagnostics::operation_scope trace{impl_->destinations,
                                             {"ui.input.navigate.request", "ui.dispatch"}};
    const impl::guard guard{impl_->busy};
    try
    {
        const bool changed = impl_->move_focus(direction, scope);
        impl_->reconcile();
        return changed && impl_->focus_target.generation != 0;
    }
    catch (...)
    {
        impl_->fail();
        throw;
    }
}
} // namespace atlas::ui
