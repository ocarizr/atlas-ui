/** @file @brief Sparse validated scrollbar links stay outside the controlled content subtree. */
#include "tree_state.hpp"

namespace atlas::ui
{
/// @brief Validate all values and ancestry before allocating or replacing a sparse component.
void element_tree::set_scrollbar(element_handle target, const scrollbar_style& style)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.scrollbar", "ui.cpu"}};
    const auto index = impl_->require(target);
    const auto viewport = impl_->require(style.viewport);
    if (!std::holds_alternative<scroll_layout>(impl_->at(viewport).panel))
    {
        throw std::invalid_argument("scrollbar requires a scroll viewport");
    }

    for (auto current = index; current != impl::none; current = impl_->at(current).parent)
    {
        if (current == viewport)
        {
            throw std::invalid_argument("scrollbar chrome must be outside its viewport subtree");
        }
    }

    for (auto current = viewport; current != impl::none; current = impl_->at(current).parent)
    {
        if (current == index)
        {
            throw std::invalid_argument("scrollbar chrome cannot contain its viewport");
        }
    }

    (void)resolve_scrollbar({}, {}, style);
    const auto entity = impl_->slots[index].entity;
    const auto* before = impl_->registry.try_get<scrollbar_style>(entity);
    if (before && *before == style)
    {
        return;
    }

    impl_->ensure_layout_revision();
    impl_->registry.emplace_or_replace<scrollbar_style>(entity, style);
    impl_->subtree(index, true);
    ++impl_->layout_revision;
}

/// @brief Reset preserves ordinary element appearance and handler registrations.
void element_tree::reset_scrollbar(element_handle target)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.scrollbar", "ui.cpu"}};
    const auto index = impl_->require(target);
    if (!impl_->registry.all_of<scrollbar_style>(impl_->slots[index].entity))
    {
        return;
    }

    impl_->ensure_layout_revision();
    impl_->registry.remove<scrollbar_style>(impl_->slots[index].entity);
    impl_->subtree(index, true);
    ++impl_->layout_revision;
}
} // namespace atlas::ui
