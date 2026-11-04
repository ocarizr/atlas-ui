/** @file @brief Sparse paint state changes invalidate descendants without changing geometry. */
#include "tree_state.hpp"
#include <algorithm>
#include <cmath>

namespace atlas::ui
{
/// @brief Invalid input and component allocation failure leave existing appearance untouched.
void element_tree::set_paint(element_handle target, const paint_style& style)
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.tree.set_paint", "ui.cpu"}};
    const auto index = impl_->require(target);
    const auto valid = [](float value)
    {
        return std::isfinite(value) && value >= 0 && value <= 1;
    };
    if (!std::all_of(style.color.begin(), style.color.end(), valid) || !valid(style.uv.x) ||
        !valid(style.uv.y) || !valid(style.uv.width) || !valid(style.uv.height) ||
        style.uv.x + style.uv.width > 1 || style.uv.y + style.uv.height > 1 ||
        !std::isfinite(style.translation.x) || !std::isfinite(style.translation.y))
    {
        throw std::invalid_argument("invalid UI paint style");
    }

    const auto entity = impl_->slots[index].entity;
    const auto* previous = impl_->registry.try_get<paint_style>(entity);
    if ((previous ? *previous : paint_style{}) == style)
    {
        return;
    }

    if (style == paint_style{})
    {
        impl_->registry.remove<paint_style>(entity);
    }
    else
    {
        impl_->registry.emplace_or_replace<paint_style>(entity, style);
    }

    impl_->subtree(index, false);
}

/// @brief Registry storage never escapes to callers or worker jobs.
paint_style element_tree::paint(element_handle target) const
{
    impl_->check();
    const auto index = impl_->require(target);
    const auto* value = impl_->registry.try_get<paint_style>(impl_->slots[index].entity);
    return value ? *value : paint_style{};
}
} // namespace atlas::ui
