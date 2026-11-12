/** @file @brief Bounded responsive hierarchy, inspector, and toolbar descriptions. */
#include "panels.hpp"
#include <algorithm>
#include <format>
#include <map>

namespace atlas::editor
{
namespace
{
constexpr std::array<float, 4> panel_color{0.075f, 0.105f, 0.15f, 1};  ///< Panel background.
constexpr std::array<float, 4> button_color{0.14f, 0.21f, 0.30f, 1};   ///< Interactive control.
constexpr std::array<float, 4> selected_color{0.18f, 0.37f, 0.55f, 1}; ///< Selection accent.
constexpr std::array<float, 4> clear_color{0, 0, 0, 0};                ///< Text-only label.

/// @brief Add a clickable caption without retaining a callback in the panel description.
void button(panel_plan& result, ui::layout_rect bounds, std::string caption, control_kind kind)
{
    result.controls.push_back({bounds, button_color, std::move(caption), {kind, {}}});
}

/// @brief Lay out one page while computing indentation from stable parent IDs.
void hierarchy(panel_plan& result, const document_session& session, ui::layout_rect bounds,
               std::size_t page)
{
    constexpr auto rows = hierarchy_page_size;
    const auto& elements = session.document().elements;
    const auto last_page = (elements.size() - 1) / rows;
    const auto first = std::min(page, last_page) * rows;
    std::map<std::string_view, std::size_t> depths;
    for (std::size_t i = 0; i < std::min(first + rows, elements.size()); ++i)
    {
        const auto& node = elements[i];
        const auto depth = node.parent.empty() ? std::size_t{0} : depths.at(node.parent) + 1;
        depths.emplace(node.id, depth);
        if (i >= first)
        {
            const float indent = static_cast<float>(std::min(depth, std::size_t{6})) * 9.f;
            const float y = bounds.y + 34.f + static_cast<float>(i - first) * 25.f;
            result.controls.push_back(
                {{bounds.x + 8.f + indent, y, bounds.width - 16.f - indent, 23},
                 node.id == session.selected() ? selected_color : button_color,
                 node.id,
                 {control_kind::select, node.id}});
        }
    }

    if (first > 0)
    {
        button(result, {bounds.x + 8, bounds.y + 336, 62, 24}, "Prev", control_kind::previous);
    }

    if (first / rows < last_page)
    {
        button(result, {bounds.x + 76, bounds.y + 336, 62, 24}, "Next", control_kind::next);
    }
}

/// @brief Expose basic scalar authoring without binding to live preview entities.
void inspector(panel_plan& result, const document_session& session, ui::layout_rect bounds)
{
    const auto& elements = session.document().elements;
    const auto selected = std::ranges::find(elements, session.selected(), &ui_content::element::id);
    if (selected == elements.end())
    {
        throw std::logic_error("Editor inspector selection is unavailable");
    }

    const float x = bounds.x + 8;
    const float w = bounds.width - 16;
    result.controls.push_back({{x, bounds.y + 36, w, 26}, clear_color, selected->id});
    result.controls.push_back({{x, bounds.y + 67, w, 26},
                               clear_color,
                               selected->requested.width_fixed
                                   ? std::format("Width {:.0f}", selected->requested.width)
                                   : "Width auto"});
    button(result, {x, bounds.y + 99, (w - 4) / 2, 25}, "- Width", control_kind::narrower);
    button(result, {x + (w + 4) / 2, bounds.y + 99, (w - 4) / 2, 25}, "+ Width",
           control_kind::wider);
    button(result, {x, bounds.y + 132, w, 25}, selected->requested.visible ? "Visible" : "Hidden",
           control_kind::toggle_visible);
    if (!std::holds_alternative<ui::leaf_layout>(selected->panel))
    {
        button(result, {x, bounds.y + 167, w, 25}, "Add child", control_kind::add_child);
    }

    if (!selected->parent.empty())
    {
        button(result, {x, bounds.y + 200, w, 25}, "Remove subtree", control_kind::remove);
    }
}
} // namespace

/// @brief Keep the authored runtime viewport isolated between the editor panels.
panel_plan make_panels(const document_session& session, ui::layout_extent size, std::size_t page,
                       std::string_view status)
{
    panel_plan result;
    const float side = std::clamp(size.width * 0.23f, 132.f, 220.f);
    const float height = std::max(1.f, size.height - 64);
    const ui::layout_rect left{8, 48, side, height};
    const ui::layout_rect right{std::max(side + 24, size.width - side - 8), 48, side, height};
    const ui::layout_rect center{side + 16, 48, std::max(1.f, right.x - side - 24), height};
    result.preview = {center.x, center.y + 32, center.width, std::max(1.f, center.height - 32)};
    result.controls = {{left, panel_color, "Hierarchy"},
                       {right, panel_color, "Inspector"},
                       {center, panel_color, "Preview"}};
    button(result, {8, 8, 56, 30}, "New", control_kind::new_document);
    button(result, {70, 8, 58, 30}, "Save", control_kind::save);
    button(result, {134, 8, 72, 30}, "Reopen", control_kind::reopen);
    if (session.can_undo())
    {
        button(result, {212, 8, 62, 30}, "Undo", control_kind::undo);
    }

    if (session.can_redo())
    {
        button(result, {280, 8, 62, 30}, "Redo", control_kind::redo);
    }

    result.controls.push_back(
        {{352, 8, std::max(1.f, size.width - 360), 30},
         clear_color,
         std::string{session.dirty() ? "Unsaved  " : "Saved  "} + std::string{status}});
    hierarchy(result, session, left, page);
    inspector(result, session, right);
    return result;
}
} // namespace atlas::editor
