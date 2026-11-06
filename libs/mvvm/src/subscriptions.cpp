/** @file @brief Retained instance-bound subscriptions with callback-safe retirement. */
#include "runtime_state.hpp"
#include <algorithm>
#include <limits>

namespace atlas::mvvm
{
/// @brief Typed slot namespaces occupy disjoint contiguous bucket ranges.
std::size_t view_model::impl::bucket(notification_kind kind, std::uint32_t index) const
{
    if (kind == notification_kind::changed)
    {
        return index;
    }

    return layout->source.fields.size() + index +
           (kind == notification_kind::event ? layout->source.actions.size() : 0);
}

/// @brief Boundary-only unlink preserves live notification traversal and registration order.
void view_model::impl::unlink(std::size_t index)
{
    auto& chain = buckets[bucket(listeners[index].kind, listeners[index].index)];
    const auto none = std::numeric_limits<std::size_t>::max();
    auto previous = none;
    for (auto current = chain.head; current != none; current = listeners[current].next)
    {
        if (current == index)
        {
            if (previous == none)
            {
                chain.head = listeners[current].next;
            }
            else
            {
                listeners[previous].next = listeners[current].next;
            }

            if (chain.tail == current)
            {
                chain.tail = previous;
            }

            return;
        }

        previous = current;
    }

    throw std::logic_error("corrupt MVVM observer chain");
}

/// @brief Reuse vacant metadata but never reuse a token that an old owner might retain.
subscription view_model::impl::add(notification_kind kind, std::uint32_t index,
                                   std::shared_ptr<observer> listener)
{
    owner();
    if (flushing || admitting)
    {
        throw std::logic_error("MVVM subscription changes require a dispatch boundary");
    }

    if (!listener)
    {
        throw std::invalid_argument("null MVVM observer");
    }

    if (sequence == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("MVVM subscription identities exhausted");
    }

    const auto vacant = std::find_if(listeners.begin(), listeners.end(),
                                     [](const auto& item)
                                     {
                                         return !item.listener;
                                     });
    if (vacant == listeners.end() && listeners.size() == limits.max_subscriptions)
    {
        throw std::overflow_error("MVVM subscription capacity exhausted");
    }

    const auto slot = vacant == listeners.end()
                          ? listeners.size()
                          : static_cast<std::size_t>(vacant - listeners.begin());
    if (vacant != listeners.end())
    {
        unlink(slot);
    }

    const auto token = ++sequence;
    registration record{kind, index, token, std::move(listener)};
    if (slot == listeners.size())
    {
        listeners.push_back(std::move(record));
    }
    else
    {
        listeners[slot] = std::move(record);
    }

    auto& chain = buckets[bucket(kind, index)];
    if (chain.tail == std::numeric_limits<std::size_t>::max())
    {
        chain.head = slot;
    }
    else
    {
        listeners[chain.tail].next = slot;
    }

    chain.tail = slot;
    return {instance, token};
}

/// @brief Fields notify only after distinct values commit.
subscription view_model::subscribe(field_handle slot, std::shared_ptr<observer> listener)
{
    impl_->owner();
    (void)impl_->description.type(slot);
    return impl_->add(notification_kind::changed, slot.index, std::move(listener));
}

/// @brief Actions retain input-protocol observers independently of field listeners.
subscription view_model::subscribe(action_handle slot, std::shared_ptr<observer> listener)
{
    impl_->owner();
    if (slot.schema != impl_->description.identity() ||
        slot.index >= impl_->layout->source.actions.size())
    {
        throw std::invalid_argument("foreign or invalid MVVM action slot");
    }

    return impl_->add(notification_kind::action, slot.index, std::move(listener));
}

/// @brief Explicit output events have their own validated namespace.
subscription view_model::subscribe(event_handle slot, std::shared_ptr<observer> listener)
{
    impl_->owner();
    if (slot.schema != impl_->description.identity() ||
        slot.index >= impl_->layout->source.events.size())
    {
        throw std::invalid_argument("foreign or invalid MVVM event slot");
    }

    return impl_->add(notification_kind::event, slot.index, std::move(listener));
}

/// @brief Removing a listener during delivery suppresses its future callbacks without invalidating
/// traversal.
void view_model::unsubscribe(subscription token)
{
    impl_->owner();
    if (impl_->admitting)
    {
        throw std::logic_error("MVVM subscription retirement during admission");
    }

    if (token.instance != impl_->instance || !token.sequence)
    {
        throw std::invalid_argument("foreign MVVM subscription");
    }

    const auto it = std::find_if(impl_->listeners.begin(), impl_->listeners.end(),
                                 [&](const auto& r)
                                 {
                                     return r.token == token.sequence;
                                 });
    if (it != impl_->listeners.end())
    {
        const detail::flag_guard guard{impl_->admitting};
        it->listener.reset();
    }
}
} // namespace atlas::mvvm
