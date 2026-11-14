/** @file @brief Closed slot vocabulary and validated sparse material overrides. */
#include <atlas/ui/material.hpp>
#include <bitset>
#include <cmath>

namespace atlas::ui
{
/// @brief Keep the semantic names in static storage for the XML codec.
const char* material_slot_name(property field)
{
    constexpr std::array material_names{
        "material-red",       "material-green",     "material-blue",      "material-alpha",
        "material-radius-tl", "material-radius-tr", "material-radius-bl", "material-radius-br",
        "material-bevel",     "material-scale",     "material-morph",     "material-outline",
        "material-glow",      "material-reveal",    "material-gradient",  "material-phase",
        "material-u-offset",  "material-v-offset",  "material-u-scale",   "material-v-scale",
        "material-union",
    };
    return material_names.at(static_cast<unsigned>(field) -
                             static_cast<unsigned>(property::material_red));
}

/// @brief Normalize numeric domains independently of pixel program compatibility.
bool valid_material_slot(material_kind kind, property field, float value) noexcept
{
    if (kind > material_kind::shape || !material_slot(field) || !std::isfinite(value))
    {
        return false;
    }

    if (field <= property::material_alpha)
    {
        return value >= 0 && value <= 1;
    }

    if (field >= property::material_u_offset && field <= property::material_v_scale)
    {
        if (kind != material_kind::texture)
        {
            return false;
        }

        return field <= property::material_v_offset ? std::abs(value) <= 2
                                                    : value >= 0 && value <= 2;
    }

    return kind == material_kind::shape && value >= 0 &&
           value <= (field == property::material_scale ? 2.f : 1.f);
}

/// @brief Resolve the typed command to an instance value without allocating storage.
float& material_value(material_style& value, property field)
{
    switch (field)
    {
        case property::material_red:
            return value.tint[0];
        case property::material_green:
            return value.tint[1];
        case property::material_blue:
            return value.tint[2];
        case property::material_alpha:
            return value.tint[3];
        case property::material_radius_tl:
            return value.radius[0];
        case property::material_radius_tr:
            return value.radius[1];
        case property::material_radius_bl:
            return value.radius[2];
        case property::material_radius_br:
            return value.radius[3];
        case property::material_bevel:
            return value.bevel;
        case property::material_scale:
            return value.scale;
        case property::material_morph:
            return value.morph;
        case property::material_outline:
            return value.outline;
        case property::material_glow:
            return value.glow;
        case property::material_reveal:
            return value.reveal;
        case property::material_gradient:
            return value.gradient;
        case property::material_phase:
            return value.phase;
        case property::material_u_offset:
            return value.uv_offset[0];
        case property::material_v_offset:
            return value.uv_offset[1];
        case property::material_u_scale:
            return value.uv_scale[0];
        case property::material_v_scale:
            return value.uv_scale[1];
        case property::material_union:
            return value.unite;
        default:
            throw std::invalid_argument("Unknown UI material slot");
    }
}

/// @brief Validate the active program and require default values for its unused slots.
void validate_material(const material_style& value)
{
    if (value.kind > material_kind::shape)
    {
        throw std::invalid_argument("Unknown UI material kind");
    }

    auto candidate = value;
    material_style defaults;
    for (auto index = static_cast<unsigned>(property::material_red);
         index <= static_cast<unsigned>(property::material_union); ++index)
    {
        const auto slot = static_cast<property>(index);
        const auto sample = material_value(candidate, slot);
        if (!valid_material_slot(value.kind, slot, sample) &&
            sample != material_value(defaults, slot))
        {
            throw std::invalid_argument("Invalid UI material value or incompatible slot");
        }
    }
}

/// @brief A failed override list cannot change template defaults or publish partial values.
material_style resolve_material(const material_style& defaults,
                                std::span<const material_override> overrides)
{
    validate_material(defaults);
    auto result = defaults;
    std::bitset<256> written;
    for (const auto& item : overrides)
    {
        const auto index = static_cast<unsigned>(item.slot);
        if (!valid_material_slot(defaults.kind, item.slot, item.value) || written.test(index))
        {
            throw std::invalid_argument("Invalid or duplicate UI material override");
        }

        written.set(index);
        material_value(result, item.slot) = item.value;
    }

    return result;
}
} // namespace atlas::ui
