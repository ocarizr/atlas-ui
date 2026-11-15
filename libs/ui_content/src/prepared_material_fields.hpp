/** @file @brief Prepared v1 codecs for persistent references and material values. */
#pragma once
#include "prepared_binary.hpp"

namespace atlas::ui_content::detail
{
/// @brief Encode the namespace, ordinal and expected type without text or native handles.
inline constexpr auto reference = [](auto& format, assets::asset_ref& ref)
{
    for (auto& byte : ref.id.space.bytes)
    {
        format.field(byte);
    }

    format.field(ref.id.local);
    format.choice(ref.type, assets::asset_type::localization);
};

/// @brief The first and last v1 material slots delimit the frozen scalar payload.
inline constexpr auto first_material_slot = ui::property::material_red;
inline constexpr auto last_material_slot = ui::property::material_union; ///< Last v1 slot.

/// @brief Record only authoring values; shader keys and render resources are resolved later.
inline constexpr auto material_style_fields = [](auto& format, ui::material_style& style)
{
    format.choice(style.kind, ui::material_kind::shape);
    for (auto slot = static_cast<unsigned>(first_material_slot);
         slot <= static_cast<unsigned>(last_material_slot); ++slot)
    {
        format.field(ui::material_value(style, static_cast<ui::property>(slot)));
    }
};

/// @brief Pair an included material descriptor with its catalog identity.
inline constexpr auto material_record_fields = [](auto& format, prepared_material_record& record)
{
    reference(format, record.source);
    auto* base = std::get_if<material_template>(&record.value);
    bool is_template = base != nullptr;
    format.field(is_template);
    if (is_template)
    {
        if constexpr (std::remove_reference_t<decltype(format)>::reading)
        {
            base = &record.value.template emplace<material_template>();
        }

        material_style_fields(format, base->defaults);
        format.optional(base->shader, reference);
        format.optional(base->texture, reference);
        return;
    }

    if constexpr (std::remove_reference_t<decltype(format)>::reading)
    {
        record.value.template emplace<material_instance>();
    }

    auto& value = std::get<material_instance>(record.value);
    reference(format, value.source);
    format.optional(value.texture, reference);
    format.sequence(value.parameters, 21,
                    [](auto& archive, ui::material_override& entry)
                    {
                        archive.choice(entry.slot, last_material_slot);
                        archive.field(entry.value);
                    });
};
} // namespace atlas::ui_content::detail
