/** @file @brief Bounded sRGB authoring colors with strict XML schema validation. */
#include "document_validation.hpp"
#include "xml_fields.hpp"
#include <atlas/ui_content/palette.hpp>
#include <cmath>

namespace atlas::ui_content
{
/// @brief Convert sRGB RGB channels once; alpha is linear and remains unpremultiplied.
ui::rgba parse_color(std::string_view value)
{
    if (value.size() != 9 || value.front() != '#')
    {
        throw std::invalid_argument("Palette color requires #RRGGBBAA");
    }

    ui::rgba result{};
    for (std::size_t i = 0; i < result.size(); ++i)
    {
        unsigned byte = 0;
        const auto channel = value.substr(1 + i * 2, 2);
        const auto parsed =
            std::from_chars(channel.data(), channel.data() + channel.size(), byte, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != channel.data() + channel.size())
        {
            throw std::invalid_argument("Invalid palette hex channel");
        }

        const auto v = static_cast<float>(byte) / 255.F;
        result[i] = i == 3 ? v : (v <= .04045F ? v / 12.92F : std::pow((v + .055F) / 1.055F, 2.4F));
    }

    return result;
}

/// @brief Display/save uses nearest eight-bit sRGB without changing runtime linear state.
std::string format_color(ui::rgba value)
{
    constexpr std::string_view hex = "0123456789ABCDEF";
    std::string result = "#00000000";
    for (std::size_t i = 0; i < value.size(); ++i)
    {
        const auto v = value[i];
        if (!std::isfinite(v) || v < 0 || v > 1)
        {
            throw std::invalid_argument("Invalid linear palette color");
        }

        const auto encoded =
            i == 3 ? v : (v <= .0031308F ? v * 12.92F : 1.055F * std::pow(v, 1.F / 2.4F) - .055F);
        const auto byte = static_cast<unsigned>(std::lround(std::clamp(encoded, 0.F, 1.F) * 255.F));
        result[1 + i * 2] = hex[byte >> 4U];
        result[2 + i * 2] = hex[byte & 15U];
    }

    return result;
}

/// @brief Duplicate fields, entities, unknown nodes and excess entries fail before compilation.
std::shared_ptr<const ui::palette> parse_palette_xml(xml_source source,
                                                     const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"ui.palette.parse", "ui.content"}};
    pugi::xml_document xml;
    const auto root = detail::read_root(xml, source.bytes, max_palette_bytes);
    detail::fields fields{root, true};
    std::string version;
    ui::palette_definition result;
    fields.value("version", version, true);
    fields.value("contract", result.contract, true);
    fields.finish();
    if (std::string_view{root.name()} != "palette" || version != "1")
    {
        throw std::invalid_argument("Unsupported palette root/version");
    }

    for (const auto child : root.children())
    {
        if (child.type() == pugi::node_comment)
        {
            continue;
        }

        if (child.type() != pugi::node_element || std::string_view{child.name()} != "color" ||
            result.colors.size() == ui::max_palette_colors)
        {
            throw std::invalid_argument("Unexpected or excessive palette color");
        }

        detail::fields entry{child, true};
        ui::palette_color color;
        std::string value;
        entry.value("key", color.key, true);
        entry.value("value", value, true);
        entry.finish();
        detail::expect_empty(child);
        color.value = parse_color(value);
        result.colors.push_back(std::move(color));
    }

    return ui::palette::compile(std::move(result));
}

/// @brief Identifiers are validated ASCII, so canonical emission needs no free-form escaping.
std::string write_palette_xml(const ui::palette& source)
{
    const auto& data = source.definition();
    std::string result = "<palette version=\"1\" contract=\"" + data.contract + "\">\n";
    for (const auto& entry : data.colors)
    {
        result +=
            "  <color key=\"" + entry.key + "\" value=\"" + format_color(entry.value) + "\"/>\n";
    }

    return result + "</palette>\n";
}
} // namespace atlas::ui_content
