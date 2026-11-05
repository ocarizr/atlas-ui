/** @file @brief Strict XML attributes/elements; DTD, processing instructions and unknown data fail.
 */
#include "content_state.hpp"
#include <algorithm>
#include <stdexcept>

namespace atlas::mvvm_content::detail
{
/// @brief Duplicate/unknown attributes cannot silently change authored meaning.
void attributes(pugi::xml_node node, std::initializer_list<std::string_view> allowed)
{
    std::vector<std::string_view> seen;
    for (const auto attribute : node.attributes())
    {
        const std::string_view name = attribute.name();
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end() ||
            std::find(seen.begin(), seen.end(), name) != seen.end())
        {
            throw std::invalid_argument("unknown or duplicate MVVM XML attribute: " +
                                        std::string{name});
        }

        seen.push_back(name);
    }
}

/// @brief Presence and value emptiness are separate for authored text defaults.
std::string required(pugi::xml_node node, const char* name)
{
    const auto attribute = node.attribute(name);
    if (!attribute)
    {
        throw std::invalid_argument("missing MVVM XML attribute: " + std::string{name});
    }

    return attribute.value();
}

/// @brief Comments and formatting whitespace do not become application data.
std::vector<pugi::xml_node> children(pugi::xml_node node)
{
    std::vector<pugi::xml_node> result;
    for (const auto child : node.children())
    {
        if (child.type() == pugi::node_element)
        {
            result.push_back(child);
            continue;
        }

        if (child.type() == pugi::node_comment)
        {
            continue;
        }

        if (child.type() == pugi::node_pcdata &&
            std::string_view{child.value()}.find_first_not_of(" \t\r\n") == std::string_view::npos)
        {
            continue;
        }

        throw std::invalid_argument("unexpected MVVM XML content");
    }

    return result;
}

/// @brief Leaf descriptors cannot hide nested fields or unexpected text.
void empty(pugi::xml_node node)
{
    if (!children(node).empty())
    {
        throw std::invalid_argument("unexpected nested MVVM XML element");
    }
}
} // namespace atlas::mvvm_content::detail
