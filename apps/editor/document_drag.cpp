/** @file @brief Candidate-first hierarchy transfer without partial document mutation. */
#include "document_session.hpp"
#include <algorithm>
#include <set>

namespace atlas::editor
{
/// @brief Reject roots, cycles, missing nodes, leaves and no-op hierarchy transfers.
bool document_session::can_reparent(std::string_view source, std::string_view destination) const
{
    const auto& nodes = document().elements;
    const auto from = std::ranges::find(nodes, source, &ui_content::element::id);
    const auto to = std::ranges::find(nodes, destination, &ui_content::element::id);
    if (from == nodes.end() || to == nodes.end() || from->parent.empty() || source == destination ||
        from->parent == destination || std::holds_alternative<ui::leaf_layout>(to->panel))
    {
        return false;
    }

    for (auto parent = to; !parent->parent.empty();)
    {
        if (parent->parent == source)
        {
            return false;
        }

        parent = std::ranges::find(nodes, parent->parent, &ui_content::element::id);
        if (parent == nodes.end())
        {
            return false;
        }
    }

    return true;
}

/// @brief Validate a complete candidate before publishing one undoable hierarchy move.
bool document_session::reparent(std::string_view source, std::string_view destination)
{
    const diagnostics::operation_scope scope{options_.diagnostics,
                                             {"editor.drag.reparent", "editor"}};
    if (!can_reparent(source, destination))
    {
        return false;
    }

    auto candidate = document();
    auto selected = std::string{source};
    auto& moved = *std::ranges::find(candidate.elements, source, &ui_content::element::id);
    moved.parent = destination;
    moved.canvas.reset();
    moved.stack.reset();
    moved.grid.reset();
    moved.horizontal.reset();
    moved.vertical.reset();
    // Re-establish parent-before-child order; stable siblings keep their existing order.
    std::vector<ui_content::element> ordered;
    ordered.reserve(candidate.elements.size());
    std::set<std::string, std::less<>> emitted;
    while (ordered.size() < candidate.elements.size())
    {
        const auto previous = ordered.size();
        for (auto& node : candidate.elements)
        {
            if (!node.id.empty() && (node.parent.empty() || emitted.contains(node.parent)))
            {
                emitted.insert(node.id);
                ordered.push_back(std::move(node));
                node = {};
            }
        }

        if (previous == ordered.size())
        {
            throw std::logic_error("cyclic editor hierarchy");
        }
    }

    candidate.elements = std::move(ordered);
    auto ready =
        make_preview(candidate); // Includes validation of retained sibling/scroll/focus links.
    const auto revision = allocate_revision();
    if (undo_.size() == 32)
    {
        undo_.erase(undo_.begin());
    }

    undo_.push_back(std::move(current_));
    current_ = {std::move(candidate), std::move(selected), revision};
    preview_ = std::move(ready);
    redo_.clear();
    return true;
}
} // namespace atlas::editor
