/** @file @brief Bounded strict YAML preferences; semantic palette content remains XML. */
#include "editor_theme.hpp"
#include <algorithm>
#include <set>
#include <yaml-cpp/yaml.h>

namespace atlas::editor
{
namespace
{
/// @brief Reject duplicate and unknown fields without permissive scalar coercion.
void keys(const YAML::Node& source, std::span<const std::string_view> allowed)
{
    if (!source.IsMap())
    {
        throw std::invalid_argument("Expected theme YAML mapping");
    }

    std::set<std::string> seen;
    for (const auto& field : source)
    {
        if (!field.first.IsScalar())
        {
            throw std::invalid_argument("Expected theme YAML key");
        }

        const auto name = field.first.as<std::string>();
        if (!seen.insert(name).second || std::ranges::find(allowed, name) == allowed.end())
        {
            throw std::invalid_argument("Unknown or duplicate theme preference: " + name);
        }
    }
}
} // namespace

/// @brief Parse only a bounded fixed-depth schema; failures never touch a live palette context.
theme_preferences parse_theme_preferences(std::string_view yaml)
{
    if (yaml.size() > ui_content::max_palette_bytes)
    {
        throw std::invalid_argument("Theme preferences too large");
    }

    try
    {
        const auto documents = YAML::LoadAll(std::string{yaml});
        if (documents.size() != 1)
        {
            throw std::invalid_argument("Expected one theme preferences document");
        }

        const auto& source = documents.front();
        constexpr std::array<std::string_view, 3> fields{"version", "preset", "overrides"};
        keys(source, fields);
        if (!source["version"].IsScalar() || source["version"].Scalar() != "1" ||
            !source["preset"].IsScalar())
        {
            throw std::invalid_argument("Unsupported theme preference version/preset");
        }

        theme_preferences result{source["preset"].as<std::string>(), {}};
        const auto preset = editor_preset(result.preset);
        if (const auto overrides = source["overrides"])
        {
            if (!overrides.IsMap() || overrides.size() > ui::max_palette_colors)
            {
                throw std::invalid_argument("Invalid theme overrides");
            }

            result.overrides.reserve(overrides.size());
            for (const auto& field : overrides)
            {
                if (!field.first.IsScalar() || !field.second.IsScalar())
                {
                    throw std::invalid_argument("Expected scalar theme override");
                }

                const auto key = field.first.as<std::string>();
                (void)preset->find(key);
                const auto value = ui_content::format_color(
                    ui_content::parse_color(field.second.as<std::string>()));
                result.overrides.push_back({key, value});
            }
        }

        normalize_theme_overrides(result.overrides);
        return result;
    }
    catch (const YAML::Exception& error)
    {
        throw std::invalid_argument(error.what());
    }
    catch (const std::out_of_range& error)
    {
        throw std::invalid_argument(error.what());
    }
}

/// @brief Emit canonical scalar data after checking all keys and colors against the preset.
std::string write_theme_preferences(const theme_preferences& value)
{
    const auto preset = editor_preset(value.preset);
    auto overrides = value.overrides;
    normalize_theme_overrides(overrides);
    YAML::Emitter out;
    out << YAML::BeginMap << YAML::Key << "version" << YAML::Value << 1 << YAML::Key << "preset"
        << YAML::Value << value.preset << YAML::Key << "overrides" << YAML::Value << YAML::BeginMap;
    for (const auto& [key, hex] : overrides)
    {
        (void)preset->find(key);
        out << YAML::Key << key << YAML::Value
            << ui_content::format_color(ui_content::parse_color(hex));
    }

    out << YAML::EndMap << YAML::EndMap;
    return std::string{out.c_str()} + "\n";
}
} // namespace atlas::editor
