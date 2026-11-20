/** @file @brief Worker-safe content inspection without storage or live UI mutation. */
#include "asset_analysis.hpp"
#include <algorithm>
#include <atlas/mvvm_content/document.hpp>
#include <atlas/ui_content/flow.hpp>
#include <atlas/ui_content/material.hpp>

namespace atlas::editor
{
/// @brief Canonical typed dependencies include both named models and sparse element references.
std::vector<assets::asset_ref> asset_references(const ui_content::document& source)
{
    std::vector<assets::asset_ref> refs;
    refs.reserve(source.models.size() + source.elements.size() * 3);
    for (const auto& model : source.models)
    {
        refs.push_back(model.source);
    }

    for (const auto& element : source.elements)
    {
        if (element.texture)
        {
            refs.push_back(*element.texture);
        }

        if (element.material)
        {
            refs.push_back(*element.material);
        }

        if (element.script)
        {
            refs.push_back(element.script->source);
        }
    }

    std::ranges::sort(refs);
    refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
    return refs;
}

/// @brief Parse only supported authored formats; binary and script assets keep their raw contract.
std::vector<assets::asset_ref> inspect_asset(assets::asset_type type,
                                             std::span<const std::uint8_t> bytes)
{
    const std::string_view text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    std::vector<assets::asset_ref> refs;
    if (assets::is_ui(type))
    {
        const auto source = ui_content::parse_xml({text});
        ui_content::validate_presentation(source.presentation, type);
        return asset_references(source);
    }

    if (type == assets::asset_type::view_model)
    {
        (void)mvvm_content::parse_xml(text);
    }

    if (type == assets::asset_type::material)
    {
        const auto source = ui_content::parse_material_xml({text});
        if (const auto* value = std::get_if<ui_content::material_template>(&source))
        {
            if (value->shader)
            {
                refs.push_back(*value->shader);
            }

            if (value->texture)
            {
                refs.push_back(*value->texture);
            }
        }
        else
        {
            const auto& instance = std::get<ui_content::material_instance>(source);
            refs.push_back(instance.source);
            if (instance.texture)
            {
                refs.push_back(*instance.texture);
            }
        }
    }

    if (type == assets::asset_type::flow)
    {
        for (const auto& screen : ui_content::parse_flow_xml({text}).screens)
        {
            refs.push_back(screen.asset);
        }
    }

    std::ranges::sort(refs);
    refs.erase(std::unique(refs.begin(), refs.end()), refs.end());
    return refs;
}
} // namespace atlas::editor
