/** @file @brief Strict nested XML track declarations and sparse cell metadata. */
#include "xml_layout.hpp"

namespace atlas::ui_content::detail
{
namespace
{
/// @brief Empty axis declarations intentionally select automatic growth along that axis.
void read_tracks(pugi::xml_node node, std::vector<ui::grid_track>& tracks)
{
    fields header{node, true};
    header.finish();
    tracks.clear();
    for (auto child : node.children())
    {
        if (child.type() == pugi::node_comment)
        {
            continue;
        }

        if (child.type() != pugi::node_element || std::string_view{child.name()} != "track")
        {
            throw std::invalid_argument("expected grid track declaration");
        }

        if (tracks.size() == 65536)
        {
            throw std::overflow_error("grid track admission exhausted");
        }

        ui::grid_track track;
        fields xml{child, true};
        xml.choice("unit", track.unit, std::array{"auto", "fixed", "fraction"});
        xml.value("value", track.value);
        xml.finish();
        expect_empty(child);
        tracks.push_back(track);
    }
}

/// @brief Track order is stable and shared by XML and prepared construction.
void write_tracks(pugi::xml_node node, const char* name, const std::vector<ui::grid_track>& tracks)
{
    auto axis = node.append_child(name);
    for (auto track : tracks)
    {
        fields xml{axis.append_child("track"), false};
        xml.choice("unit", track.unit, std::array{"auto", "fixed", "fraction"});
        xml.value("value", track.value);
    }
}
} // namespace

/// @brief A grid section is valid only on a grid panel, with at most one section per axis.
void read_grid(pugi::xml_node node, element& target)
{
    if (!std::holds_alternative<ui::grid_layout>(target.panel))
    {
        throw std::invalid_argument("grid tracks require a grid panel");
    }

    ui::grid_definition definition;
    fields xml{node, true};
    xml.value("column-gap", definition.column_gap);
    xml.value("row-gap", definition.row_gap);
    xml.finish();
    bool rows = false;
    bool columns = false;
    for (auto child : node.children())
    {
        if (child.type() == pugi::node_comment)
        {
            continue;
        }

        const std::string_view name = child.name();
        if (child.type() != pugi::node_element)
        {
            throw std::invalid_argument("invalid grid content");
        }

        if (name == "rows" && !rows)
        {
            rows = true;
            read_tracks(child, definition.rows);
        }
        else if (name == "columns" && !columns)
        {
            columns = true;
            read_tracks(child, definition.columns);
        }
        else
        {
            throw std::invalid_argument("unknown or duplicate grid track axis");
        }
    }

    target.panel = ui::grid_layout{std::move(definition)};
}

/// @brief Persist the exact explicit/implicit axis distinction, including empty axis elements.
void write_grid(pugi::xml_node node, const ui::grid_layout& grid)
{
    const auto& definition = grid.definition();
    auto section = node.append_child("grid");
    auto copy = definition;
    fields xml{section, false};
    xml.value("column-gap", copy.column_gap);
    xml.value("row-gap", copy.row_gap);
    write_tracks(section, "columns", definition.columns);
    write_tracks(section, "rows", definition.rows);
}

/// @brief Position is either a complete pair or absent; spans remain independently configurable.
void grid_fields(fields& xml, ui::grid_slot& slot)
{
    auto position = slot.position.value_or(ui::grid_cell{});
    const bool row = static_cast<bool>(xml.node().attribute("row"));
    const bool column = static_cast<bool>(xml.node().attribute("column"));
    if (xml.reading() && row != column)
    {
        throw std::invalid_argument("grid position needs row and column");
    }

    xml.optional("row", position.row, slot.position.has_value());
    xml.optional("column", position.column, slot.position.has_value());
    if (xml.reading() && row)
    {
        slot.position = position;
    }

    xml.value("row-span", slot.row_span);
    xml.value("column-span", slot.column_span);
}
} // namespace atlas::ui_content::detail
