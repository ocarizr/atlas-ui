/** @file @brief Sparse stack ordering/weight metadata follows the parent-child relationship. */
#include "stack_validation.hpp"
#include "tree_state.hpp"

namespace atlas::ui
{
/// @brief Default metadata does not require a component on every stack child.
stack_slot element_tree::impl::child_stack_slot(std::uint32_t index) const
{
    const auto* value = registry.try_get<stack_slot>(slots[index].entity);
    return value ? *value : stack_slot{};
}

/// @brief Reject invalid parent/weights before publication or revision changes.
void element_tree::set_stack_slot(element_handle target, const stack_slot& slot)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"ui.tree.set_stack_slot", "ui.cpu"}};
    const auto index = impl_->require(target);
    const auto parent = impl_->at(index).parent;
    if (parent == impl::none || !detail::stack_panel(impl_->at(parent).panel))
    {
        throw std::invalid_argument("stack slot requires a horizontal or vertical parent");
    }

    detail::validate_stack_slot(slot);
    if (impl_->child_stack_slot(index) == slot)
    {
        return;
    }

    impl_->ensure_layout_revision();
    if (slot == stack_slot{})
    {
        impl_->registry.remove<stack_slot>(impl_->slots[index].entity);
    }
    else
    {
        impl_->registry.emplace_or_replace<stack_slot>(impl_->slots[index].entity, slot);
    }

    impl_->subtree(index, true);
    impl_->ancestors(parent);
    ++impl_->layout_revision;
}

/// @brief A reset uses the same validation and removes the nondefault sparse record.
void element_tree::reset_stack_slot(element_handle target)
{
    set_stack_slot(target, {});
}

/// @brief Copy the relationship metadata; never expose mutable registry references.
std::optional<stack_slot> element_tree::stack_slot_for(element_handle target) const
{
    impl_->check();
    const auto index = impl_->require(target);
    const auto parent = impl_->at(index).parent;
    return parent != impl::none && detail::stack_panel(impl_->at(parent).panel)
               ? std::optional{impl_->child_stack_slot(index)}
               : std::nullopt;
}
} // namespace atlas::ui
