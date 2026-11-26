/** @file @brief Semantic authoring fields and conservative known-control inference. */
#include "accessibility_content.hpp"

namespace atlas::ui_content::detail
{
/// @brief Names use literals or localization keys; actions use existing semantic input identities.
void accessibility_fields(fields& xml, ui::accessible_element& data)
{
    xml.choice("role", data.role,
               std::array{"group", "text", "image", "button", "tab-group", "tab", "list",
                          "list-item", "checkbox", "slider", "scroll-container"});
    xml.value("name", data.name);
    xml.value("description", data.description);
    xml.value("name-key", data.name_key);
    xml.value("description-key", data.description_key);
    xml.value("value", data.value);
    xml.value("invoke-action", data.invoke_action);
    xml.value("select-action", data.select_action);
    xml.value("selected", data.selected);
    xml.value("checked", data.checked);
    xml.value("number", data.number);
    xml.value("minimum", data.minimum);
    xml.value("maximum", data.maximum);
}

/// @brief A caption is text; a known authored activation is a button, never inferred from focus.
void configure_accessibility(ui::input_session& input, ui::element_handle target,
                             const element& node)
{
    if (node.decorative)
    {
        return;
    }

    if (node.accessible)
    {
        input.configure_accessibility(target, *node.accessible);
        return;
    }

    ui::accessible_element metadata{.role = accessibility::role::text, .name = node.caption};
    for (const auto& action : node.actions)
    {
        if (action.binding.event == ui::event_kind::activate &&
            action.binding.route == ui::route_phase::target)
        {
            metadata.role = accessibility::role::button;
        }
    }

    for (const auto& event : node.events)
    {
        if (event.event == authored_event::click)
        {
            metadata.role = accessibility::role::button;
            if (!metadata.invoke_action)
            {
                metadata.invoke_action = event.input_action;
            }
        }
    }

    if (!metadata.name.empty() || metadata.role == accessibility::role::button)
    {
        input.configure_accessibility(target, std::move(metadata));
    }
}
} // namespace atlas::ui_content::detail
