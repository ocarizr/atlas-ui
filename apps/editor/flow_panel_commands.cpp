/** @file @brief Bounded graph commands and independent document lifecycle. */
#include "flow_panel.hpp"
#include <algorithm>
#include <limits>

namespace atlas::editor
{
/// @brief Allocate a graph identity independently from display labels and vector positions.
std::string flow_panel::allocate_id(std::string_view prefix)
{
    std::string result;
    const auto& graph = document_.edit().current().graph;
    do
    {
        if (next_ == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("Flow identity sequence exhausted");
        }

        result = std::string{prefix} + identity_scope_ + "_" + std::to_string(next_++);
    } while (std::ranges::find(graph.screens, result, &ui_content::flow_screen::id) !=
                 graph.screens.end() ||
             std::ranges::find(graph.regions, result, &ui_content::flow_region::id) !=
                 graph.regions.end() ||
             std::ranges::find(graph.transitions, result, &ui_content::flow_transition::id) !=
                 graph.transitions.end());

    return result;
}

/// @brief Create typed declarations and same-region connections with stable identities.
bool flow_panel::create_command(std::string_view action, document_session& ui)
{
    const auto selection = tool_.selection();
    if (action == "screen")
    {
        const auto label = "Screen " + std::to_string(next_);
        const auto id = allocate_id("screen_");
        const auto& graph = document_.edit().current().graph;
        const auto region = std::ranges::find(graph.regions, region_, &ui_content::flow_region::id);
        if (region == graph.regions.end())
        {
            throw std::invalid_argument("Select an existing destination region");
        }

        const auto fallback =
            region->owner.empty() ? assets::asset_type::menu : assets::asset_type::submenu;
        const auto graph_row = graph.screens.size() / 4;
        tool_.add_screen({id, region_, asset_.value_or(assets::asset_ref{{}, fallback})},
                         tool_.to_graph({30 + static_cast<double>(graph.screens.size() % 4) * 165,
                                         30 + static_cast<double>(graph_row) * 75}),
                         label);
        tool_.select({editor_flow::item_kind::screen, id});
        region_selection_.reset();
    }
    else if (action == "region")
    {
        region_ = allocate_id("region_");
        const auto owner =
            selection.empty() || selection.front().kind != editor_flow::item_kind::screen
                ? std::string{}
                : selection.front().id;
        tool_.add_region(
            {region_, "", owner, owner.empty() ? "" : std::string{ui.selected()}, true});
        region_selection_ = region_;
        asset_.reset();
    }
    else if (action == "connect")
    {
        if (selection.size() != 2 || selection[0].kind != editor_flow::item_kind::screen ||
            selection[1].kind != editor_flow::item_kind::screen)
        {
            throw std::invalid_argument("M-select source then destination before Connect");
        }

        const auto& screens = document_.edit().current().graph.screens;
        const auto target =
            std::ranges::find(screens, selection[1].id, &ui_content::flow_screen::id);
        auto operation = ui_content::flow_operation::push;
        if (target->asset.type == assets::asset_type::popup)
        {
            operation = ui_content::flow_operation::overlay;
        }

        if (target->asset.type == assets::asset_type::submenu)
        {
            operation = ui_content::flow_operation::replace;
        }

        route_ = allocate_id("route_");
        tool_.connect({route_, selection[0].id, selection[1].id, operation});
    }
    else
    {
        return false;
    }

    return true;
}

/// @brief Choose a compatible current catalog asset while preserving graph identity.
void flow_panel::choose_asset()
{
    const auto selection = tool_.selection();
    if (!repository_)
    {
        throw std::invalid_argument("Open an asset catalog before choosing a destination");
    }

    const auto choices = editor_flow::asset_choices(document_.edit().current(), region_,
                                                    repository_->snapshot().document().records);
    if (choices.empty())
    {
        throw std::invalid_argument("No compatible presentation assets in this region");
    }

    auto found = asset_ ? std::ranges::find(choices, *asset_, &assets::asset_record::reference)
                        : choices.end();
    if (found != choices.end())
    {
        ++found;
    }

    if (found == choices.end())
    {
        found = choices.begin();
    }

    asset_ = found->reference;
    if (selection.size() == 1 && selection.front().kind == editor_flow::item_kind::screen)
    {
        auto node = *std::ranges::find(document_.edit().current().graph.screens,
                                       selection.front().id, &ui_content::flow_screen::id);
        node.asset = *asset_;
        tool_.update_screen(std::move(node));
    }
}

/// @brief Route graph and history commands after all Atlas callbacks have returned.
bool flow_panel::command(std::string_view action, document_session& ui)
{
    if (create_command(action, ui) || binding_command(action, ui) || choice_command(action))
    {
        return true;
    }

    if (action == "delete")
    {
        tool_.erase_selection();
    }
    else if (action == "save")
    {
        document_.save();
        status_ = "Flow draft saved";
    }
    else if (action == "undo")
    {
        (void)document_.edit().undo();
        route_.clear();
    }
    else if (action == "redo")
    {
        (void)document_.edit().redo();
        route_.clear();
    }
    else if (action == "recover")
    {
        (void)document_.recover();
        route_.clear();
    }
    else if (action == "discard")
    {
        document_.discard_recovery();
    }
    else if (action == "page")
    {
        ++page_;
    }
    else if (action == "zoom+" || action == "zoom-")
    {
        tool_.zoom_at(action == "zoom+" ? 1.25 : 0.8,
                      {graph_bounds_.width / 2, graph_bounds_.height / 2});
    }
    else if (action.starts_with("route:"))
    {
        route_ = action.substr(6);
        region_selection_.reset();
    }
    else if (action.starts_with("region:"))
    {
        region_selection_ = action.substr(7);
        region_ = *region_selection_;
        route_.clear();
        asset_.reset();
    }
    else if (action == "asset")
    {
        choose_asset();
    }
    else
    {
        return false;
    }

    return true;
}
} // namespace atlas::editor
