/** @file @brief Copied action maps, deferred context requests and bounded rebinding. */
#include "input_state.hpp"

namespace atlas::input
{
/// @brief Stable storage indexes are resolved only during configuration.
std::size_t input_system::impl::action_index(action_id id) const
{
    const auto it = std::find_if(actions.begin(), actions.end(),
                                 [id](const auto& a)
                                 {
                                     return a.id == id;
                                 });
    if (it == actions.end())
    {
        throw std::invalid_argument("unknown input action");
    }

    return static_cast<std::size_t>(it - actions.begin());
}

/// @brief Enable requests cannot implicitly invent maps.
context_record& input_system::impl::context(context_id id)
{
    const auto it = std::find_if(contexts.begin(), contexts.end(),
                                 [id](const auto& c)
                                 {
                                     return c.id == id;
                                 });
    if (it == contexts.end())
    {
        throw std::invalid_argument("unknown input context");
    }

    return *it;
}

/// @brief Validate every reference before configuration publication.
void input_system::impl::validate_bindings(std::span<const binding> values) const
{
    for (const auto& b : values)
    {
        (void)action_index(b.action);
        if (!b.control.code || b.control.device > device_kind::gamepad)
        {
            throw std::invalid_argument("invalid input control binding");
        }
    }
}

/// @brief Names and immutable processors remain owned after caller metadata disappears.
void input_system::define_action(const action_definition& definition)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    if (!definition.id || definition.type > value_type::axis2d ||
        std::any_of(impl_->actions.begin(), impl_->actions.end(),
                    [&](const auto& a)
                    {
                        return a.id == definition.id;
                    }))
    {
        throw std::invalid_argument("invalid or duplicate input action");
    }

    const auto name = checked_name(definition.name);
    if (impl_->actions.size() == impl_->limits.max_actions)
    {
        throw std::overflow_error("input action capacity exhausted");
    }

    impl_->actions.push_back(
        {definition.id, name, definition.type, definition.modifier, definition.trigger});
}

/// @brief Descending priority, then ascending map identity, defines precedence.
void input_system::define_context(const context_definition& definition)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    if (!definition.id || definition.activation > activation_policy::allow_held ||
        std::any_of(impl_->contexts.begin(), impl_->contexts.end(),
                    [&](const auto& c)
                    {
                        return c.id == definition.id;
                    }))
    {
        throw std::invalid_argument("invalid or duplicate input context");
    }

    const auto name = checked_name(definition.name);
    impl_->validate_bindings(definition.bindings);
    if (impl_->contexts.size() == impl_->limits.max_contexts ||
        definition.bindings.size() > impl_->limits.max_bindings - impl_->bindings.size())
    {
        throw std::overflow_error("input context/binding capacity exhausted");
    }

    impl_->contexts.push_back(
        {definition.id, name, definition.priority, definition.enabled, definition.activation});
    impl_->contexts.back().begin = impl_->bindings.size();
    impl_->contexts.back().count = definition.bindings.size();
    for (const auto& b : definition.bindings)
    {
        impl_->bindings.push_back({definition.id, impl_->action_index(b.action), b});
    }

    impl_->order.push_back(impl_->contexts.size() - 1);
    std::sort(impl_->order.begin(), impl_->order.end(),
              [&](std::size_t a, std::size_t b)
              {
                  const auto& ca = impl_->contexts[a];
                  const auto& cb = impl_->contexts[b];
                  return ca.priority != cb.priority ? ca.priority > cb.priority : ca.id < cb.id;
              });
}

/// @brief Replacement is atomic; interaction generation advances at the next dispatch.
void input_system::rebind(context_id id, std::span<const binding> values)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    auto& context = impl_->context(id);
    impl_->validate_bindings(values);
    const auto old =
        static_cast<std::size_t>(std::count_if(impl_->bindings.begin(), impl_->bindings.end(),
                                               [id](const auto& b)
                                               {
                                                   return b.context == id;
                                               }));
    if (values.size() > impl_->limits.max_bindings - (impl_->bindings.size() - old))
    {
        throw std::overflow_error("input binding capacity exhausted");
    }

    std::pmr::vector<binding_record> replacement(0, impl_->memory.get());
    replacement.reserve(impl_->limits.max_bindings);
    for (const auto& b : impl_->bindings)
    {
        if (b.context != id)
        {
            replacement.push_back(b);
        }
    }

    for (const auto& b : values)
    {
        replacement.push_back({id, impl_->action_index(b.action), b});
    }

    impl_->bindings.swap(replacement);
    context.rearm = true;
    for (auto& c : impl_->contexts)
    {
        const auto first = std::find_if(impl_->bindings.begin(), impl_->bindings.end(),
                                        [&](const auto& b)
                                        {
                                            return b.context == c.id;
                                        });
        c.begin = static_cast<std::size_t>(first - impl_->bindings.begin());
        c.count = static_cast<std::size_t>(std::count_if(first, impl_->bindings.end(),
                                                         [&](const auto& b)
                                                         {
                                                             return b.context == c.id;
                                                         }));
    }
}

/// @brief Callback demands apply next dispatch, never halfway through the current packet batch.
void input_system::request_context(context_id id, bool enabled)
{
    impl_->check_thread();
    if (impl_->requesting || (impl_->busy && !impl_->delivering))
    {
        throw std::logic_error("input context request reentry");
    }

    const impl::guard guard{impl_->requesting};
    (void)impl_->context(id);
    const auto it = std::find_if(impl_->pending.begin(), impl_->pending.end(),
                                 [id](const auto& r)
                                 {
                                     return r.context == id;
                                 });
    if (it == impl_->pending.end())
    {
        impl_->pending.push_back({id, enabled});
        return;
    }

    const bool conflict = it->enabled != enabled;
    it->enabled = enabled;
    if (conflict)
    {
        diagnostics::write(impl_->destinations, diagnostics::log_level::warning, "input",
                           "conflicting context requests; last request selected");
    }
}
} // namespace atlas::input
