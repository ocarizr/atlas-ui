/** @file @brief Stable region, node and route inspectors with explicit asset and popup policies. */
#include "flow_panel.hpp"
#include <algorithm>

namespace atlas::editor
{
namespace
{
/// @brief Read a declaration by ID and return owned values for an independent inspector edit.
template <class T> T find(const std::vector<T>& values, std::string_view id)
{
    const auto found = std::ranges::find(values, id, &T::id);
    if (found == values.end())
    {
        throw std::invalid_argument("Flow inspector item no longer exists");
    }

    return *found;
}
} // namespace

/// @brief Organize independent graph declaration and UI-binding inspector sections.
void flow_panel::inspect(std::vector<control>& rows, const document_session& ui)
{
    const auto& source = document_.edit().current();
    rows.push_back({{}, {0, 0, 0, 0}, source.id + (document_.edit().dirty() ? " *" : "")});
    if (document_.recovery_pending())
    {
        rows.push_back({{}, {0, 0, 0, 0}, "Recovery available: Recover or Discard"});
    }

    for (const auto& region : source.graph.regions)
    {
        button(rows, "Region: " + region.id, "region:" + region.id);
    }

    button(rows,
           asset_ ? "Asset: " + std::string{assets::format_type(asset_->type)} + " | next"
                  : "Choose typed asset | next",
           "asset");
    if (asset_)
    {
        rows.push_back({{}, {0, 0, 0, 0}, assets::format_id(asset_->id)});
    }

    inspect_region(rows);
    inspect_screen(rows, ui);
    inspect_transition(rows);
}

/// @brief Edit named submenu hosts and region ownership without rewriting identities.
void flow_panel::inspect_region(std::vector<control>& rows)
{
    const auto& source = document_.edit().current();
    if (region_selection_)
    {
        auto id = *region_selection_;
        const auto region =
            std::ranges::find(source.graph.regions, id, &ui_content::flow_region::id);
        if (region != source.graph.regions.end())
        {
            field(rows, "region_initial", "Entry submenu/screen", region->initial,
                  [this, id](std::string value)
                  {
                      auto region = find(document_.edit().current().graph.regions, id);
                      region.initial = std::move(value);
                      tool_.update_region(std::move(region));
                  });
            field(rows, "region_owner", "Owning menu (empty: root)", region->owner,
                  [this, id](std::string value)
                  {
                      auto region = find(document_.edit().current().graph.regions, id);
                      region.owner = std::move(value);
                      tool_.update_region(std::move(region));
                  });
            field(rows, "region_host", "Submenu Area element", region->host,
                  [this, id](std::string value)
                  {
                      auto region = find(document_.edit().current().graph.regions, id);
                      region.host = std::move(value);
                      tool_.update_region(std::move(region));
                  });
            operation(rows, region->remember ? "Remember tabs: on" : "Remember tabs: off",
                      "region_remember",
                      [this, id](const auto&)
                      {
                          auto region = find(document_.edit().current().graph.regions, id);
                          region.remember = !region.remember;
                          tool_.update_region(std::move(region));
                      });
            operation(rows, "Delete this region and owned content", "region_delete",
                      [this, id](const auto&)
                      {
                          tool_.select({editor_flow::item_kind::region, id});
                          tool_.erase_selection();
                          region_selection_.reset();
                      });
        }
    }
}

/// @brief Edit typed node assets, popup overrides, focus and display captions.
void flow_panel::inspect_screen(std::vector<control>& rows, const document_session& ui)
{
    const auto& source = document_.edit().current();
    const auto selection = tool_.selection();
    if (!selection.empty() && selection.front().kind == editor_flow::item_kind::screen)
    {
        auto id = selection.front().id;
        const auto screen = find(source.graph.screens, id);
        if (!region_selection_)
        {
            region_ = screen.region;
        }

        const auto metadata =
            std::ranges::find(source.layout, selection.front(), &editor_flow::item_layout::item);
        field(rows, "caption", "Node " + id, metadata == source.layout.end() ? id : metadata->label,
              [this, id](std::string value)
              {
                  tool_.rename({editor_flow::item_kind::screen, id}, std::move(value));
              });
        field(rows, "node_region", "Region", screen.region,
              [this, id](std::string value)
              {
                  auto screen = find(document_.edit().current().graph.screens, id);
                  screen.region = std::move(value);
                  tool_.update_screen(std::move(screen));
              });
        field(rows, "node_focus", "Initial focus", screen.focus,
              [this, id](std::string value)
              {
                  auto screen = find(document_.edit().current().graph.screens, id);
                  screen.focus = std::move(value);
                  tool_.update_screen(std::move(screen));
              });
        rows.push_back({{},
                        {0, 0, 0, 0},
                        screen.asset.id == assets::asset_id{}
                            ? "Missing asset (saveable draft)"
                            : assets::format_id(screen.asset.id)});
        if (screen.asset.type == assets::asset_type::popup)
        {
            const std::array names{"Modal", "Outside dismiss", "Back dismiss"};
            const std::array values{screen.popup.modal, screen.popup.dismiss_outside,
                                    screen.popup.dismiss_back};
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                operation(rows,
                          std::string{names[i]} + ": " +
                              (values[i] ? (*values[i] ? "on" : "off") : "inherit"),
                          "popup_" + std::to_string(i),
                          [this, id, i](const auto&)
                          {
                              auto screen = find(document_.edit().current().graph.screens, id);
                              std::array slots{&screen.popup.modal, &screen.popup.dismiss_outside,
                                               &screen.popup.dismiss_back};
                              auto& slot = *slots[i];
                              slot = !slot   ? std::optional{true}
                                     : *slot ? std::optional{false}
                                             : std::nullopt;
                              tool_.update_screen(std::move(screen));
                          });
            }
        }

        for (const auto& route : tool_.routes(id))
        {
            button(rows, "Route: " + route.id + " -> " + route.to, "route:" + route.id);
        }

        inspect_events(rows, ui);
    }
}

/// @brief Keep outgoing routes, history policy and predicate decorators together.
void flow_panel::inspect_transition(std::vector<control>& rows)
{
    const auto& source = document_.edit().current();
    const auto edge =
        std::ranges::find(source.graph.transitions, route_, &ui_content::flow_transition::id);
    if (edge != source.graph.transitions.end())
    {
        auto id = edge->id;
        field(rows, "edge_from", "Route " + id + " from", edge->from,
              [this, id](std::string value)
              {
                  auto route = find(document_.edit().current().graph.transitions, id);
                  route.from = std::move(value);
                  tool_.update_transition(std::move(route));
              });
        field(rows, "edge_to", "Destination", edge->to,
              [this, id](std::string value)
              {
                  auto route = find(document_.edit().current().graph.transitions, id);
                  route.to = std::move(value);
                  tool_.update_transition(std::move(route));
              });
        const std::array operations{"Open with history", "Replace", "Close top", "Back",
                                    "Open popup",        "Reload",  "Exit menu", "Close popup"};
        operation(rows, operations[static_cast<std::size_t>(edge->operation)], "edge_operation",
                  [this, id](const auto&)
                  {
                      auto route = find(document_.edit().current().graph.transitions, id);
                      route.operation = static_cast<ui_content::flow_operation>(
                          (static_cast<unsigned>(route.operation) + 1) % 8);
                      if (route.operation != ui_content::flow_operation::push &&
                          route.operation != ui_content::flow_operation::replace &&
                          route.operation != ui_content::flow_operation::overlay)
                      {
                          route.to.clear();
                      }

                      auto source = document_.edit().current();
                      *std::ranges::find(source.graph.transitions, id,
                                         &ui_content::flow_transition::id) = std::move(route);
                      document_.edit().apply(std::move(source));
                  });
        operation(rows, edge->enabled ? "Route enabled" : "Route disabled", "edge_enabled",
                  [this, id](const auto&)
                  {
                      auto route = find(document_.edit().current().graph.transitions, id);
                      route.enabled = !route.enabled;
                      tool_.update_transition(std::move(route));
                  });
        inspect_conditions(rows, id);
        operation(rows, "Delete route", "edge_delete",
                  [this, id](const auto&)
                  {
                      tool_.select({editor_flow::item_kind::transition, id});
                      tool_.erase_selection();
                      route_.clear();
                  });
    }
}
} // namespace atlas::editor
