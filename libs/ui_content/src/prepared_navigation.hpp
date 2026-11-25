/** @file @brief Version-eight optional navigation descriptors appended after legacy fields. */
#pragma once
#include "prepared_binary.hpp"

namespace atlas::ui_content::detail
{
/// @brief Fixed directional order matches the public portable direction vocabulary.
template <class Archive>
void prepared_focus_actions(Archive& format, ui::focus_navigation_actions& data)
{
    format.fields(data.left, data.right, data.up, data.down, data.next, data.previous);
}

/// @brief Preserve declared IDs; resolve transient handles only during instance construction.
template <class Archive> void prepared_focus(Archive& format, focus_navigation& data)
{
    for (auto& id : data.destinations)
    {
        format.field(id);
    }

    for (auto& blocked : data.blocked)
    {
        format.field(blocked);
    }

    format.fields(data.tab_order, data.tab_stop);
}

/// @brief Group metadata stays independent of geometry and device SDKs.
template <class Archive>
void prepared_focus_scope(Archive& format, ui::focus_navigation_scope& data)
{
    prepared_focus_actions(format, data.actions);
    format.fields(data.wrap_horizontal, data.wrap_vertical, data.wrap_tab);
}

/// @brief Presence is explicit so old documents keep keyboard defaults unchanged.
template <class Archive> void prepared_navigation(Archive& format, document& source)
{
    format.optional(source.focus_input,
                    [](auto& archive, navigation_input& data)
                    {
                        prepared_focus_actions(archive, data.actions);
                        archive.field(data.keyboard);
                    });
    for (auto& node : source.elements)
    {
        format.optional(node.focus, prepared_focus<Archive>);
        format.optional(node.focus_scope, prepared_focus_scope<Archive>);
    }
}
} // namespace atlas::ui_content::detail
