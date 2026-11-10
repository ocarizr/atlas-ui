/** @file @brief Strict v1 catalog XML parsing from already acquired bytes. */
#include "catalog_limits.hpp"
#include <algorithm>
#include <atlas/assets/catalog.hpp>
#include <charconv>
#include <initializer_list>
#include <pugixml.hpp>
#include <stdexcept>

namespace atlas::assets
{
namespace
{
/// @brief Require one instance of every field and reject undeclared attributes.
void attributes(pugi::xml_node node, std::initializer_list<const char*> required)
{
    std::size_t count = 0;
    for (auto attribute : node.attributes())
    {
        ++count;
        if (std::ranges::none_of(required,
                                 [&](std::string_view name)
                                 {
                                     return name == attribute.name();
                                 }))
        {
            throw std::invalid_argument("Unknown catalog attribute: " +
                                        std::string{attribute.name()});
        }
    }

    if (count != required.size())
    {
        throw std::invalid_argument("Missing or duplicate attributes on " +
                                    std::string{node.name()});
    }

    for (const auto name : required)
    {
        if (!node.attribute(name))
        {
            throw std::invalid_argument("Missing catalog attribute: " + std::string{name});
        }
    }
}

/// @brief Parse unsigned decimal values without truncation, defaults or partial consumption.
template <class Number> Number number(pugi::xml_node node, const char* attribute)
{
    const std::string_view text = node.attribute(attribute).value();
    Number value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    {
        throw std::invalid_argument("Invalid catalog number: " + std::string{attribute});
    }

    return value;
}

/// @brief Both record and reference elements persist the expected type explicitly.
asset_ref reference(pugi::xml_node node)
{
    return {parse_id(node.attribute("id").value()), parse_type(node.attribute("type").value())};
}

/// @brief Comments are ignored; all other child content must belong to the schema.
void read_links(pugi::xml_node node, asset_record& record)
{
    for (const auto child : node.children())
    {
        if (child.type() == pugi::node_comment)
        {
            continue;
        }

        if (child.type() != pugi::node_element || child.first_child() ||
            record.dependencies.size() + record.products.size() >= detail::max_record_links)
        {
            throw std::invalid_argument("Invalid catalog link content or link limit exceeded");
        }

        const std::string_view kind = child.name();
        if (kind == "dependency")
        {
            attributes(child, {"id", "type"});
            record.dependencies.push_back(reference(child));
        }
        else if (kind == "derived")
        {
            attributes(child, {"role", "id", "type"});
            record.products.push_back({child.attribute("role").value(), reference(child)});
        }
        else
        {
            throw std::invalid_argument("Unknown catalog link: " + std::string{kind});
        }
    }
}

/// @brief Decode a complete record before adding it to the draft.
asset_record read_record(pugi::xml_node node)
{
    attributes(node, {"id", "type", "root", "path", "revision", "byte-size", "importer",
                      "importer-version", "settings-version"});
    asset_record record{reference(node),
                        {node.attribute("root").value(), node.attribute("path").value()},
                        number<std::uint64_t>(node, "revision"),
                        number<std::uint64_t>(node, "byte-size"),
                        node.attribute("importer").value(),
                        number<std::uint32_t>(node, "importer-version"),
                        number<std::uint32_t>(node, "settings-version")};
    read_links(node, record);
    return record;
}

/// @brief Only a single catalog element and optional declaration/comments are permitted.
pugi::xml_node catalog_root(const pugi::xml_document& document)
{
    pugi::xml_node root;
    for (const auto node : document.children())
    {
        if (node.type() == pugi::node_comment || node.type() == pugi::node_declaration)
        {
            continue;
        }

        if (root || node.type() != pugi::node_element ||
            std::string_view{node.name()} != "asset-catalog")
        {
            throw std::invalid_argument("Expected a single asset-catalog element without a DTD");
        }

        root = node;
    }

    if (!root)
    {
        throw std::invalid_argument("Missing asset-catalog element");
    }

    return root;
}

/// @brief Decode only the supported version and bound record/link counts while reading.
catalog_document read_document(pugi::xml_node root)
{
    attributes(root, {"version", "namespace", "next-local"});
    if (std::string_view{root.attribute("version").value()} != "1")
    {
        throw std::invalid_argument("Unsupported asset catalog version");
    }

    catalog_document draft{parse_namespace(root.attribute("namespace").value()),
                           number<std::uint64_t>(root, "next-local")};
    std::size_t links = 0;
    for (const auto node : root.children())
    {
        if (node.type() == pugi::node_comment)
        {
            continue;
        }

        if (node.type() != pugi::node_element || std::string_view{node.name()} != "asset" ||
            draft.records.size() == detail::max_records)
        {
            throw std::invalid_argument("Unexpected catalog element or record limit exceeded");
        }

        try
        {
            auto record = read_record(node);
            links += record.dependencies.size() + record.products.size();
            if (links > detail::max_catalog_links)
            {
                throw std::invalid_argument("Asset catalog link limit exceeded");
            }

            draft.records.push_back(std::move(record));
        }
        catch (const std::invalid_argument& error)
        {
            throw std::invalid_argument("asset at byte " + std::to_string(node.offset_debug()) +
                                        ": " + error.what());
        }
    }

    return draft;
}
} // namespace

/// @brief Add the caller's source identity to schema and parser diagnostics.
catalog read_xml(xml_source source, const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"catalog.read_xml", "assets.cpu"}};
    const auto bytes = source.bytes;
    try
    {
        if (bytes.empty() || bytes.size() > detail::max_xml_bytes || bytes.find('\0') != bytes.npos)
        {
            throw std::invalid_argument("Invalid asset catalog XML byte payload");
        }

        pugi::xml_document document;
        const auto parsed = document.load_buffer(
            bytes.data(), bytes.size(),
            (pugi::parse_full | pugi::parse_fragment) & ~pugi::parse_escapes, pugi::encoding_utf8);
        if (!parsed)
        {
            throw std::invalid_argument("XML byte " + std::to_string(parsed.offset) + ": " +
                                        parsed.description());
        }

        return catalog::compile(read_document(catalog_root(document)), sinks);
    }
    catch (const std::invalid_argument& error)
    {
        throw std::invalid_argument(std::string{source.name} + ": " + error.what());
    }
}
} // namespace atlas::assets
