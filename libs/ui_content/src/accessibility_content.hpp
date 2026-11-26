/** @file @brief Shared semantic XML/prepared schema and construction defaults. */
#pragma once
#include "xml_fields.hpp"

namespace atlas::ui_content::detail
{
void accessibility_fields(fields& xml, ui::accessible_element& data); ///< Attribute-only schema.
void configure_accessibility(ui::input_session& input, ui::element_handle target,
                             const element& node); ///< Known defaults and overrides.

/// @brief Append v9 metadata without serializing live identities or resolved localized strings.
template <class Archive> void prepared_accessibility(Archive& format, document& source)
{
    for (auto& node : source.elements)
    {
        format.field(node.decorative);
        format.optional(node.accessible,
                        [](auto& archive, ui::accessible_element& data)
                        {
                            archive.choice(data.role, accessibility::role::scroll_container);
                            archive.fields(data.name, data.description, data.name_key,
                                           data.description_key, data.value, data.invoke_action,
                                           data.select_action, data.selected, data.number,
                                           data.minimum, data.maximum, data.checked);
                        });
    }
}
} // namespace atlas::ui_content::detail
