/**
 * @file
 * @brief Typed parameters for the built-in viewport material programs.
 */
#pragma once
#include <atlas/ui/commands.hpp>

namespace atlas::ui
{
/// @brief Select the pixel program independently of layout and texture residency.
enum class material_kind : std::uint8_t
{
    texture, ///< Tint and transform an existing sampled image.
    shape    ///< Paint a procedural signed-distance shape inside the existing border.
};

/// @brief Validated material defaults or resolved per-element values.
struct material_style
{
    material_kind kind = material_kind::texture; ///< Pixel program identity.
    std::array<float, 4> tint{1, 1, 1, 1};       ///< Straight-alpha linear RGBA multiplier.
    std::array<float, 4> radius{};       ///< Top-left, top-right, bottom-left, bottom-right.
    float bevel = 0;                     ///< Mix each rounded corner into its chamfer.
    float scale = 1;                     ///< Shape-local scale about its center, up to two.
    float morph = 0;                     ///< Interpolate rectangle and ellipse distances.
    float outline = 0;                   ///< Inner outline width in normalized shape coordinates.
    float glow = 0;                      ///< Outside falloff strength.
    float reveal = 1;                    ///< Left-to-right coverage inside the quad.
    float gradient = 0;                  ///< Darken the bottom of the fill.
    float phase = 0;                     ///< Explicit normalized cosmetic loop time.
    std::array<float, 2> uv_offset{};    ///< Signed texture-coordinate offsets.
    std::array<float, 2> uv_scale{1, 1}; ///< Texture-coordinate multipliers.
    float unite = 0; ///< Blend toward the union of the rectangle and an offset ellipse.
    bool operator==(const material_style&) const = default; ///< Compare complete instance data.
};

/// @brief One sparse typed override against a material's defaults.
struct material_override
{
    property slot; ///< Stable material parameter identity.
    float value;   ///< Value in the slot's domain.
};

/// @brief Determine whether a command targets a material slot.
[[nodiscard]] constexpr bool material_slot(property field) noexcept
{
    return field >= property::material_red && field <= property::material_union;
}

/// @brief Check the slot's value range and compatibility with its pixel program.
[[nodiscard]] bool valid_material_slot(material_kind kind, property field, float value) noexcept;
/// @brief Return the stable XML and diagnostic name of a slot.
[[nodiscard]] const char* material_slot_name(property field);
/// @brief Reject malformed instance data before storing or uploading it.
void validate_material(const material_style& value);
/// @brief Copy a template and apply a unique set of sparse overrides.
[[nodiscard]] material_style resolve_material(const material_style& defaults,
                                              std::span<const material_override> overrides);
/// @brief Select a validated typed slot for owner mutation.
[[nodiscard]] float& material_value(material_style& value, property field);
} // namespace atlas::ui
