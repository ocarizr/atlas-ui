/** @file @brief Validated layout input mutation and all-or-nothing computed-state publication. */
#include "layout_validation.hpp"
#include "tree_state.hpp"

namespace atlas::ui
{
/// @brief Never wrap a snapshot revision into a previously published input identity.
void element_tree::impl::ensure_layout_revision() const
{
    if (layout_revision == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("UI layout revisions exhausted");
    }
}

/// @brief Leaf and single-child container invariants hold for creation and reparenting.
void element_tree::impl::validate_parent(std::uint32_t parent) const
{
    if (parent == none)
    {
        return;
    }

    const auto& value = at(parent);
    if (value.layout.kind == layout_kind::leaf ||
        (value.layout.kind == layout_kind::size_box && value.first != none))
    {
        throw std::invalid_argument("UI layout container child capacity exceeded");
    }
}

/// @brief Copying scalar style values cannot fail after the validated commit boundary.
void element_tree::set_layout(element_handle target, const layout_style& style)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.set_layout", "ui.cpu"}};
    const auto index = impl_->require(target);
    detail::validate_style(style);
    auto& value = impl_->at(index);
    if ((style.kind == layout_kind::leaf && value.first != impl::none) ||
        (style.kind == layout_kind::size_box && value.first != impl::none &&
         impl_->at(value.first).next != impl::none))
    {
        throw std::invalid_argument("UI layout style is incompatible with existing children");
    }

    if (style == value.layout)
    {
        return;
    }

    impl_->ensure_layout_revision();
    value.layout = style;
    impl_->subtree(index, true);
    impl_->ancestors(value.parent);
    ++impl_->layout_revision;
}

/// @brief Return requested parameters without exposing mutable components.
layout_style element_tree::layout(element_handle target) const
{
    impl_->check();
    return impl_->at(impl_->require(target)).layout;
}

/// @brief Viewport changes invalidate every root, including alignment-only changes.
void element_tree::set_viewport(layout_rect viewport)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.viewport", "ui.cpu"}};
    detail::validate_viewport(viewport);
    if (viewport == impl_->viewport)
    {
        return;
    }

    impl_->ensure_layout_revision();
    for (std::uint32_t index = 0; index < impl_->slots.size(); ++index)
    {
        if (impl_->slots[index].entity == entt::null)
        {
            continue;
        }

        auto& node = impl_->at(index);
        node.computed_valid = false;
        node.dirty = {true, true};
    }

    impl_->viewport = viewport;
    ++impl_->layout_revision;
}

/// @brief Validate the entire result before writing or acknowledging any computed state.
void element_tree::publish_layout(const layout_snapshot& result)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"ui.tree.publish_layout", "ui.cpu"}};
    if (result.session() != impl_->session || result.revision() != impl_->layout_revision ||
        result.viewport() != impl_->viewport || result.items().size() != impl_->count)
    {
        throw std::invalid_argument("stale or foreign UI layout result");
    }

    for (const auto& item : result.items())
    {
        (void)impl_->require(item.target);
    }

    for (const auto& item : result.items())
    {
        auto& node = impl_->at(item.target.index);
        if (!node.computed || *node.computed != item.geometry)
        {
            node.dirty.paint = true;
        }

        node.computed = item.geometry;
        node.computed_valid = true;
        node.dirty.layout = false;
    }
}

/// @brief Acknowledging dirty flags cannot expose invalidated numeric geometry.
std::optional<layout_geometry> element_tree::computed(element_handle target) const
{
    impl_->check();
    const auto& node = impl_->at(impl_->require(target));
    for (auto index = target.index; index != impl::none; index = impl_->at(index).parent)
    {
        if (!impl_->at(index).computed_valid)
        {
            return std::nullopt;
        }
    }

    return node.computed;
}
} // namespace atlas::ui
