/** @file @brief Atomic policy changes validate arity and existing child slots before mutation. */
#include "canvas_validation.hpp"
#include "stack_validation.hpp"
#include "tree_state.hpp"

namespace atlas::ui
{
/// @brief Sparse override lookup never allocates a default canvas slot.
canvas_slot element_tree::impl::child_slot(std::uint32_t index) const
{
    const auto* slot = registry.try_get<canvas_slot>(slots[index].entity);
    return slot ? *slot : canvas_slot{};
}

/// @brief Every conversion is checked before clearing incompatible parent-owned metadata.
void element_tree::set_panel(element_handle target, const panel_layout& panel)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.set_panel", "ui.cpu"}};
    const auto index = impl_->require(target);
    detail::validate_panel(panel);
    auto& node = impl_->at(index);
    if ((std::holds_alternative<leaf_layout>(panel) && node.first != impl::none) ||
        (std::holds_alternative<size_box_layout>(panel) && node.first != impl::none &&
         impl_->at(node.first).next != impl::none))
    {
        throw std::invalid_argument("UI panel is incompatible with existing children");
    }

    if (const auto* canvas = std::get_if<canvas_layout>(&panel))
    {
        for (auto child = node.first; child != impl::none; child = impl_->at(child).next)
        {
            detail::validate_slot(impl_->child_slot(child), *canvas);
        }
    }

    if (node.panel == panel)
    {
        return;
    }

    impl_->ensure_layout_revision();
    node.panel = panel;
    if (!std::holds_alternative<canvas_layout>(panel))
    {
        for (auto child = node.first; child != impl::none; child = impl_->at(child).next)
        {
            impl_->registry.remove<canvas_slot>(impl_->slots[child].entity);
        }
    }

    if (!detail::stack_panel(panel))
    {
        for (auto child = node.first; child != impl::none; child = impl_->at(child).next)
        {
            impl_->registry.remove<stack_slot>(impl_->slots[child].entity);
        }
    }

    impl_->subtree(index, true);
    impl_->ancestors(node.parent);
    ++impl_->layout_revision;
}

/// @brief Copying a small policy never exposes registry references.
panel_layout element_tree::panel(element_handle target) const
{
    impl_->check();
    return impl_->at(impl_->require(target)).panel;
}
} // namespace atlas::ui
