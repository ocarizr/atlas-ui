/** @file @brief Material descriptor invariants and canonical typed XML. */
#include "material_validation.hpp"
#include "xml_fields.hpp"
#include <bitset>

namespace atlas::ui_content
{
namespace
{
/// @brief Check identities in prepared descriptors using the same types as XML.
void require_type(assets::asset_ref value, assets::asset_type type)
{
    if (value.type != type)
    {
        throw std::invalid_argument("Incompatible material asset reference type");
    }

    (void)assets::format_id(value.id);
}

/// @brief Only the textured ABI has a sampled image input.
void validate_template(const material_template& entry)
{
    ui::validate_material(entry.defaults);
    if (entry.defaults.shader != 0 ||
        (entry.defaults.kind == ui::material_kind::texture) != entry.texture.has_value())
    {
        throw std::invalid_argument("Invalid material template texture or runtime shader binding");
    }

    if (entry.texture)
    {
        require_type(*entry.texture, assets::asset_type::texture);
    }

    if (entry.shader)
    {
        require_type(*entry.shader, assets::asset_type::shader);
    }
}

/// @brief Validate declarations before resolving the target template's stricter slot domains.
void validate_instance(const material_instance& entry)
{
    require_type(entry.source, assets::asset_type::material);
    if (entry.texture)
    {
        require_type(*entry.texture, assets::asset_type::texture);
    }

    std::bitset<256> seen;
    for (const auto parameter : entry.parameters)
    {
        if (!(ui::valid_material_slot(ui::material_kind::shape, parameter.slot, parameter.value) ||
              ui::valid_material_slot(ui::material_kind::texture, parameter.slot, parameter.value)))
        {
            throw std::invalid_argument("Invalid material override parameter");
        }

        const auto slot = static_cast<unsigned>(parameter.slot);
        if (seen.test(slot))
        {
            throw std::invalid_argument("Duplicate material override parameter");
        }

        seen.set(slot);
    }
}

/// @brief Traverse the shared fixed slot order regardless of input declaration order.
void parameters(detail::fields& xml, std::vector<ui::material_override>& values)
{
    for (auto index = static_cast<unsigned>(ui::property::material_red);
         index <= static_cast<unsigned>(ui::property::material_union); ++index)
    {
        const auto slot = static_cast<ui::property>(index);
        const auto* name = ui::material_slot_name(slot);
        if (xml.reading())
        {
            const bool present = static_cast<bool>(xml.node().attribute(name));
            float value = 0;
            xml.value(name, value);
            if (present)
            {
                values.push_back({slot, value});
            }
        }
        else if (const auto found = std::ranges::find(values, slot, &ui::material_override::slot);
                 found != values.end())
        {
            xml.value(name, found->value);
        }
    }
}

/// @brief Encode program defaults through the exact same typed slots as sparse overrides.
void template_fields(detail::fields& xml, material_template& entry)
{
    if (xml.reading() && !xml.node().attribute("kind"))
    {
        throw std::invalid_argument("Material template requires a shader kind");
    }

    xml.choice("kind", entry.defaults.kind, std::array{"texture", "shape"});
    xml.reference("shader", entry.shader, assets::asset_type::shader);
    xml.reference("texture", entry.texture, assets::asset_type::texture);
    std::vector<ui::material_override> slots;
    if (!xml.reading())
    {
        ui::material_style builtin;
        for (auto index = static_cast<unsigned>(ui::property::material_red);
             index <= static_cast<unsigned>(ui::property::material_union); ++index)
        {
            const auto slot = static_cast<ui::property>(index);
            const auto value = ui::material_value(entry.defaults, slot);
            if (value != ui::material_value(builtin, slot))
            {
                slots.push_back({slot, value});
            }
        }
    }

    parameters(xml, slots);
    if (xml.reading())
    {
        entry.defaults = ui::resolve_material(entry.defaults, slots);
    }
}

/// @brief Keep source typing and explicitly authored replacements in both directions.
void instance_fields(detail::fields& xml, material_instance& entry)
{
    xml.reference("template", entry.source, assets::asset_type::material);
    xml.reference("texture", entry.texture, assets::asset_type::texture);
    parameters(xml, entry.parameters);
}
} // namespace

/// @brief Validate prepared or parsed descriptors consistently.
void detail::validate_material_document(const material_document& source)
{
    if (const auto* entry = std::get_if<material_template>(&source))
    {
        validate_template(*entry);
    }
    else
    {
        validate_instance(std::get<material_instance>(source));
    }
}

/// @brief Accept exactly one versioned attribute-only material declaration.
material_document parse_material_xml(xml_source source, const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"ui.material.parse", "ui.content"}};
    try
    {
        pugi::xml_document xml;
        const auto root = detail::read_root(xml, source.bytes, detail::max_material_bytes);
        detail::fields fields{root, true};
        std::string version;
        fields.value("version", version, true);
        if (version != "1")
        {
            throw std::invalid_argument("Unsupported UI material version");
        }

        material_document result;
        const std::string_view kind = root.name();
        if (kind == "material-template")
        {
            template_fields(fields, std::get<material_template>(result));
        }
        else if (kind == "material-override")
        {
            instance_fields(fields, result.emplace<material_instance>());
        }
        else
        {
            throw std::invalid_argument("Expected a material template or override");
        }

        fields.finish();
        detail::expect_empty(root);
        detail::validate_material_document(result);
        return result;
    }
    catch (const std::invalid_argument& error)
    {
        throw std::invalid_argument(std::string{source.name} + ": " + error.what());
    }
}

/// @brief Serialize the selected authoring form, preserving sparse override presence.
std::string write_material_xml(const material_document& source, const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"ui.material.save", "ui.content"}};
    detail::validate_material_document(source);
    auto entry = source;
    pugi::xml_document xml;
    const bool templated = std::holds_alternative<material_template>(entry);
    auto root = xml.append_child(templated ? "material-template" : "material-override");
    root.append_attribute("version").set_value("1");
    detail::fields fields{root, false};
    if (auto* base = std::get_if<material_template>(&entry))
    {
        template_fields(fields, *base);
    }
    else
    {
        instance_fields(fields, std::get<material_instance>(entry));
    }

    /// @brief Store the bounded fixed-schema XML in an owned string.
    struct writer final : pugi::xml_writer
    {
        std::string bytes; ///< Owned canonical UTF-8.

        void write(const void* data, std::size_t size) override
        {
            bytes.append(static_cast<const char*>(data), size);
        } ///< Append one serialized chunk.
    } result;

    xml.save(result, "  ", pugi::format_default, pugi::encoding_utf8);
    return std::move(result.bytes);
}
} // namespace atlas::ui_content
