/** @file @brief Strict UI root schema for typed presentations and popup defaults. */
#include "presentation_xml.hpp"

namespace atlas::ui_content::detail
{
/// @brief Reject presentation options on legacy or incompatible UI roots.
presentation_options read_presentation(pugi::xml_node root)
{
    fields header{root, true};
    std::string version;
    header.value("version", version, true);
    presentation_options result;
    if (version == "2")
    {
        std::string kind;
        header.value("kind", kind, true);
        result.type = assets::parse_type(kind);
        if (result.type == assets::asset_type::ui)
        {
            throw std::invalid_argument("UI v2 requires an explicit presentation kind");
        }

        if (result.type == assets::asset_type::popup)
        {
            header.value("modal", result.popup.modal);
            header.value("dismiss-outside", result.popup.dismiss_outside);
            header.value("dismiss-back", result.popup.dismiss_back);
        }
    }
    else if (version != "1")
    {
        throw std::invalid_argument("Unsupported UI XML version: " + version);
    }

    header.finish();
    validate_presentation(result);
    return result;
}

/// @brief Persist only authored options; no live focus or view state enters the source.
void write_presentation(pugi::xml_node root, presentation_options source)
{
    validate_presentation(source);
    const bool legacy = source.type == assets::asset_type::ui;
    root.append_attribute("version").set_value(legacy ? "1" : "2");
    if (!legacy)
    {
        root.append_attribute("kind").set_value(
            std::string{assets::format_type(source.type)}.c_str());
    }

    if (source.type == assets::asset_type::popup)
    {
        fields header{root, false};
        header.value("modal", source.popup.modal);
        header.value("dismiss-outside", source.popup.dismiss_outside);
        header.value("dismiss-back", source.popup.dismiss_back);
    }
}
} // namespace atlas::ui_content::detail
