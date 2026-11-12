/** @file @brief Atlas canvas controls, clipped runtime preview and shaped panel labels. */
#include "panel_scene.hpp"
#include <algorithm>
#include <utility>

namespace atlas::editor
{
namespace
{
/// @brief Use Atlas pointer capture and enqueue one command on a release inside the control.
class click_handler final : public ui::input_handler
{
  public:
    /// @brief Retain stable authored identity instead of a preview element handle.
    click_handler(control_action action, std::vector<control_action>& output)
        : action_{std::move(action)}, output_{output}
    {
    }

    /// @brief Release capture automatically through the existing input session.
    ui::event_response on_event(const ui::routed_event& event, ui::command_buffer&) override
    {
        if (event.kind == ui::event_kind::capture_lost ||
            event.kind == ui::event_kind::pointer_cancel)
        {
            pressed_ = false;
        }

        if (event.phase != ui::route_phase::target)
        {
            return {};
        }

        if (event.kind == ui::event_kind::pointer_down)
        {
            pressed_ = true;
            return {.stop = true, .capture_pointer = true};
        }

        if (event.kind == ui::event_kind::pointer_up && std::exchange(pressed_, false) &&
            event.local.x >= 0 && event.local.y >= 0 &&
            event.local.x < event.current_bounds.width &&
            event.local.y < event.current_bounds.height)
        {
            output_.push_back(action_);
            return {.stop = true};
        }

        return {};
    }

  private:
    control_action action_;               ///< Immutable click intent.
    std::vector<control_action>& output_; ///< Owned by the panel scene.
    bool pressed_ = false;                ///< Reset on every matching release.
};

/// @brief Materialize a logical rectangle as a normal Atlas canvas leaf.
ui::element_handle make_element(ui::element_tree& tree, ui::element_handle parent,
                                const control& item)
{
    const auto node = tree.create(parent);
    tree.set_panel(node, ui::leaf_layout{});
    ui::layout_style style;
    style.constraints.minimum = {item.bounds.width, item.bounds.height};
    style.constraints.maximum = style.constraints.minimum;
    tree.set_layout(node, style);
    ui::canvas_slot slot;
    slot.horizontal.offset_min = item.bounds.x;
    slot.vertical.offset_min = item.bounds.y;
    tree.set_canvas_slot(node, slot);
    ui::paint_style appearance;
    appearance.fill = item.color[3] > 0;
    appearance.color = item.color;
    appearance.layer = item.action.kind == control_kind::none ? 10 : 0;
    tree.set_paint(node, appearance);
    return node;
}

/// @brief Append one label per visible control while preserving its logical clip.
void append_labels(editor_frame& packet, const panel_plan& plan, float scale,
                   const diagnostics::services& sinks)
{
    std::vector<ui_text::text_layout> labels;
    labels.reserve(plan.controls.size());
    for (const auto& item : plan.controls)
    {
        labels.push_back(ui_text::shape({.text = item.label,
                                         .size = 14,
                                         .width = std::max(1.f, item.bounds.width - 12),
                                         .dpi = 96.f * scale,
                                         .wrap = false},
                                        sinks));
    }

    packet.atlas =
        std::make_shared<const ui_text::glyph_atlas>(ui_text::glyph_atlas::pack(labels, {}, sinks));
    for (std::size_t i = 0; i < plan.controls.size(); ++i)
    {
        const auto& bounds = plan.controls[i].bounds;
        const auto glyphs = ui_text::glyph_quads(
            *packet.atlas, {.label = i, .origin = {bounds.x + 6, bounds.y + 3}, .clip = bounds});
        packet.quads.insert(packet.quads.end(), glyphs.begin(), glyphs.end());
    }
}
} // namespace

/// @brief Build independent layout snapshots for authoring chrome and its preview.
panel_scene::panel_scene(const panel_plan& plan, ui::element_tree& preview,
                         ui::layout_rect viewport, float scale, ui::layout_engine& layout,
                         ui::paint_engine& paint, const diagnostics::services& sinks)
    : tree_{{.max_elements = static_cast<std::uint32_t>(plan.controls.size() + 1)}, {}, sinks},
      input_{tree_, {.max_events = 4096}, {}, sinks}, scale_{scale}
{
    const auto root = tree_.create();
    tree_.set_panel(root, ui::canvas_layout{});
    for (const auto& item : plan.controls)
    {
        const auto node = make_element(tree_, root, item);
        if (item.action.kind != control_kind::none)
        {
            input_.bind(node, std::make_shared<click_handler>(item.action, pending_));
        }
    }

    tree_.set_viewport(viewport);
    source_ = tree_.snapshot();
    geometry_ = layout.calculate(source_);
    const auto chrome = paint.calculate(source_, geometry_);
    const auto preview_geometry = layout.update(preview, plan.preview);
    const auto displayed = paint.calculate(preview.snapshot(), preview_geometry);
    auto result = std::make_shared<editor_frame>();
    result->viewport = viewport;
    result->quads.assign(chrome.quads().begin(), chrome.quads().end());
    result->quads.insert(result->quads.end(), displayed.quads().begin(), displayed.quads().end());
    append_labels(*result, plan, scale_, sinks);
    packet_ = std::move(result);
}

/// @brief Borrow the retained main/render handoff payload.
const std::shared_ptr<const editor_frame>& panel_scene::packet() const noexcept
{
    return packet_;
}

/// @brief Dispatch a batch in order and execute actions only after leaving callbacks.
std::vector<control_action> panel_scene::dispatch(std::span<const input::event> events)
{
    input_bridge_.clear();
    for (auto event : events)
    {
        if (event.device.kind == input::device_kind::pointer)
        {
            event.position.x /= scale_;
            event.position.y /= scale_;
            if (event.kind == input::event_kind::pointer_motion)
            {
                event.value.x /= scale_;
                event.value.y /= scale_;
            }
        }

        input_bridge_.on_input(event);
    }

    input_.begin_frame(++frame_, source_, geometry_);
    input_.process(input_bridge_.values());
    input_.tick();
    input_.late_tick();
    input_.end_frame();
    tree_.apply(tree_.commands().seal());
    return std::exchange(pending_, {});
}
} // namespace atlas::editor
