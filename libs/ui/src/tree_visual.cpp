/** @file @brief General paint-only origins with checked pure resolution into layout coordinates. */
#include "canvas_validation.hpp"
#include "layout_validation.hpp"
#include "tree_state.hpp"

namespace atlas::ui
{
/// @brief No default component is required for elements that use the centered visual origin.
visual_style element_tree::impl::child_visual(std::uint32_t index) const
{
    const auto* value = registry.try_get<visual_style>(slots[index].entity);
    return value ? *value : visual_style{};
}

/// @brief Origin changes affect painting, never untransformed desired size or layout revision.
void element_tree::set_visual(element_handle target, visual_style visual)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.set_visual", "ui.cpu"}};
    const auto index = impl_->require(target);
    detail::validate_visual(visual);
    if (impl_->child_visual(index) == visual)
    {
        return;
    }

    if (visual == visual_style{})
    {
        impl_->registry.remove<visual_style>(impl_->slots[index].entity);
    }
    else
    {
        impl_->registry.emplace_or_replace<visual_style>(impl_->slots[index].entity, visual);
    }

    impl_->subtree(index, false);
}

/// @brief Metadata can be consumed by future paint/input without borrowing mutable components.
visual_style element_tree::visual(element_handle target) const
{
    impl_->check();
    return impl_->child_visual(impl_->require(target));
}

/// @brief This resolves metadata only; it applies no rotation, scale or layout-bound change.
layout_point resolve_transform_origin(layout_rect bounds, visual_style visual)
{
    detail::validate_viewport(bounds);
    detail::validate_visual(visual);
    return {detail::layout_float(static_cast<double>(bounds.x) +
                                 bounds.width * static_cast<double>(visual.origin.x)),
            detail::layout_float(static_cast<double>(bounds.y) +
                                 bounds.height * static_cast<double>(visual.origin.y))};
}
} // namespace atlas::ui
