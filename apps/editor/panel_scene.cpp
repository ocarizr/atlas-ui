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
    click_handler(control_action action, std::vector<control_action>& output,
                  std::function<bool(const ui::drag_event&)> accepts = {})
        : action_{std::move(action)}, output_{output}, accepts_{std::move(accepts)}
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

        if (event.kind == ui::event_kind::drag_query && event.drag && accepts_)
        {
            return {.acceptance = accepts_(*event.drag) ? ui::drag_acceptance::accept
                                                        : ui::drag_acceptance::reject};
        }

        if (event.kind == ui::event_kind::activate)
        {
            output_.push_back(action_);
            return {.stop = true};
        }

        if (event.phase != ui::route_phase::target)
        {
            return {};
        }

        if (event.kind == ui::event_kind::pointer_down)
        {
            pressed_ = true;
            return {.stop = true, .capture_pointer = true, .focus = true};
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
    control_action action_;                              ///< Immutable click intent.
    std::vector<control_action>& output_;                ///< Owned by the panel scene.
    std::function<bool(const ui::drag_event&)> accepts_; ///< Pure editor transaction preflight.
    bool pressed_ = false;                               ///< Reset on every matching release.
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
            *packet.atlas, {.label = i,
                            .origin = {bounds.x + 6, bounds.y + 3},
                            .clip = bounds,
                            .color = plan.palette ? plan.palette->color(plan.palette->find("text"))
                                                  : ui::rgba{1, 1, 1, 1}});
        packet.quads.insert(packet.quads.end(), glyphs.begin(), glyphs.end());
    }
}
} // namespace

/// @brief Build independent layout snapshots for authoring chrome and its preview.
panel_scene::panel_scene(const panel_plan& plan, ui::element_tree& preview,
                         ui::layout_rect viewport, float scale, ui::layout_engine& layout,
                         ui::paint_engine& paint, const diagnostics::services& sinks,
                         std::optional<control_action> focus)
    : initial_focus_{std::move(focus)},
      tree_{{.max_elements = static_cast<std::uint32_t>(plan.controls.size() + 1)}, {}, sinks},
      input_{tree_, {.max_events = 4096}, {}, sinks}, scale_{scale}
{
    const auto root = tree_.create();
    palette_ = plan.palette;
    tree_.set_panel(root, ui::canvas_layout{});
    for (const auto& item : plan.controls)
    {
        const auto node = make_element(tree_, root, item);
        if (plan.palette && item.color[3] > 0)
        {
            const auto key = item.color_key.empty()
                                 ? (item.action.kind == control_kind::none ? "surface" : "control")
                                 : item.color_key;
            const auto slot = plan.palette->find(key);
            auto style = tree_.paint(node);
            style.color = plan.palette->color(slot);
            tree_.set_paint(node, style);
            colors_.emplace_back(node, slot);
        }

        if (item.action.kind != control_kind::none)
        {
            input_.bind(node, std::make_shared<click_handler>(item.action, pending_, item.accepts),
                        {.focusable = true});
            input_.configure_accessibility(node,
                                           {.role = item.action.kind == control_kind::drop_asset
                                                        ? accessibility::role::group
                                                        : accessibility::role::button,
                                            .name = item.label});
            focus_targets_.emplace_back(node, item.action);
        }

        if (item.source)
        {
            input_.configure_drag_source(node, *item.source);
        }

        if (item.destination)
        {
            input_.configure_drop_target(node, *item.destination);
            destinations_.emplace_back(node, item.action);
        }
    }

    input_.configure_navigation();
    input_.configure_keyboard_activation();

    tree_.set_viewport(viewport);
    source_ = tree_.snapshot();
    geometry_ = layout.calculate(source_);
    const auto chrome = paint.calculate(source_, geometry_);
    auto result = std::make_shared<editor_frame>();
    result->viewport = viewport;
    result->quads.assign(plan.shell.quads().begin(), plan.shell.quads().end());
    result->quads.insert(result->quads.end(), chrome.quads().begin(), chrome.quads().end());
    tool_begin_ = result->quads.size();
    if (plan.flow_preview)
    {
        result->quads.insert(result->quads.end(), plan.flow_preview->begin(),
                             plan.flow_preview->end());
    }
    else if (plan.tool)
    {
        tool_visible_ = true;
        result->quads.insert(result->quads.end(), plan.tool->quads().begin(),
                             plan.tool->quads().end());
    }
    else
    {
        const auto preview_geometry = layout.update(preview, plan.preview);
        const auto displayed = paint.calculate(preview.snapshot(), preview_geometry);
        result->quads.insert(result->quads.end(), displayed.quads().begin(),
                             displayed.quads().end());
    }

    text_begin_ = result->quads.size();
    append_labels(*result, plan, scale_, sinks);
    packet_ = std::move(result);
}

/// @brief Borrow the retained main/render handoff payload.
const std::shared_ptr<const editor_frame>& panel_scene::packet() const noexcept
{
    return packet_;
}

/// @brief Dispatch a batch in order and execute actions only after leaving callbacks.
std::span<const ui::input_event> panel_scene::begin_input(std::span<const input::event> events)
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

    ui::validate_input_events(input_bridge_.values());
    input_.begin_frame(++frame_, source_, geometry_);
    input_.process({});
    if (initial_focus_)
    {
        const auto found =
            std::ranges::find_if(focus_targets_,
                                 [&](const auto& item)
                                 {
                                     return item.second.kind == initial_focus_->kind &&
                                            item.second.target == initial_focus_->target;
                                 });
        if (found != focus_targets_.end())
        {
            input_.focus(found->first);
        }

        initial_focus_.reset();
    }

    return input_bridge_.values();
}

/// @brief Rebuilds preserve keyboard intent without reusing a retired runtime identity.
std::optional<control_action> panel_scene::focused_action() const
{
    const auto target = input_.focused();
    const auto found = std::ranges::find(focus_targets_, target,
                                         [](const auto& item)
                                         {
                                             return item.first;
                                         });
    return found == focus_targets_.end() ? std::nullopt : std::optional{found->second};
}

/// @brief Seal only after all window drag callbacks and transactions have returned.
std::vector<control_action> panel_scene::finish_input()
{
    input_.tick();
    input_.late_tick();
    input_.end_frame();
    tree_.apply(tree_.commands().seal());
    return std::exchange(pending_, {});
}

/// @brief Standalone routing remains available for isolated panel tests.
std::vector<control_action> panel_scene::dispatch(std::span<const input::event> events)
{
    const auto packets = begin_input(events);
    input_.process_more(packets);
    return finish_input();
}

/// @brief Borrow the serial owner during root-orchestrated dispatch.
ui::input_session& panel_scene::input() noexcept
{
    return input_;
}

/// @brief Authored tool destinations are intentionally not mistaken for native editor controls.
control_action panel_scene::destination(ui::element_handle target) const
{
    const auto found = std::ranges::find_if(destinations_,
                                            [&](const auto& item)
                                            {
                                                return item.first == target;
                                            });
    return found == destinations_.end() ? control_action{} : found->second;
}
} // namespace atlas::editor
