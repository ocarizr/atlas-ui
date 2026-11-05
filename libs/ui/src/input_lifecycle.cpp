/** @file @brief Safe cleanup and bounded serial callback delivery. */
#include "input_state.hpp"
#include <cmath>

namespace atlas::ui
{
namespace
{
/// @brief Finite inputs can still have an unrepresentable difference on captured motion.
layout_point local_position(layout_point position, layout_rect bounds)
{
    const auto checked = [](double value)
    {
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
        {
            throw std::overflow_error("UI local input coordinate overflow");
        }

        return static_cast<float>(value);
    };
    return {checked(static_cast<double>(position.x) - bounds.x),
            checked(static_cast<double>(position.y) - bounds.y)};
}
} // namespace

/// @brief Missing or reused slots never resolve to another generation.
const detail::presentation_region* input_session::impl::region(element_handle target) const
{
    if (target.session != session || target.index >= slots.size())
    {
        return nullptr;
    }

    const auto index = slots[target.index];
    return index < regions.size() && regions[index].target == target ? &regions[index] : nullptr;
}

/// @brief Direct tree removal/hide/reparent is detected before subsequent route deliveries.
bool input_session::impl::eligible(element_handle target) const
{
    const auto* value = region(target);
    if (!value || !value->visible)
    {
        return false;
    }

    for (std::size_t depth = 0; target.generation != 0; ++depth)
    {
        value = region(target);
        if (!value || depth >= limits.max_route_depth || !tree.contains(target) ||
            !tree.requested(target).visible || tree.parent(target) != value->parent)
        {
            return false;
        }

        const auto* binding = find(target);
        if (binding && (!binding->options.enabled || binding->retiring || binding->ended))
        {
            return false;
        }

        target = value->parent;
    }

    return true;
}

/// @brief Cleanup keeps the old full identity even when the live tree has reused its slot.
event_response input_session::impl::send(element_handle current, event_kind kind, route_phase phase,
                                         element_handle target, input_event raw)
{
    auto* binding = find(current);
    if (!binding)
    {
        return {};
    }

    if (stats.callbacks == limits.max_callbacks)
    {
        throw std::overflow_error("UI input callback limit exhausted");
    }

    ++stats.callbacks;
    layout_point local = raw.position;
    layout_rect current_bounds{}, clip{}, target_bounds{};
    if (const auto* value = region(current))
    {
        current_bounds = value->bounds;
        clip = value->clip;
        local = local_position(raw.position, value->bounds);
    }

    if (const auto* value = region(target))
    {
        target_bounds = value->bounds;
    }

    const diagnostics::operation_scope scope{destinations, {"ui.input.callback", "ui.dispatch"}};
    return binding->handler->on_event({kind, phase, target, current, raw.position, local,
                                       target_bounds, current_bounds, clip, raw.key, stats.frame,
                                       delta, tree.contains(target)},
                                      tree.commands());
}

/// @brief State is invalidated before a possibly throwing cleanup callback.
void input_session::impl::clear_capture()
{
    const auto previous = std::exchange(capture_target, {});
    if (previous.generation != 0)
    {
        (void)send(previous, event_kind::capture_lost, route_phase::direct, previous);
    }
}

/// @brief Hover transitions refer to geometric targets, not the capture recipient.
void input_session::impl::set_hover(element_handle target, input_event raw)
{
    if (hover_target == target)
    {
        return;
    }

    const auto previous = std::exchange(hover_target, {});
    if (previous.generation != 0)
    {
        (void)send(previous, event_kind::pointer_leave, route_phase::direct, previous, raw);
    }

    if (target.generation != 0 && eligible(target))
    {
        hover_target = target;
        (void)send(target, event_kind::pointer_enter, route_phase::direct, target, raw);
    }
}

/// @brief Reconciliation never invokes a removed target's raw input body.
void input_session::impl::reconcile()
{
    if (capture_target.generation != 0 && !eligible(capture_target))
    {
        clear_capture();
    }

    const auto* focus_binding = find(focus_target);
    if (focus_target.generation != 0 &&
        (!eligible(focus_target) || !focus_binding || !focus_binding->options.focusable))
    {
        set_focus({});
    }

    if (hover_target.generation != 0 && !eligible(hover_target))
    {
        set_hover({});
    }

    for (auto& binding : bindings)
    {
        if (binding.enabled && !eligible(binding.target))
        {
            binding.enabled = false;
            (void)send(binding.target, event_kind::disable, route_phase::direct, binding.target);
        }

        if (!binding.ended && (binding.retiring || !tree.contains(binding.target)))
        {
            binding.ended = true;
            if (binding.started)
            {
                (void)send(binding.target, event_kind::end, route_phase::direct, binding.target);
            }
        }
    }
}

/// @brief System order is stable registration identity order, independent of worker completion.
void input_session::impl::broadcast(event_kind kind)
{
    reconcile();
    for (auto& binding : bindings)
    {
        if (binding.enabled && eligible(binding.target))
        {
            (void)send(binding.target, kind, route_phase::direct, binding.target);
        }
    }

    reconcile();
}

/// @brief Earlier sealed publications survive; unsealed commands and stale input ownership do not.
void input_session::impl::fail() noexcept
{
    stats.failed = true;
    stage = 5;
    focus_target = capture_target = hover_target = {};
    try
    {
        tree.commands().discard_pending();
    }
    catch (...)
    {
        diagnostics::write(destinations, diagnostics::log_level::warning, "ui.input",
                           "unsealed input command cleanup also failed");
    }

    diagnostics::write(destinations, diagnostics::log_level::warning, "ui.input",
                       "input frame failed; unsealed commands discarded");
}
} // namespace atlas::ui
