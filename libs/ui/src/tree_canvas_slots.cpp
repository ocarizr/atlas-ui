/** @file @brief Sparse overrides exist only on children whose current parent is a canvas. */
#include "boundary_validation.hpp"
#include "canvas_validation.hpp"
#include "tree_state.hpp"

namespace atlas::ui
{
/// @brief Fail before mutation for roots, noncanvas parents or invalid slot policies.
void element_tree::set_canvas_slot(element_handle target, const canvas_slot& slot)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"ui.tree.set_canvas_slot", "ui.cpu"}};
    const auto index = impl_->require(target);
    const auto parent = impl_->at(index).parent;
    const auto* canvas =
        parent == impl::none ? nullptr : std::get_if<canvas_layout>(&impl_->at(parent).panel);
    if (!canvas)
    {
        throw std::invalid_argument("canvas slot requires a canvas parent");
    }

    detail::validate_slot(slot, *canvas);
    detail::validate_boundary_values(impl_->child_boundaries(index), slot);
    if (impl_->child_slot(index) == slot)
    {
        return;
    }

    impl_->ensure_layout_revision();
    if (slot == canvas_slot{})
    {
        impl_->registry.remove<canvas_slot>(impl_->slots[index].entity);
    }
    else
    {
        impl_->registry.emplace_or_replace<canvas_slot>(impl_->slots[index].entity, slot);
    }

    impl_->subtree(index, true);
    impl_->ancestors(parent);
    ++impl_->layout_revision;
}

/// @brief Reset is an ordinary validated default-slot assignment, retaining strong failure
/// behavior.
void element_tree::reset_canvas_slot(element_handle target)
{
    set_canvas_slot(target, {});
}

/// @brief Default canvas placement is synthesized instead of stored on every child.
std::optional<canvas_slot> element_tree::canvas_slot_for(element_handle target) const
{
    impl_->check();
    const auto index = impl_->require(target);
    const auto parent = impl_->at(index).parent;
    return parent != impl::none && std::holds_alternative<canvas_layout>(impl_->at(parent).panel)
               ? std::optional{impl_->child_slot(index)}
               : std::nullopt;
}
} // namespace atlas::ui
