/** @file @brief Bounded authoring/recovery XML without runtime topology requirements. */
#include "xml_fields.hpp"

namespace atlas::editor_flow
{
namespace
{
/// @brief Check declarations before retaining another record from untrusted XML.
template <class T, class Codec>
void read(pugi::xml_node node, std::vector<T>& output, std::size_t maximum, Codec codec)
{
    if (output.size() >= maximum)
    {
        throw std::length_error("Flow draft declaration budget exhausted");
    }

    detail::fields fields{node, true};
    codec(fields, output.emplace_back());
    fields.finish();
    if constexpr (!std::is_same_v<T, ui_content::flow_transition>)
    {
        detail::empty(node);
    }
}

/// @brief Copy only each record being serialized, leaving immutable session snapshots untouched.
template <class T, class Codec>
void write(pugi::xml_node root, const char* name, const std::vector<T>& source, Codec codec)
{
    for (const auto& record : source)
    {
        auto copy = record;
        detail::fields fields{root.append_child(name), false};
        codec(fields, copy);
    }
}

/// @brief Reject DTDs, processing instructions, text and multiple roots before reading
/// declarations.
pugi::xml_node root(pugi::xml_document& tree, ui_content::xml_source source)
{
    mvvm::validate_text(source.bytes, detail::max_bytes);
    if (!tree.load_buffer(source.bytes.data(), source.bytes.size(), pugi::parse_full,
                          pugi::encoding_utf8))
    {
        throw std::invalid_argument("Malformed Flow authoring XML");
    }

    std::size_t elements = 0;
    for (const auto node : tree.children())
    {
        if (node.type() == pugi::node_element)
        {
            ++elements;
        }
        else if (node.type() != pugi::node_declaration && node.type() != pugi::node_comment)
        {
            throw std::invalid_argument("Unsupported Flow authoring XML content");
        }
    }

    const auto result = tree.document_element();
    if (elements != 1 || std::string_view{result.name()} != "flow-authoring")
    {
        throw std::invalid_argument("Expected one Flow authoring root");
    }

    return result;
}
} // namespace

/// @brief Draft syntax is strict while unresolved graph references remain available for editing.
document read_xml(ui_content::xml_source source)
{
    pugi::xml_document tree;
    const auto node = root(tree, source);
    detail::fields header{node, true};
    document result;
    unsigned version = 0;
    header.value("version", version);
    header.value("id", result.id);
    header.finish();
    if (version != 1)
    {
        throw std::invalid_argument("Unsupported Flow authoring XML version");
    }

    bool selected = false;
    for (const auto child : node.children())
    {
        if (child.type() == pugi::node_comment)
        {
            continue;
        }

        if (child.type() != pugi::node_element)
        {
            throw std::invalid_argument("Unexpected Flow authoring content");
        }

        const std::string_view name = child.name();
        if (name == "region")
        {
            read(child, result.graph.regions, 64, detail::region_fields);
        }
        else if (name == "screen")
        {
            read(child, result.graph.screens, 256, detail::screen_fields);
        }
        else if (name == "transition")
        {
            read(child, result.graph.transitions, 1024, detail::transition_fields);
        }
        else if (name == "parameter")
        {
            read(child, result.graph.parameters, 64, detail::parameter_fields);
        }
        else if (name == "layout")
        {
            read(child, result.layout, 1344, detail::layout_fields);
        }
        else if (name == "selection" && !selected)
        {
            detail::fields fields{child, true};
            detail::reference_fields(fields, result.selection);
            fields.finish();
            detail::empty(child);
            selected = true;
        }
        else
        {
            throw std::invalid_argument("Unknown or duplicate Flow authoring declaration");
        }
    }

    validate(result);
    return result;
}

/// @brief Source and recovery files use the same complete versioned snapshot representation.
std::string write_xml(const document& source)
{
    validate(source);
    pugi::xml_document tree;
    auto node = tree.append_child("flow-authoring");
    node.append_attribute("version") = 1;
    node.append_attribute("id").set_value(source.id.c_str());
    write(node, "region", source.graph.regions, detail::region_fields);
    write(node, "screen", source.graph.screens, detail::screen_fields);
    write(node, "parameter", source.graph.parameters, detail::parameter_fields);
    write(node, "transition", source.graph.transitions, detail::transition_fields);
    write(node, "layout", source.layout, detail::layout_fields);
    auto selection = source.selection;
    detail::fields selected{node.append_child("selection"), false};
    detail::reference_fields(selected, selection);
    detail::writer output;
    tree.save(output, "  ");
    return std::move(output.bytes);
}
} // namespace atlas::editor_flow
