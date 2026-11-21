/** @file @brief Clipped Atlas graph nodes and elbow connections reuse persistent tree storage. */
#include "canvas_state.hpp"
#include <algorithm>

namespace atlas::editor_flow
{
/// @brief Node rectangles and edge segments use the same portable Canvas layout as authored UI.
void canvas::impl::rect(ui::element_handle node, ui::layout_rect area, std::array<float, 4> color,
                        std::int32_t layer)
{
    ui::layout_style style;
    style.constraints.minimum = {area.width, area.height};
    style.constraints.maximum = style.constraints.minimum;
    tree.set_layout(node, style);
    ui::canvas_slot slot;
    slot.horizontal.offset_min = area.x;
    slot.vertical.offset_min = area.y;
    tree.set_canvas_slot(node, slot);
    ui::paint_style appearance;
    appearance.fill = true;
    appearance.layer = layer;
    appearance.color = color;
    tree.set_paint(node, appearance);
}

/// @brief Reuse up to three standard rectangles per route rather than allocate jobs for lines.
void canvas::impl::connections()
{
    std::size_t used = 0;
    const auto segment = [&](ui::layout_rect area)
    {
        if (used == edges.size())
        {
            const auto leaf = tree.create(root);
            tree.set_panel(leaf, ui::leaf_layout{});
            edges.push_back(leaf);
        }

        rect(edges[used++], area, {0.32F, 0.55F, 0.72F, 1}, 10);
    };
    const auto& graph = tool.edit().current().graph;
    for (const auto& route : graph.transitions)
    {
        if (!nodes.contains(route.from) || !nodes.contains(route.to))
        {
            continue;
        }

        const auto from = tool.to_view(tool.position({item_kind::screen, route.from}));
        const auto to = tool.to_view(tool.position({item_kind::screen, route.to}));
        const auto x1 = static_cast<float>(from.x + 144 * tool.zoom());
        const auto y1 = static_cast<float>(from.y + 22 * tool.zoom());
        const auto x2 = static_cast<float>(to.x);
        const auto y2 = static_cast<float>(to.y + 22 * tool.zoom());
        const auto middle = (x1 + x2) / 2;
        segment({std::min(x1, middle), y1, std::max(2.F, std::abs(middle - x1)), 2});
        segment({middle, std::min(y1, y2), 2, std::max(2.F, std::abs(y2 - y1))});
        segment({std::min(x2, middle), y2, std::max(2.F, std::abs(middle - x2)), 2});
    }

    while (edges.size() > used)
    {
        tree.remove(edges.back());
        edges.pop_back();
    }
}

/// @brief Build immutable geometry and paint while retaining capture ownership between frames.
ui::paint_snapshot canvas::arrange(ui::layout_rect bounds, ui::layout_engine& layout,
                                   ui::paint_engine& paint)
{
    const diagnostics::operation_scope scope{impl_->sinks, {"editor.flow.canvas", "editor.flow"}};
    const auto& source = impl_->tool.edit().current();
    impl_->bounds = bounds;
    for (auto it = impl_->nodes.begin(); it != impl_->nodes.end();)
    {
        if (std::ranges::find(source.graph.screens, it->first, &ui_content::flow_screen::id) ==
            source.graph.screens.end())
        {
            impl_->input.unbind(it->second);
            impl_->tree.remove(it->second);
            it = impl_->nodes.erase(it);
        }
        else
        {
            ++it;
        }
    }

    impl_->labels.clear();
    impl_->labels.reserve(source.graph.screens.size());
    const auto selected = impl_->tool.selection();
    for (const auto& screen : source.graph.screens)
    {
        const item_ref item{item_kind::screen, screen.id};
        auto found = impl_->nodes.find(screen.id);
        if (found == impl_->nodes.end())
        {
            found = impl_->nodes.emplace(screen.id, impl_->create(item)).first;
        }

        const auto point = impl_->tool.to_view(impl_->tool.position(item));
        const auto scale = static_cast<float>(impl_->tool.zoom());
        ui::layout_rect area{static_cast<float>(point.x), static_cast<float>(point.y), 144 * scale,
                             44 * scale};
        const bool active = std::ranges::find(selected, item) != selected.end();
        impl_->rect(found->second, area,
                    active ? std::array{0.22F, 0.43F, 0.65F, 1.F}
                           : std::array{0.12F, 0.20F, 0.29F, 1.F});
        const auto metadata = std::ranges::find(source.layout, item, &item_layout::item);
        area.x += bounds.x;
        area.y += bounds.y;
        const auto right = std::min(area.x + area.width, bounds.x + bounds.width);
        const auto bottom = std::min(area.y + area.height, bounds.y + bounds.height);
        area.x = std::max(area.x, bounds.x);
        area.y = std::max(area.y, bounds.y);
        area.width = std::max(0.F, right - area.x);
        area.height = std::max(0.F, bottom - area.y);
        impl_->labels.push_back(
            {item, metadata == source.layout.end() ? screen.id : metadata->label, area});
    }

    impl_->connections();
    impl_->tree.set_viewport(bounds);
    impl_->source = impl_->tree.snapshot();
    impl_->geometry = layout.calculate(impl_->source);
    return paint.calculate(impl_->source, impl_->geometry);
}
} // namespace atlas::editor_flow
