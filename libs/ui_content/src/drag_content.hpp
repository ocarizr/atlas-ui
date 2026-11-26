/** @file @brief Shared XML/prepared drag metadata and runtime configuration. */
#pragma once
#include "xml_fields.hpp"

namespace atlas::ui_content::detail
{
void drag_source_fields(fields& xml, drag_source_definition& data);  ///< Attribute-only source.
void drop_target_fields(fields& xml, ui::drop_target_options& data); ///< Exact accepted type keys.
void validate_drag(const element& node); ///< Pure validation before external asset resolution.
void configure_drag(ui::input_session& input, ui::element_handle target,
                    const element& node); ///< Private candidate wiring.

/// @brief Append version 10 policy without serializing live handles or pending gestures.
template <class Archive> void prepared_drag(Archive& format, document& source)
{
    for (auto& node : source.elements)
    {
        format.optional(node.drag_source,
                        [](auto& archive, drag_source_definition& value)
                        {
                            archive.fields(value.type, value.text, value.threshold);
                            archive.choice(value.operations, ui::drag_operations::copy_move);
                        });
        format.optional(node.drop_target,
                        [](auto& archive, ui::drop_target_options& value)
                        {
                            archive.sequence(value.types, 32,
                                             [](auto& codec, std::string& type)
                                             {
                                                 codec.field(type);
                                             });
                            archive.choice(value.operations, ui::drag_operations::copy_move);
                        });
    }
}
} // namespace atlas::ui_content::detail
