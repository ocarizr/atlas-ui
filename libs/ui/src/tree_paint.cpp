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
    if (style.image)
    {
        (void)fit_image({}, *style.image, style.uv);
    }

    const auto valid = [](float value)
    {
        return std::isfinite(value) && value >= 0 && value <= 1;
    };
    if (!std::all_of(style.color.begin(), style.color.end(), valid) || !valid(style.uv.x) ||
        !valid(style.uv.y) || !valid(style.uv.width) || !valid(style.uv.height) ||
        style.uv.x + style.uv.width > 1 || style.uv.y + style.uv.height > 1 ||
        !std::isfinite(style.translation.x) || !std::isfinite(style.translation.y) ||
        !valid_scalar(property::scale_x, style.scale.x) ||
        !valid_scalar(property::scale_y, style.scale.y) ||
        !valid_scalar(property::rotate, style.rotation))
    {
        throw std::invalid_argument("invalid UI paint style");
    }

    const auto entity = impl_->slots[index].entity;
    auto& node = impl_->at(index);
    auto appearance = style;
    appearance.translation = {};
    appearance.scale = {1, 1};
    appearance.rotation = 0;
    const auto* previous = impl_->registry.try_get<paint_style>(entity);
    const bool changed_appearance = (previous ? *previous : paint_style{}) != appearance;
    if (!changed_appearance && node.translation == style.translation && node.scale == style.scale &&
        node.rotation == style.rotation)
    {
        return;
    }

    if (changed_appearance)
    {
        if (appearance == paint_style{})
        {
            impl_->registry.remove<paint_style>(entity);
        }
        else
        {
            impl_->registry.emplace_or_replace<paint_style>(entity, appearance);
        }
    }

    node.translation = style.translation;
    node.scale = style.scale;
    node.rotation = style.rotation;
    impl_->subtree(index, false);
}

/// @brief Registry storage never escapes to callers or worker jobs.
paint_style element_tree::paint(element_handle target) const
{
    impl_->check();
    const auto index = impl_->require(target);
    const auto* value = impl_->registry.try_get<paint_style>(impl_->slots[index].entity);
    auto style = value ? *value : paint_style{};
    const auto& node = impl_->at(index);
    style.translation = node.translation;
    style.scale = node.scale;
    style.rotation = node.rotation;
    return style;
}
} // namespace atlas::ui
