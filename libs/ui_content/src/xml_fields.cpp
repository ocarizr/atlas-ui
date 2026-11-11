/** @file @brief Exact UI XML attributes and typed asset references. */
#include "xml_fields.hpp"
#include <set>

namespace atlas::ui_content::detail
{
/// @brief Track the field name and validate required attributes before reading.
pugi::xml_attribute fields::take(const char* name, bool required)
{
    known_.emplace_back(name);
    const auto attribute = node_.attribute(name);
    if (required && !attribute)
    {
        throw std::invalid_argument(std::string{"Missing UI XML attribute: "} + name);
    }

    return attribute;
}

/// @brief Fixed use-site types are checked before parsing or emitting an ID.
void fields::reference(const char* name, assets::asset_ref& data, assets::asset_type type)
{
    if (reading_)
    {
        data = {assets::parse_id(take(name, true).value()), type};
        return;
    }

    if (data.type != type)
    {
        throw std::invalid_argument("Wrong UI document asset reference type");
    }

    node_.append_attribute(name).set_value(assets::format_id(data.id).c_str());
}

/// @brief The optional field shares the required reference representation.
void fields::reference(const char* name, std::optional<assets::asset_ref>& data,
                       assets::asset_type type)
{
    if (reading_)
    {
        if (const auto attribute = take(name, false))
        {
            data = assets::asset_ref{assets::parse_id(attribute.value()), type};
        }
    }
    else if (data)
    {
        reference(name, *data, type);
    }
}

/// @brief Every provided attribute must appear exactly once in the declared field set.
void fields::finish() const
{
    if (!reading_)
    {
        return;
    }

    std::set<std::string_view> seen;
    for (auto attribute : node_.attributes())
    {
        const std::string_view name = attribute.name();
        if (std::ranges::find(known_, name) == known_.end() || !seen.insert(name).second)
        {
            throw std::invalid_argument("Unknown or duplicate UI XML attribute: " +
                                        std::string{name});
        }
    }
}

/// @brief Permit comments between declarations without ignoring other XML node types.
void expect_empty(pugi::xml_node node)
{
    for (auto child : node.children())
    {
        if (child.type() != pugi::node_comment)
        {
            throw std::invalid_argument(std::string{"Unexpected content in "} + node.name());
        }
    }
}
} // namespace atlas::ui_content::detail
