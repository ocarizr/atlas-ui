/** @file @brief Clipped hierarchical drop negotiation and deterministic cross-panel traversal. */
#include "drag_state.hpp"

namespace atlas::ui
{
/// @brief Bound candidate and policy work for this domain frame.
void drag_drop_session::impl::visit()
{
    if (queries == limits.max_queries)
    {
        throw std::overflow_error("drag query budget exhausted");
    }

    ++queries;
}

/// @brief Intersect typed capabilities before asking a pure dynamic destination policy.
drag_operation drag_drop_session::impl::negotiate(const endpoint& candidate)
{
    visit();
    const auto* options = target_options(candidate);
    if (!valid(candidate) || !options || candidate.target == source.target ||
        std::find(options->types.begin(), options->types.end(), payload.type()) ==
            options->types.end())
    {
        return drag_operation::none;
    }

    const auto capabilities =
        static_cast<unsigned>(options->operations) & static_cast<unsigned>(offered.operations);
    auto operation = preferred;
    const auto supports = [&](drag_operation choice)
    {
        return choice != drag_operation::none &&
               (capabilities & static_cast<unsigned>(choice)) != 0;
    };
    if (operation == drag_operation::none)
    {
        operation = supports(drag_operation::copy) ? drag_operation::copy : drag_operation::move;
    }

    if (!supports(operation))
    {
        return drag_operation::none;
    }

    const auto old_operation = feedback.operation;
    const auto old_target = target;
    feedback.operation = operation;
    target = candidate;
    event_response response;
    try
    {
        response = emit(candidate, event_kind::drag_query);
    }
    catch (...)
    {
        target = old_target;
        feedback.operation = old_operation;
        throw;
    }

    target = old_target;
    feedback.operation = old_operation;
    if (response.acceptance > drag_acceptance::reject || response.operation > drag_operation::move)
    {
        throw std::invalid_argument("invalid drag query response");
    }

    if (!valid(candidate) || response.acceptance == drag_acceptance::reject)
    {
        return drag_operation::none;
    }

    if (response.operation != drag_operation::none)
    {
        if (!supports(response.operation) ||
            (preferred != drag_operation::none && response.operation != preferred))
        {
            return drag_operation::none;
        }

        operation = response.operation;
    }

    return operation;
}

/// @brief Notify ordered destination transitions without changing the layout tree.
void drag_drop_session::impl::select(endpoint candidate, drag_operation operation)
{
    if (target.target == candidate.target && target.lifetime.lock() == candidate.lifetime.lock())
    {
        feedback.operation = operation;
        if (candidate.target.generation)
        {
            (void)emit(target, event_kind::drag_over);
        }

        return;
    }

    const auto previous = target;
    target = std::move(candidate);
    feedback.target = target.target;
    feedback.operation = operation;
    if (feedback.keyboard)
    {
        if (auto* state = view(target))
        {
            if (const auto* region = state->region(target.target))
            {
                feedback.position = {region->bounds.x, region->bounds.y};
            }
        }
    }

    (void)emit(previous, event_kind::drag_leave);
    (void)emit(target, event_kind::drag_enter);
}

/// @brief Choose the deepest accepting ancestor of the frontmost clipped hit.
void drag_drop_session::impl::point(layout_point position)
{
    feedback.position = position;
    for (auto it = views.rbegin(); it != views.rend(); ++it)
    {
        const auto lease = it->lock();
        if (!lease || !lease->owner)
        {
            continue;
        }

        auto& input = *lease->owner;
        visit();
        auto node = input.impl_->hit(position);
        if (!node.generation)
        {
            continue;
        }

        while (node.generation)
        {
            const auto candidate = capture(input, node);
            const auto operation = negotiate(candidate);
            if (operation != drag_operation::none)
            {
                select(candidate, operation);
                return;
            }

            const auto* region = input.impl_->region(node);
            node = region ? region->parent : element_handle{};
        }

        // A frontmost hit occludes panels below even if none of its ancestors accepts.
        break;
    }

    select({}, drag_operation::none);
}

/// @brief Traverse eligible presented targets across all admitted panels.
void drag_drop_session::impl::next(bool previous)
{
    if (feedback.phase != drag_phase::dragging)
    {
        return;
    }

    endpoint first{}, last{}, before{}, after{};
    drag_operation first_op{}, last_op{}, before_op{}, after_op{};
    bool found = !target.target.generation;
    for (const auto& weak : views)
    {
        const auto lease = weak.lock();
        if (!lease || !lease->owner)
        {
            continue;
        }

        auto& input = *lease->owner;
        for (const auto& region : input.impl_->regions)
        {
            const auto candidate = capture(input, region.target);
            const auto operation = negotiate(candidate);
            const auto visible = intersect_clip(region.bounds, region.clip);
            if (operation == drag_operation::none || visible.width <= 0 || visible.height <= 0)
            {
                continue;
            }

            if (!first.target.generation)
            {
                first = candidate;
                first_op = operation;
            }

            last = candidate;
            last_op = operation;
            const bool current = target.target == candidate.target &&
                                 target.lifetime.lock() == candidate.lifetime.lock();
            if (current)
            {
                found = true;
                continue;
            }

            if (!found || !target.target.generation)
            {
                before = candidate;
                before_op = operation;
            }

            if (found && !after.target.generation)
            {
                after = candidate;
                after_op = operation;
            }
        }
    }

    if (previous)
    {
        select(before.target.generation ? before : last,
               before.target.generation ? before_op : last_op);
    }
    else
    {
        select(after.target.generation ? after : first,
               after.target.generation ? after_op : first_op);
    }
}
} // namespace atlas::ui
