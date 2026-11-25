/** @file @brief Stable navigation attribute names shared by canonical reader and writer. */
#include "navigation_content.hpp"

namespace atlas::ui_content::detail
{
namespace
{
/// @brief Semantic IDs remain separate from physical device/control assignments.
void action_fields(fields& xml, ui::focus_navigation_actions& actions)
{
    constexpr std::array names{"left-action", "right-action", "up-action",
                               "down-action", "next-action",  "previous-action"};
    const std::array values{&actions.left, &actions.right, &actions.up,
                            &actions.down, &actions.next,  &actions.previous};
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        xml.value(names[i], *values[i]);
    }
}
} // namespace

/// @brief An absent target is automatic; blocking is explicit and cannot also name a target.
void focus_fields(fields& xml, focus_navigation& data)
{
    constexpr std::array targets{"left-target", "right-target", "up-target",
                                 "down-target", "next-target",  "previous-target"};
    constexpr std::array blocked{"left-blocked", "right-blocked", "up-blocked",
                                 "down-blocked", "next-blocked",  "previous-blocked"};
    for (std::size_t i = 0; i < targets.size(); ++i)
    {
        xml.value(targets[i], data.destinations[i]);
        xml.value(blocked[i], data.blocked[i]);
    }

    xml.value("tab-order", data.tab_order);
    xml.value("tab-stop", data.tab_stop);
}

/// @brief Wrapping defaults off independently on every axis and ordered traversal.
void focus_scope_fields(fields& xml, ui::focus_navigation_scope& data)
{
    action_fields(xml, data.actions);
    xml.value("wrap-horizontal", data.wrap_horizontal);
    xml.value("wrap-vertical", data.wrap_vertical);
    xml.value("wrap-tab", data.wrap_tab);
}

/// @brief No new physical-key vocabulary is embedded in authored navigation data.
void navigation_input_fields(fields& xml, navigation_input& data)
{
    action_fields(xml, data.actions);
    xml.value("keyboard", data.keyboard);
}
} // namespace atlas::ui_content::detail
