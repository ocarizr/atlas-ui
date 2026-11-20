/** @file @brief Graph document validation and explicit runtime import boundary. */
#include "document_state.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace atlas::editor_flow
{
namespace
{
/// @brief Runtime-compatible identity spelling, with empty references allowed only in drafts.
void identity(std::string_view value, bool optional = false)
{
    if (optional && value.empty())
    {
        return;
    }

    if (value.empty() || value.size() > 64 ||
        !std::ranges::all_of(value,
                             [](char ch)
                             {
                                 return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                                        (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
                             }))
    {
        throw std::invalid_argument("Invalid Flow draft identity");
    }
}

/// @brief Bound exact scalar values without resolving their runtime dependencies.
void scalar(const mvvm::value& value)
{
    if (const auto* text = std::get_if<std::string>(&value))
    {
        mvvm::validate_text(*text, 4096);
    }

    if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
    {
        throw std::invalid_argument("Nonfinite Flow draft scalar");
    }
}

/// @brief Every declaration must have a unique stable ID even while its references are unresolved.
template <class T> void identities(const std::vector<T>& records)
{
    std::set<std::string_view> ids;
    for (const auto& item : records)
    {
        identity(item.id);
        if (!ids.insert(item.id).second)
        {
            throw std::invalid_argument("Duplicate Flow draft identity");
        }
    }
}
} // namespace

/// @brief Selection and layout refer to actual declarations rather than dangling runtime links.
bool detail::contains(const document& source, const item_ref& item)
{
    switch (item.kind)
    {
        case item_kind::none:
            return item.id.empty();
        case item_kind::region:
            return std::ranges::find(source.graph.regions, item.id, &ui_content::flow_region::id) !=
                   source.graph.regions.end();
        case item_kind::screen:
            return std::ranges::find(source.graph.screens, item.id, &ui_content::flow_screen::id) !=
                   source.graph.screens.end();
        case item_kind::transition:
            return std::ranges::find(source.graph.transitions, item.id,
                                     &ui_content::flow_transition::id) !=
                   source.graph.transitions.end();
    }

    return false;
}

/// @brief Enforce structural authoring invariants without requiring a complete runtime graph.
void validate(const document& source)
{
    identity(source.id);
    const auto& graph = source.graph;
    if (graph.regions.size() > 64 || graph.screens.size() > 256 ||
        graph.transitions.size() > 1024 || graph.parameters.size() > 64 ||
        source.layout.size() > 1344)
    {
        throw std::length_error("Flow draft declaration budget exhausted");
    }

    identities(graph.regions);
    identities(graph.screens);
    identities(graph.transitions);
    identities(graph.parameters);
    for (const auto& item : graph.regions)
    {
        identity(item.initial, true);
        identity(item.owner, true);
        identity(item.host, true);
    }

    for (const auto& item : graph.screens)
    {
        identity(item.region, true);
        identity(item.focus, true);
        if (!assets::is_ui(item.asset.type))
        {
            throw std::invalid_argument("Flow screen requires a UI asset type");
        }

        if (item.asset.id != assets::asset_id{})
        {
            (void)assets::format_id(item.asset.id);
        }
    }

    for (const auto& item : graph.parameters)
    {
        scalar(item.value);
    }

    for (const auto& item : graph.transitions)
    {
        identity(item.from, true);
        identity(item.to, true);
        identity(item.control, true);
        if (item.operation > ui_content::flow_operation::close_popup)
        {
            throw std::invalid_argument("Invalid Flow draft operation");
        }

        if (item.guard)
        {
            identity(item.guard->parameter, true);
            scalar(item.guard->equals);
        }
    }

    std::set<std::pair<item_kind, std::string>> metadata;
    for (const auto& item : source.layout)
    {
        mvvm::validate_text(item.label, 1024);
        if (item.item.kind == item_kind::none || !detail::contains(source, item.item) ||
            !metadata.emplace(item.item.kind, item.item.id).second || !std::isfinite(item.x) ||
            !std::isfinite(item.y) || std::abs(item.x) > 1000000 || std::abs(item.y) > 1000000)
        {
            throw std::invalid_argument("Invalid Flow draft layout metadata");
        }
    }

    if (!detail::contains(source, source.selection))
    {
        throw std::invalid_argument("Invalid Flow draft selection");
    }
}

/// @brief Runtime loading remains strict and separate from incomplete authoring XML.
document import_runtime(std::string id, ui_content::xml_source source)
{
    document result{std::move(id), ui_content::parse_flow_xml(source), {}, {}};
    for (const auto& screen : result.graph.screens)
    {
        result.layout.push_back({{item_kind::screen, screen.id}, screen.id, 0, 0});
    }

    validate(result);
    return result;
}
} // namespace atlas::editor_flow
