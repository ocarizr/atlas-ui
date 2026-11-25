/** @file @brief Sparse owner semantic configuration and revalidated action delivery. */
#include "input_state.hpp"

namespace atlas::ui
{
/// @brief Reuse the independent semantic validator for bounded UTF-8 and role checking.
void validate_accessibility(const accessible_element& source)
{
    for (const auto* text : {&source.name, &source.description, &source.name_key,
                             &source.description_key, &source.value})
    {
        if (text->size() > 65536)
        {
            throw std::length_error("accessible descriptor text exceeds prepared content limit");
        }
    }

    accessibility::node value{{0, 1, 1},          {},          source.role, source.name,
                              source.description, source.value};
    value.number = source.number;
    value.minimum = source.minimum;
    value.maximum = source.maximum;
    value.invoke = source.invoke_action != 0;
    value.select = source.select_action != 0;
    if ((source.role == accessibility::role::tab && !source.select_action) ||
        (source.role == accessibility::role::checkbox && !source.invoke_action))
    {
        throw std::invalid_argument("accessible tab or checkbox requires its semantic action");
    }

    const std::array values{value};
    accessibility::validate(values, {});
    for (const auto& key : {source.name_key, source.description_key})
    {
        const std::array keys{accessibility::node{{0, 1, 1}, {}, accessibility::role::text, key}};
        accessibility::validate(keys, {});
    }

    if ((!source.name.empty() && !source.name_key.empty()) ||
        (!source.description.empty() && !source.description_key.empty()))
    {
        throw std::invalid_argument("accessible text must use a literal or a localization key");
    }
}

/// @brief Configuration is atomic and does not require decorative containers to bind handlers.
void input_session::configure_accessibility(element_handle target, accessible_element value)
{
    impl_->idle();
    if (!impl_->tree.contains(target))
    {
        throw std::invalid_argument("invalid accessibility target");
    }

    validate_accessibility(value);
    if ((value.invoke_action || value.select_action) && value.name.empty() &&
        value.name_key.empty())
    {
        diagnostics::write(impl_->destinations, diagnostics::log_level::warning, "ui.accessibility",
                           "interactive element has no accessible name");
    }

    std::erase_if(impl_->accessible,
                  [&](const auto& entry)
                  {
                      return !impl_->tree.contains(entry.first);
                  });
    const auto found = std::ranges::lower_bound(impl_->accessible, target.index, {},
                                                [](const auto& entry)
                                                {
                                                    return entry.first.index;
                                                });
    if (found != impl_->accessible.end() && found->first == target)
    {
        found->second = std::move(value);
        return;
    }

    if (impl_->accessible.size() == impl_->limits.max_nodes)
    {
        throw std::length_error("UI accessibility metadata limit exhausted");
    }

    impl_->accessible.insert(found, std::pair{target, std::move(value)});
}

/// @brief Sorted sparse metadata needs no map allocation in the input/capture hot paths.
const accessible_element* input_session::impl::accessibility_for(element_handle target) const
{
    const auto found = std::ranges::lower_bound(accessible, target.index, {},
                                                [](const auto& entry)
                                                {
                                                    return entry.first.index;
                                                });
    return found != accessible.end() && found->first == target ? &found->second : nullptr;
}

/// @brief Omitting a container does not remove its meaningful descendants.
void input_session::reset_accessibility(element_handle target)
{
    impl_->idle();
    std::erase_if(impl_->accessible,
                  [&](const auto& entry)
                  {
                      return entry.first == target;
                  });
}

/// @brief Execution is serial, does not manufacture pointer packets, and rechecks live state.
void input_session::process_accessibility(std::span<const accessibility::request> requests,
                                          element_handle modal_root)
{
    impl_->open();
    if (modal_root.generation && !impl_->tree.contains(modal_root))
    {
        throw std::invalid_argument("invalid accessibility modal root");
    }

    if (impl_->stage > 2 || requests.size() > impl_->limits.max_callbacks)
    {
        throw std::invalid_argument("accessibility requests require the bounded input phase");
    }

    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope trace{impl_->destinations,
                                             {"ui.accessibility.actions", "ui.dispatch"}};
    try
    {
        for (const auto& request : requests)
        {
            const element_handle target{request.target.index, request.target.generation,
                                        request.target.session};
            const auto* descriptor = impl_->accessibility_for(target);
            if (!descriptor || !impl_->eligible(target))
            {
                continue;
            }

            if (modal_root.generation)
            {
                auto parent = target;
                while (parent.generation && parent != modal_root)
                {
                    parent = impl_->tree.parent(parent);
                }

                if (!parent.generation)
                {
                    continue;
                }
            }

            const auto& metadata = *descriptor;
            if (request.operation == accessibility::action::focus && impl_->focus_eligible(target))
            {
                impl_->set_focus(target);
            }
            else if (request.operation == accessibility::action::reveal)
            {
                impl_->reveal(target);
            }
            else
            {
                const auto id =
                    request.operation == accessibility::action::invoke   ? metadata.invoke_action
                    : request.operation == accessibility::action::select ? metadata.select_action
                                                                         : 0;
                if (!id && request.operation == accessibility::action::invoke &&
                    metadata.role == accessibility::role::button)
                {
                    (void)impl_->route({}, target, event_kind::activate);
                }

                if (id)
                {
                    input_event raw{.kind = input_kind::action};
                    raw.action =
                        input::action_event{.action = id, .phase = input::action_phase::performed};
                    (void)impl_->route(raw, target, event_kind::action);
                }
            }

            impl_->reconcile();
        }
    }
    catch (...)
    {
        impl_->fail();
        throw;
    }
}
} // namespace atlas::ui
