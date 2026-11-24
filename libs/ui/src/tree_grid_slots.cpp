/** @file @brief Sparse grid placement lifetime and checked owner-thread edits. */
#include "grid_validation.hpp"
#include "tree_state.hpp"

namespace atlas::ui
{
/// @brief Reject invalid parent/span before publishing metadata or advancing revisions.
void element_tree::set_grid_slot(element_handle target, const grid_slot& slot)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"ui.tree.set_grid_slot", "ui.cpu"}};
    const auto index = impl_->require(target);
    const auto parent = impl_->at(index).parent;
    const auto* grid =
        parent != impl::none ? std::get_if<grid_layout>(&impl_->at(parent).panel) : nullptr;
    if (!grid)
    {
        throw std::invalid_argument("grid slot requires a grid parent");
    }

    detail::validate_grid_slot(slot, grid->definition());
    const auto* current = impl_->registry.try_get<grid_slot>(impl_->slots[index].entity);
    if ((current ? *current : grid_slot{}) == slot)
    {
        return;
    }

    impl_->ensure_layout_revision();
    if (slot == grid_slot{})
    {
        impl_->registry.remove<grid_slot>(impl_->slots[index].entity);
    }
    else
    {
        impl_->registry.emplace_or_replace<grid_slot>(impl_->slots[index].entity, slot);
    }

    impl_->subtree(index, true);
    impl_->ancestors(parent);
    ++impl_->layout_revision;
}

/// @brief Default placement removes the sparse override through the same validation path.
void element_tree::reset_grid_slot(element_handle target)
{
    set_grid_slot(target, {});
}

/// @brief Copy relationship metadata without exposing mutable registry state.
std::optional<grid_slot> element_tree::grid_slot_for(element_handle target) const
{
    impl_->check();
    const auto index = impl_->require(target);
    const auto parent = impl_->at(index).parent;
    if (parent == impl::none || !std::holds_alternative<grid_layout>(impl_->at(parent).panel))
    {
        return std::nullopt;
    }

    const auto* slot = impl_->registry.try_get<grid_slot>(impl_->slots[index].entity);
    return slot ? *slot : grid_slot{};
}
} // namespace atlas::ui
