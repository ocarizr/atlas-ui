/** @file @brief Authoring rejects invalid grid topology before constructing a live instance. */
#include "document_validation.hpp"
#include <algorithm>
#include <map>

namespace atlas::ui_content::detail
{
/// @brief Group immediate children by their declared parent while preserving declaration order.
void validate_grids(const document& source)
{
    if (std::ranges::none_of(source.elements,
                             [](const auto& node)
                             {
                                 return node.grid.has_value() ||
                                        std::holds_alternative<ui::grid_layout>(node.panel);
                             }))
    {
        return;
    }

    std::map<std::string_view, const element*> elements;
    std::map<std::string_view, std::vector<ui::grid_slot>> groups;
    for (const auto& node : source.elements)
    {
        elements.emplace(node.id, &node);
        if (node.parent.empty())
        {
            if (node.grid)
            {
                throw std::invalid_argument("root element cannot have a grid slot");
            }

            continue;
        }

        const auto* parent = elements.at(node.parent);
        const auto* grid = std::get_if<ui::grid_layout>(&parent->panel);
        if (node.grid && !grid)
        {
            throw std::invalid_argument("grid slot requires an authored grid parent");
        }

        if (grid)
        {
            if (node.canvas || node.stack)
            {
                throw std::invalid_argument("incompatible authored grid child metadata");
            }

            groups[node.parent].push_back(node.grid.value_or(ui::grid_slot{}));
        }
    }

    for (auto& [parent, slots] : groups)
    {
        ui::resolve_grid_slots(std::get<ui::grid_layout>(elements.at(parent)->panel), slots, slots);
    }
}
} // namespace atlas::ui_content::detail
