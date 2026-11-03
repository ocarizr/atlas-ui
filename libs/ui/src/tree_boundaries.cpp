/** @file @brief Owner-guarded sparse relationship authoring and bounded cycle preflight. */
#include "boundary_validation.hpp"
#include "tree_state.hpp"

namespace atlas::ui
{
/// @brief Empty dependency metadata is implicit for every element.
layout_boundaries element_tree::impl::child_boundaries(std::uint32_t index) const
{
    const auto* value = registry.try_get<layout_boundaries>(slots[index].entity);
    return value ? *value : layout_boundaries{};
}

/// @brief Walk source dependencies, not hierarchy descendants, using bounded owner scratch.
void element_tree::impl::validate_boundaries(std::uint32_t index, const layout_boundaries& value)
{
    detail::validate_boundary_values(value, child_slot(index));
    if (value == layout_boundaries{})
    {
        return;
    }

    const auto parent = at(index).parent;
    const auto* canvas = parent == none ? nullptr : std::get_if<canvas_layout>(&at(parent).panel);
    if (!canvas || canvas->measure != canvas_measure::intrinsic)
    {
        throw std::invalid_argument("UI boundaries require an intrinsic canvas parent");
    }

    std::pmr::vector<unsigned char> visited(slots.size(), 0, memory.get());
    work.clear();
    const auto append = [&](const std::optional<boundary_relation>& relation)
    {
        if (!relation)
        {
            return;
        }

        const auto source = require(relation->source);
        if (source == index)
        {
            throw std::invalid_argument("UI boundary dependency cycle");
        }

        if (at(source).parent != parent)
        {
            throw std::invalid_argument("UI boundary source must be a sibling in the same canvas");
        }

        if (!visited[source])
        {
            visited[source] = 1;
            work.push_back(source);
        }
    };
    append(value.horizontal);
    append(value.vertical);
    for (std::size_t cursor = 0; cursor < work.size(); ++cursor)
    {
        const auto dependencies = child_boundaries(work[cursor]);
        append(dependencies.horizontal);
        append(dependencies.vertical);
    }
}

/// @brief Validation/allocation precede revision publication; identical writes remain no-ops.
void element_tree::set_boundaries(element_handle target, const layout_boundaries& value)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"ui.tree.set_boundaries", "ui.cpu"}};
    const auto index = impl_->require(target);
    if (value == impl_->child_boundaries(index))
    {
        return;
    }

    impl_->validate_boundaries(index, value);
    impl_->ensure_layout_revision();
    if (value == layout_boundaries{})
    {
        impl_->registry.remove<layout_boundaries>(impl_->slots[index].entity);
    }
    else
    {
        impl_->registry.emplace_or_replace<layout_boundaries>(impl_->slots[index].entity, value);
    }

    impl_->subtree(index, true);
    impl_->ancestors(impl_->at(index).parent);
    ++impl_->layout_revision;
}

/// @brief Reset is allowed for any live element, including one without a canvas parent.
void element_tree::reset_boundaries(element_handle target)
{
    set_boundaries(target, {});
}

/// @brief Return a copy without exposing the registry or mutable source ownership.
layout_boundaries element_tree::boundaries(element_handle target) const
{
    impl_->check();
    return impl_->child_boundaries(impl_->require(target));
}
} // namespace atlas::ui
