/** @file @brief Strict byte-only XML string-table loading with bounded decoded state. */
#include "state.hpp"
#include <algorithm>
#include <pugixml.hpp>
#include <set>
#include <stdexcept>

namespace atlas::localization
{
namespace
{
/// @brief Reject unrecognized attributes instead of silently losing authored metadata.
void attributes(pugi::xml_node node, std::initializer_list<std::string_view> allowed)
{
    std::set<std::string_view> seen;
    for (auto value : node.attributes())
    {
        if (!seen.insert(value.name()).second ||
            std::find(allowed.begin(), allowed.end(), value.name()) == allowed.end())
        {
            throw std::invalid_argument("unknown localization XML attribute");
        }
    }
}

/// @brief Literal values contain text/CDATA only, never nested markup.
std::string literal(pugi::xml_node node)
{
    std::string result;
    for (auto child : node.children())
    {
        if (child.type() == pugi::node_comment)
        {
            continue;
        }

        if (child.type() != pugi::node_pcdata && child.type() != pugi::node_cdata)
        {
            throw std::invalid_argument("localization literal contains markup");
        }

        result += child.value();
    }

    return result;
}

/// @brief Stable category tokens map to the public indexed form array.
std::size_t category(std::string_view name)
{
    constexpr std::array names{"zero", "one", "two", "few", "many", "other"};
    const auto found = std::find(names.begin(), names.end(), name);
    if (found == names.end())
    {
        throw std::invalid_argument("unknown localization plural category");
    }

    return static_cast<std::size_t>(found - names.begin());
}

/// @brief Parse a bounded message with no implicit fallback plural policy.
message read_message(pugi::xml_node node)
{
    attributes(node, {"key"});
    message result;
    result.key = node.attribute("key").value();
    const std::string_view kind = node.name();
    if (kind == "string")
    {
        result.text = literal(node);
    }
    else if (kind == "plural")
    {
        for (auto form : node.children())
        {
            if (form.type() == pugi::node_comment)
            {
                continue;
            }

            if (form.type() != pugi::node_element || std::string_view{form.name()} != "form")
            {
                throw std::invalid_argument("expected localization plural form");
            }

            attributes(form, {"category"});
            auto& value = result.forms[category(form.attribute("category").value())];
            if (value)
            {
                throw std::invalid_argument("duplicate localization plural form");
            }

            value = literal(form);
        }
    }
    else
    {
        throw std::invalid_argument("unknown localization message kind");
    }

    return result;
}
} // namespace

/// @brief Reject malformed UTF-8 and doctypes before constructing an immutable schema.
definition parse_xml(std::string_view bytes, const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"localization.parse", "localization.cpu"}};
    detail::validate_text(bytes, std::size_t{8} * 1024 * 1024);
    if (bytes.find("<!DOCTYPE") != bytes.npos)
    {
        throw std::invalid_argument("localization doctype forbidden");
    }

    pugi::xml_document xml;
    if (!xml.load_buffer(bytes.data(), bytes.size(),
                         pugi::parse_default | pugi::parse_comments | pugi::parse_ws_pcdata_single,
                         pugi::encoding_utf8))
    {
        throw std::invalid_argument("invalid localization XML");
    }

    auto root = xml.document_element();
    if (std::string_view{root.name()} != "localization")
    {
        throw std::invalid_argument("expected localization root");
    }

    for (auto node : xml.children())
    {
        if (node.type() == pugi::node_element && node != root)
        {
            throw std::invalid_argument("multiple localization roots");
        }
    }

    attributes(root, {"fallback"});
    definition result;
    result.fallback = root.attribute("fallback").value();
    for (auto node : root.children())
    {
        if (node.type() == pugi::node_comment)
        {
            continue;
        }

        if (std::string_view{node.name()} != "language" || result.languages.size() >= 128)
        {
            throw std::invalid_argument("invalid localization language declaration");
        }

        attributes(node, {"tag", "direction"});
        language table;
        table.tag = node.attribute("tag").value();
        const std::string_view direction = node.attribute("direction").value();
        if (direction == "rtl")
        {
            table.direction = text_direction::right_to_left;
        }
        else if (direction != "ltr")
        {
            throw std::invalid_argument("language direction must be ltr or rtl");
        }

        for (auto entry : node.children())
        {
            if (entry.type() == pugi::node_comment)
            {
                continue;
            }

            if (table.messages.size() >= 65536)
            {
                throw std::invalid_argument("localization key budget exceeded");
            }

            table.messages.push_back(read_message(entry));
        }

        result.languages.push_back(std::move(table));
    }

    return detail::prepare(std::move(result))->data;
}
} // namespace atlas::localization
