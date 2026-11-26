/** @file @brief Capture semantic values from the exact frozen input/paint presentation. */
#include "input_state.hpp"
#include <cmath>

namespace atlas::ui
{
namespace
{
/// @brief Convert immutable presentation metadata into an owned semantic value.
accessibility::node make_accessible_node(const detail::presentation_region& region,
                                         accessibility::identity parent,
                                         const accessible_element& metadata,
                                         accessibility_view view, const accessible_text& resolve)
{
    const auto text = [&](const std::string& literal, const std::string& key)
    {
        if (key.empty())
        {
            return literal;
        }

        if (!resolve)
        {
            throw std::invalid_argument("accessible localization requires a resolver");
        }

        return resolve(key);
    };
    const auto& box = region.bounds;
    const auto& clip = region.clip;
    accessibility::node node{{region.target.index, region.target.generation, region.target.session},
                             parent,
                             metadata.role,
                             text(metadata.name, metadata.name_key),
                             text(metadata.description, metadata.description_key),
                             metadata.value};
    node.bounds = {view.x + box.x * view.scale, view.y + box.y * view.scale, box.width * view.scale,
                   box.height * view.scale};
    node.offscreen = box.width <= 0 || box.height <= 0 || clip.width <= 0 || clip.height <= 0 ||
                     box.x >= clip.x + clip.width || box.y >= clip.y + clip.height ||
                     box.x + box.width <= clip.x || box.y + box.height <= clip.y;
    node.selected = metadata.selected;
    node.checked = metadata.checked;
    node.number = metadata.number;
    node.minimum = metadata.minimum;
    node.maximum = metadata.maximum;
    node.clip = {view.x + clip.x * view.scale, view.y + clip.y * view.scale,
                 clip.width * view.scale, clip.height * view.scale};
    node.clipped = true;
    node.reveal = true;
    return node;
}
} // namespace

/// @brief Read only on the UI owner; native queries later consume the resulting owned values.
std::vector<accessibility::node>
input_session::accessibility_nodes(accessibility_view view, const accessible_text& resolve) const
{
    impl_->open();
    if (!std::isfinite(view.x) || !std::isfinite(view.y) || !std::isfinite(view.scale) ||
        view.scale <= 0 || (view.modal_root.generation && !impl_->tree.contains(view.modal_root)))
    {
        throw std::invalid_argument("invalid accessibility screen transform or modal root");
    }

    const impl::guard guard{impl_->busy};
    const diagnostics::operation_scope trace{impl_->destinations,
                                             {"ui.accessibility.capture", "ui.cpu"}};
    std::vector<accessibility::node> output;
    output.reserve(impl_->accessible.size());
    for (const auto& region : impl_->regions)
    {
        const auto* descriptor = impl_->accessibility_for(region.target);
        if (!descriptor)
        {
            continue;
        }

        bool visible = true;
        bool in_scope = !view.modal_root.generation;
        bool outside_modal = false;
        accessibility::identity inherited{};
        auto ancestor = region.target;
        for (std::size_t depth = 0; ancestor.generation; ++depth)
        {
            const auto* captured = impl_->region(ancestor);
            if (depth >= impl_->limits.max_route_depth || !captured || !captured->visible ||
                !impl_->tree.contains(ancestor) || !impl_->tree.requested(ancestor).visible ||
                impl_->tree.parent(ancestor) != captured->parent)
            {
                visible = false;
                break;
            }

            if (ancestor != region.target && !inherited.generation && !outside_modal &&
                impl_->accessibility_for(ancestor))
            {
                inherited = {ancestor.index, ancestor.generation, ancestor.session};
            }

            in_scope |= ancestor == view.modal_root;
            outside_modal |= view.modal_root.generation && ancestor == view.modal_root;
            ancestor = captured->parent;
        }

        if (!visible || !in_scope)
        {
            continue;
        }

        const auto& metadata = *descriptor;
        auto node = make_accessible_node(region, inherited, metadata, view, resolve);
        node.enabled = impl_->eligible(region.target);
        const auto* binding = impl_->find(region.target);
        node.focusable = binding && binding->options.focusable;
        node.focused = node.enabled && impl_->focus_target == region.target;
        node.invoke = binding && !binding->retiring && !binding->ended &&
                      (metadata.invoke_action != 0 || metadata.role == accessibility::role::button);
        node.select =
            binding && !binding->retiring && !binding->ended && metadata.select_action != 0;
        output.push_back(std::move(node));
    }

    accessibility::validate(output, {.max_nodes = impl_->limits.max_nodes});
    return output;
}
} // namespace atlas::ui
