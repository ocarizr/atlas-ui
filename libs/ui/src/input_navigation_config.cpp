/** @file @brief Owner-bound sparse focus configuration with atomic validation before mutation. */
#include "input_state.hpp"

namespace atlas::ui
{
namespace
{
/// @brief No ID can ambiguously target two independent focus groups.
void validate_actions(std::span<const detail::focus_scope> scopes, focus_navigation_actions global,
                      std::optional<detail::focus_scope> replacement = {})
{
    const auto duplicate = [](auto ids, auto other)
    {
        for (auto id : ids)
        {
            for (auto candidate : other)
            {
                if (id && id == candidate)
                {
                    return true;
                }
            }
        }

        return false;
    };
    const auto local = detail::focus_actions(replacement ? replacement->options.actions : global);
    for (std::size_t i = 0; i < local.size(); ++i)
    {
        for (std::size_t j = i + 1; j < local.size(); ++j)
        {
            if (local[i] && local[i] == local[j])
            {
                throw std::invalid_argument("duplicate focus navigation action");
            }
        }
    }

    if (replacement && duplicate(local, detail::focus_actions(global)))
    {
        throw std::invalid_argument("ambiguous global/group focus action");
    }

    for (const auto& scope : scopes)
    {
        if (replacement && scope.target == replacement->target)
        {
            continue;
        }

        if (duplicate(local, detail::focus_actions(scope.options.actions)))
        {
            throw std::invalid_argument("ambiguous focus group action");
        }
    }
}
} // namespace

/// @brief Keyboard traversal is explicit opt-in; semantic actions remain device-independent.
void input_session::configure_navigation(focus_navigation_actions actions, bool keyboard)
{
    impl_->idle();
    std::erase_if(impl_->focus_scopes,
                  [&](const auto& scope)
                  {
                      return !impl_->tree.contains(scope.target);
                  });
    validate_actions(impl_->focus_scopes, actions);
    impl_->focus_actions = actions;
    impl_->focus_keyboard = keyboard;
    impl_->focus_shift = false;
}

/// @brief Only non-default relationships allocate metadata, bounded by existing input admission.
void input_session::configure_navigation(element_handle target,
                                         const focus_navigation_options& options)
{
    impl_->idle();
    const auto* binding = impl_->find(target);
    if (!binding || binding->retiring || binding->ended || !impl_->tree.contains(target))
    {
        throw std::invalid_argument("focus navigation needs a live bound element");
    }

    for (std::size_t i = 0; i < options.destinations.size(); ++i)
    {
        const auto destination = options.destinations[i];
        if ((options.blocked[i] && destination.generation) ||
            (destination.generation && !impl_->tree.contains(destination)))
        {
            throw std::invalid_argument("invalid explicit focus navigation destination");
        }
    }

    std::erase_if(impl_->focus_nodes,
                  [&](const auto& node)
                  {
                      return !impl_->tree.contains(node.target);
                  });
    auto found = std::find_if(impl_->focus_nodes.begin(), impl_->focus_nodes.end(),
                              [&](const auto& node)
                              {
                                  return node.target == target;
                              });
    if (options == focus_navigation_options{})
    {
        if (found != impl_->focus_nodes.end())
        {
            impl_->focus_nodes.erase(found);
        }

        return;
    }

    if (found != impl_->focus_nodes.end())
    {
        found->options = options;
        return;
    }

    if (impl_->focus_nodes.size() >= impl_->limits.max_bindings)
    {
        throw std::overflow_error("focus navigation metadata budget exhausted");
    }

    impl_->focus_nodes.push_back({target, options});
}

/// @brief Scope actions remember their group entry while focus moves into other content.
void input_session::configure_navigation_scope(element_handle root, focus_navigation_scope options)
{
    impl_->idle();
    if (!impl_->tree.contains(root))
    {
        throw std::invalid_argument("focus scope needs a live root");
    }

    std::erase_if(impl_->focus_scopes,
                  [&](const auto& scope)
                  {
                      return !impl_->tree.contains(scope.target);
                  });
    detail::focus_scope candidate{root, options};
    validate_actions(impl_->focus_scopes, impl_->focus_actions, candidate);
    auto found = std::find_if(impl_->focus_scopes.begin(), impl_->focus_scopes.end(),
                              [&](const auto& scope)
                              {
                                  return scope.target == root;
                              });
    if (found != impl_->focus_scopes.end())
    {
        found->options = options;
        return;
    }

    if (impl_->focus_scopes.size() >= impl_->limits.max_bindings)
    {
        throw std::overflow_error("focus scope budget exhausted");
    }

    impl_->focus_scopes.push_back(candidate);
}

/// @brief Removing a group restores automatic traversal to the enclosing group/root.
void input_session::reset_navigation_scope(element_handle root)
{
    impl_->idle();
    std::erase_if(impl_->focus_scopes,
                  [&](const auto& scope)
                  {
                      return scope.target == root;
                  });
}

/// @brief Copy author options without exposing live mutable metadata.
focus_navigation_options input_session::impl::focus_options(element_handle target) const
{
    const auto found = std::find_if(focus_nodes.begin(), focus_nodes.end(),
                                    [&](const auto& node)
                                    {
                                        return node.target == target;
                                    });
    return found == focus_nodes.end() ? focus_navigation_options{} : found->options;
}

/// @brief Locate group metadata with complete identities, never slot-only matching.
detail::focus_scope* input_session::impl::focus_scope_for(element_handle target)
{
    const auto found = std::find_if(focus_scopes.begin(), focus_scopes.end(),
                                    [&](const auto& scope)
                                    {
                                        return scope.target == target;
                                    });
    return found == focus_scopes.end() ? nullptr : &*found;
}

/// @brief Automatic search stops at a nearest explicit group, otherwise the independent root.
element_handle input_session::impl::focus_domain(element_handle target) const
{
    element_handle root{};
    for (std::size_t depth = 0; target.generation && depth < limits.max_route_depth; ++depth)
    {
        const auto* value = region(target);
        if (!value)
        {
            return {};
        }

        root = target;
        if (std::ranges::any_of(focus_scopes,
                                [&](const auto& scope)
                                {
                                    return scope.target == target;
                                }))
        {
            return target;
        }

        target = value->parent;
    }

    return root;
}
} // namespace atlas::ui
