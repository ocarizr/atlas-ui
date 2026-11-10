/** @file @brief Deterministic XML serialization of a validated central asset catalog. */
#include "catalog_limits.hpp"
#include <atlas/assets/catalog.hpp>
#include <pugixml.hpp>
#include <sstream>
#include <stdexcept>

namespace atlas::assets
{
namespace
{
/// @brief Refuse to publish a partially allocated XML tree.
pugi::xml_node element(pugi::xml_node parent, const char* name)
{
    auto node = parent.append_child(name);
    if (!node)
    {
        throw std::bad_alloc{};
    }

    return node;
}

/// @brief Copy a bounded validated attribute into the owning XML tree.
void attribute(pugi::xml_node node, const char* name, std::string_view value)
{
    if (!node.append_attribute(name).set_value(value.data(), value.size()))
    {
        throw std::bad_alloc{};
    }
}

/// @brief Serialize the expected type independently of the stable ID.
void reference(pugi::xml_node node, asset_ref ref)
{
    attribute(node, "id", format_id(ref.id));
    attribute(node, "type", format_type(ref.type));
}

/// @brief Emit fields and canonicalized link arrays in fixed schema order.
void write_record(pugi::xml_node root, const asset_record& record)
{
    const auto node = element(root, "asset");
    reference(node, record.reference);
    attribute(node, "root", record.source.root);
    attribute(node, "path", record.source.path);
    attribute(node, "revision", std::to_string(record.revision));
    attribute(node, "byte-size", std::to_string(record.byte_size));
    attribute(node, "importer", record.importer);
    attribute(node, "importer-version", std::to_string(record.importer_version));
    attribute(node, "settings-version", std::to_string(record.settings_version));
    for (const auto& dependency : record.dependencies)
    {
        reference(element(node, "dependency"), dependency);
    }

    for (const auto& product : record.products)
    {
        const auto derived = element(node, "derived");
        attribute(derived, "role", product.role);
        reference(derived, product.target);
    }
}
} // namespace

/// @brief The caller decides how to persist the complete, canonical v1 document.
std::string write_xml(const catalog& snapshot, const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"catalog.write_xml", "assets.cpu"}};
    pugi::xml_document document;
    const auto root = element(document, "asset-catalog");
    const auto& source = snapshot.document();
    attribute(root, "version", "1");
    attribute(root, "namespace", format_namespace(source.space));
    attribute(root, "next-local", std::to_string(source.next_local));
    for (const auto& record : source.records)
    {
        write_record(root, record);
    }

    std::ostringstream output;
    document.save(output, "  ", pugi::format_default, pugi::encoding_utf8);
    if (!output)
    {
        throw std::runtime_error("Asset catalog XML serialization failed");
    }

    auto xml = std::move(output).str();
    if (xml.size() > detail::max_xml_bytes)
    {
        throw std::runtime_error("Asset catalog XML exceeded its byte limit");
    }

    return xml;
}
} // namespace atlas::assets
