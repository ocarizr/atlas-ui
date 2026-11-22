/** @file @brief Sparse coalesced scroll requests publish through the existing command boundary. */
#include "input_state.hpp"
#include "snapshot_lookup.hpp"
#include <cmath>

namespace atlas::ui
{
/// @brief Exact captured identity lookup never observes a recycled viewport generation.
pending_scroll* input_session::impl::scroll(element_handle target)
{
    if (!scrolling)
    {
        return nullptr;
    }

    auto& values = scrolling->pending;
    const auto found = std::lower_bound(values.begin(), values.end(), target.index,
                                        [](const pending_scroll& value, std::uint32_t index)
                                        {
                                            return value.target.index < index;
                                        });
    return found != values.end() && found->target == target ? &*found : nullptr;
}

/// @brief Frozen input regions remain unchanged until a new coherent frame is prepared.
void input_session::impl::queue_scroll(pending_scroll& pending, layout_point offset)
{
    const layout_point next{std::clamp(offset.x, 0.f, pending.geometry.range.x),
                            std::clamp(offset.y, 0.f, pending.geometry.range.y)};
    pending.requested = pending.requested || next != pending.offset;
    pending.offset = next;
}

/// @brief Input and custom handlers may coexist on chrome and viewport elements.
bool input_session::impl::scroll_interactive(element_handle target) const
{
    if (!scrolling)
    {
        return false;
    }

    const auto bar = scrolling->geometry.find_scrollbar(target);
    return bar ? bar->enabled : scrolling->geometry.find_scroll(target).has_value();
}

/// @brief Each axis is written once per frame; repeated wheel packets do not create conflicts.
void input_session::impl::flush_scroll()
{
    if (!scrolling)
    {
        return;
    }

    const diagnostics::operation_scope scope{destinations,
                                             {"ui.input.scroll.flush", "ui.dispatch"}};
    std::size_t writes = 0;
    const auto changed = [&](const pending_scroll& pending, auto emit)
    {
        if (!eligible(pending.target))
        {
            return;
        }

        const auto panel = tree.panel(pending.target);
        if (const auto* policy = std::get_if<scroll_layout>(&panel))
        {
            if (!pending.requested)
            {
                const auto* node = detail::find_element(scrolling->source, pending.target);
                const auto* captured = node ? std::get_if<scroll_layout>(&node->panel) : nullptr;
                if (!captured || captured->offset == pending.geometry.offset ||
                    policy->offset != captured->offset)
                {
                    return;
                }
            }

            if (policy->offset.x != pending.offset.x)
            {
                emit(property::scroll_x, pending.offset.x);
            }

            if (policy->offset.y != pending.offset.y)
            {
                emit(property::scroll_y, pending.offset.y);
            }
        }
    };
    for (const auto& pending : scrolling->pending)
    {
        changed(pending,
                [&](property, float)
                {
                    ++writes;
                });
    }

    if (!writes)
    {
        scrolling->focus = focus_target;
        return;
    }

    tree.commands().require_capacity(writes);
    constexpr auto scroll_producer = std::numeric_limits<std::uint64_t>::max();
    for (const auto& pending : scrolling->pending)
    {
        changed(pending,
                [&](property field, float value)
                {
                    tree.commands().set(pending.target, field, value, scroll_producer);
                });
    }

    scrolling->focus = focus_target;
}

/// @brief Semantic keyboard/gamepad adapters can request logical deltas without native key codes.
void input_session::scroll_by(element_handle viewport, layout_point delta)
{
    impl_->open();
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y))
    {
        throw std::invalid_argument("invalid finite UI scroll delta");
    }

    auto* pending = impl_->scroll(viewport);
    if (!pending || !impl_->eligible(viewport))
    {
        throw std::invalid_argument("scroll needs a live enabled viewport in the input frame");
    }

    const impl::guard guard{impl_->busy};
    const auto add = [](float position, float change, float range)
    {
        return static_cast<float>(
            std::clamp(static_cast<double>(position) + change, 0., static_cast<double>(range)));
    };
    impl_->queue_scroll(*pending, {add(pending->offset.x, delta.x, pending->geometry.range.x),
                                   add(pending->offset.y, delta.y, pending->geometry.range.y)});
}
} // namespace atlas::ui
