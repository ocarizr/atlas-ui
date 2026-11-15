/**
 * @file
 * @brief Persistent viewport material templates and sparse overrides.
 */
#pragma once
#include <atlas/ui_content/document.hpp>
#include <variant>

namespace atlas::ui_content
{
/// @brief Directly usable shader pairing and defaults shared by many elements.
struct material_template
{
    ui::material_style defaults{}; ///< Typed scalar and vector channels in the fixed UI ABI.
    std::optional<assets::asset_ref>
        shader{}; ///< Optional custom HLSL; absence selects the builtin.
    std::optional<assets::asset_ref> texture{}; ///< Required image default for the texture program.
};

/// @brief Inherit directly from one template and replace only fields explicitly authored.
struct material_instance
{
    assets::asset_ref source;                      ///< A material template, never another override.
    std::vector<ui::material_override> parameters; ///< Explicit slot replacements.
    std::optional<assets::asset_ref> texture;      ///< Optional replacement of the default image.
};

using material_document = std::variant<material_template, material_instance>; ///< XML v1 values.

/// @brief Parse a strict, versioned material template or override without storage access.
[[nodiscard]] material_document parse_material_xml(xml_source source,
                                                   const diagnostics::services& sinks = {});
/// @brief Produce canonical XML while retaining explicitly authored override values.
[[nodiscard]] std::string write_material_xml(const material_document& source,
                                             const diagnostics::services& sinks = {});

/// @brief Authored revision prepared by the host's native or binary construction path.
struct prepared_material
{
    assets::asset_ref source;                 ///< Persistent material asset.
    std::uint64_t revision = 0;               ///< Exact mounted authored revision.
    const material_document* value = nullptr; ///< Borrowed during construction only.
};

/// @brief Owned shader payload suitable for passing to the graphics coordinator.
struct material_shader_source
{
    std::uint64_t key = 0; ///< Runtime binding key copied into immutable paint packets.
    ui::material_kind kind = ui::material_kind::texture; ///< Texture or shape pixel ABI.
    std::string hlsl; ///< Trusted UTF-8 source acquired through the typed streamer.
};
} // namespace atlas::ui_content
