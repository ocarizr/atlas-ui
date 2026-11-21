/** @file @brief Stable-identity graph mutations with atomic draft validation. */
#include "document_state.hpp"
#include <algorithm>
#include <atlas/editor_flow/workspace.hpp>
#include <set>

namespace atlas::editor_flow
{
namespace
{
/// @brief Resolve a declaration by persistent ID, never by vector offset.
template <class T> auto& require(std::vector<T>& values, std::string_view id)
{
    const auto found = std::ranges::find(values, id, &T::id);
    if (found == values.end())
    {
        throw std::invalid_argument("Unknown Flow item: " + std::string{id});
    }

    return *found;
}

/// @brief Menu roots and submenu regions offer only compatible destination asset families.
void compatible(const document& source, const ui_content::flow_screen& node)
{
    const auto found =
        std::ranges::find(source.graph.regions, node.region, &ui_content::flow_region::id);
    if (found == source.graph.regions.end())
    {
        throw std::invalid_argument("Unknown Flow destination region");
    }

    const auto& region = *found;
    if (node.asset.type == assets::asset_type::widget ||
        (!region.owner.empty() && node.asset.type != assets::asset_type::submenu) ||
        (region.owner.empty() && node.asset.type != assets::asset_type::menu &&
         node.asset.type != assets::asset_type::popup && node.asset.type != assets::asset_type::ui))
    {
        throw std::invalid_argument("Incompatible Flow presentation asset");
    }

    if (node.asset.id != assets::asset_id{})
    {
        (void)assets::format_id(node.asset.id);
    }
}

/// @brief Only forward history operations carry a destination, in the same region.
void route_shape(document& source, const ui_content::flow_transition& route)
{
    const auto& from = require(source.graph.screens, route.from);
    const bool target = route.operation == ui_content::flow_operation::push ||
                        route.operation == ui_content::flow_operation::replace ||
                        route.operation == ui_content::flow_operation::overlay;
    if (target)
    {
        const auto& to = require(source.graph.screens, route.to);
        if (from.region != to.region)
        {
            throw std::invalid_argument("Flow routes cannot cross region ownership");
        }
    }
    else if (!route.to.empty())
    {
        throw std::invalid_argument("Flow history return has no authored destination");
    }
}
} // namespace

/// @brief Unresolved initial/host values remain editable, but declaration identities stay unique.
void workspace::add_region(ui_content::flow_region region)
{
    auto source = owner_.current();
    source.graph.regions.push_back(std::move(region));
    owner_.apply(std::move(source));
}

/// @brief Typed asset selection prevents reusable widgets from becoming navigation destinations.
void workspace::add_screen(ui_content::flow_screen screen, point at, std::string label)
{
    auto source = owner_.current();
    compatible(source, screen);
    source.layout.push_back(
        {{item_kind::screen, screen.id}, label.empty() ? screen.id : std::move(label), at.x, at.y});
    source.selection = {item_kind::screen, screen.id};
    source.graph.screens.push_back(std::move(screen));
    owner_.apply(std::move(source));
}

/// @brief Connections use explicit stable route IDs; repeated endpoints remain distinguishable.
void workspace::connect(ui_content::flow_transition route)
{
    auto source = owner_.current();
    route_shape(source, route);
    source.graph.transitions.push_back(std::move(route));
    owner_.apply(std::move(source));
}

/// @brief Region ownership and host-element names are independent editable fields.
void workspace::update_region(ui_content::flow_region region)
{
    auto source = owner_.current();
    require(source.graph.regions, region.id) = std::move(region);
    owner_.apply(std::move(source));
}

/// @brief A replacement asset preserves the graph screen identity and every referring route.
void workspace::update_screen(ui_content::flow_screen screen)
{
    auto source = owner_.current();
    compatible(source, screen);
    require(source.graph.screens, screen.id) = std::move(screen);
    owner_.apply(std::move(source));
}

/// @brief Inspector edits obey the same region permissions as pointer-created connections.
void workspace::update_transition(ui_content::flow_transition route)
{
    auto source = owner_.current();
    route_shape(source, route);
    require(source.graph.transitions, route.id) = std::move(route);
    owner_.apply(std::move(source));
}

/// @brief Display-name edits never rewrite graph identities, asset handles or binding targets.
void workspace::rename(item_ref item, std::string label)
{
    auto source = owner_.current();
    if (!detail::contains(source, item) || item.kind == item_kind::none)
    {
        throw std::invalid_argument("Unknown Flow caption target");
    }

    auto found = std::ranges::find(source.layout, item, &item_layout::item);
    if (found == source.layout.end())
    {
        source.layout.push_back({std::move(item), std::move(label), 0, 0});
    }
    else
    {
        found->label = std::move(label);
    }

    owner_.apply(std::move(source));
}

/// @brief Delete owned regions/screens recursively and remove only their dangling graph links.
void workspace::erase_selection()
{
    auto source = owner_.current();
    std::set<std::string> screens, regions, routes;
    for (const auto& item : selection())
    {
        if (item.kind == item_kind::screen)
        {
            screens.insert(item.id);
        }

        if (item.kind == item_kind::region)
        {
            regions.insert(item.id);
        }

        if (item.kind == item_kind::transition)
        {
            routes.insert(item.id);
        }
    }

    bool changed;
    do
    {
        const auto count = screens.size() + regions.size();
        for (const auto& region : source.graph.regions)
        {
            if (screens.contains(region.owner))
            {
                regions.insert(region.id);
            }
        }

        for (const auto& screen : source.graph.screens)
        {
            if (regions.contains(screen.region))
            {
                screens.insert(screen.id);
            }
        }

        changed = count != screens.size() + regions.size();
    } while (changed);

    std::erase_if(source.graph.regions,
                  [&](const auto& v)
                  {
                      return regions.contains(v.id);
                  });
    std::erase_if(source.graph.screens,
                  [&](const auto& v)
                  {
                      return screens.contains(v.id);
                  });
    for (const auto& route : source.graph.transitions)
    {
        if (screens.contains(route.from) || screens.contains(route.to))
        {
            routes.insert(route.id);
        }
    }

    std::erase_if(source.graph.transitions,
                  [&](const auto& v)
                  {
                      return routes.contains(v.id);
                  });
    std::erase_if(source.conditions,
                  [&](const auto& v)
                  {
                      return routes.contains(v.transition);
                  });
    for (auto& region : source.graph.regions)
    {
        if (screens.contains(region.initial))
        {
            region.initial.clear();
        }
    }

    std::erase_if(source.layout,
                  [&](const auto& v)
                  {
                      return !detail::contains(source, v.item);
                  });
    source.selection = {};
    owner_.apply(std::move(source));
    selected_.clear();
}

/// @brief Runtime-disabled routes remain inspector-visible without permitting other sources.
std::vector<ui_content::flow_transition> workspace::routes(std::string_view source) const
{
    std::vector<ui_content::flow_transition> result;
    for (const auto& route : owner_.current().graph.transitions)
    {
        if (route.from == source)
        {
            result.push_back(route);
        }
    }

    return result;
}
} // namespace atlas::editor_flow
