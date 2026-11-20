/** @file @brief Draft declarations preserve incomplete references and optional popup overrides. */
#include "xml_fields.hpp"

namespace atlas::editor_flow::detail
{
/// @brief Empty initial/owner/host references are serializable while a region is being authored.
void region_fields(fields& codec, ui_content::flow_region& value)
{
    codec.value("id", value.id);
    codec.value("initial", value.initial);
    codec.value("owner", value.owner);
    codec.value("host", value.host);
    codec.value("remember", value.remember);
}

/// @brief A missing asset is explicit draft state rather than a fabricated valid asset identity.
void screen_fields(fields& codec, ui_content::flow_screen& value)
{
    codec.value("id", value.id);
    codec.value("region", value.region);
    codec.value("focus", value.focus);
    std::string asset;
    auto type = std::string{assets::format_type(value.asset.type)};
    if (!codec.reading() && value.asset.id != assets::asset_id{})
    {
        asset = assets::format_id(value.asset.id);
    }

    codec.value("asset", asset);
    codec.value("asset-type", type);
    if (codec.reading())
    {
        value.asset = {asset.empty() ? assets::asset_id{} : assets::parse_id(asset),
                       assets::parse_type(type)};
    }

    codec.optional_bool("modal", value.popup.modal);
    codec.optional_bool("dismiss-outside", value.popup.dismiss_outside);
    codec.optional_bool("dismiss-back", value.popup.dismiss_back);
}

/// @brief Preserve the existing runtime parameter vocabulary without resolving its use sites.
void parameter_fields(fields& codec, ui_content::flow_parameter& value)
{
    codec.value("id", value.id);
    scalar_fields(codec, value.value);
}

/// @brief Outgoing equality guards remain optional complete typed records.
void transition_fields(fields& codec, ui_content::flow_transition& value)
{
    codec.value("id", value.id);
    codec.value("from", value.from);
    codec.value("to", value.to);
    codec.value("enabled", value.enabled);
    codec.value("control", value.control);
    codec.value("action", value.action);
    auto operation = static_cast<unsigned>(value.operation);
    codec.value("operation", operation);
    if (operation > static_cast<unsigned>(ui_content::flow_operation::close_popup))
    {
        throw std::invalid_argument("Unknown Flow draft operation");
    }

    value.operation = static_cast<ui_content::flow_operation>(operation);
    if (codec.reading())
    {
        for (const auto node : codec.node().children())
        {
            if (node.type() == pugi::node_comment)
            {
                continue;
            }

            if (node.type() != pugi::node_element || std::string_view{node.name()} != "guard" ||
                value.guard)
            {
                throw std::invalid_argument("Invalid Flow draft transition content");
            }

            fields child{node, true};
            auto& guard = value.guard.emplace();
            child.value("parameter", guard.parameter);
            scalar_fields(child, guard.equals);
            child.finish();
            empty(node);
        }
    }
    else if (value.guard)
    {
        fields child{codec.node().append_child("guard"), false};
        child.value("parameter", value.guard->parameter);
        scalar_fields(child, value.guard->equals);
    }
}

/// @brief Layout and presentation metadata reference stable declaration identities.
void layout_fields(fields& codec, item_layout& value)
{
    reference_fields(codec, value.item);
    codec.value("label", value.label);
    codec.value("x", value.x);
    codec.value("y", value.y);
}
} // namespace atlas::editor_flow::detail
