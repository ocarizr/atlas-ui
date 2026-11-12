/** @file @brief Order-preserving XML element declarations. */
#include "xml_layout.hpp"
#include <set>

namespace atlas::ui_content::detail
{
namespace
{
/// @brief Decode an attribute-only section into its own typed element metadata.
template <class T, class Codec> void read_section(pugi::xml_node node, T& target, Codec codec)
{
    fields xml{node, true};
    codec(xml, target);
    xml.finish();
    expect_empty(node);
}

/// @brief Encode one typed section using the same attribute schema as the reader.
template <class T, class Codec>
void write_section(pugi::xml_node node, const char* name, const T& source, Codec codec)
{
    auto data = source;
    fields xml{node.append_child(name), false};
    codec(xml, data);
}

/// @brief Read optional metadata that may appear at most once per element.
bool read_metadata(pugi::xml_node node, element& target)
{
    const std::string_view kind = node.name();
    if (kind == "layout")
    {
        read_section(node, target.layout, layout_fields);
    }
    else if (kind == "paint")
    {
        read_section(node, target, paint_fields);
    }
    else if (kind == "canvas-slot")
    {
        read_section(node, target.canvas.emplace(), canvas_fields);
    }
    else if (kind == "stack-slot")
    {
        read_section(node, target.stack.emplace(), stack_fields);
    }
    else if (kind == "horizontal-boundary")
    {
        read_section(node, target.horizontal.emplace(), boundary_fields);
    }
    else if (kind == "vertical-boundary")
    {
        read_section(node, target.vertical.emplace(), boundary_fields);
    }
    else if (kind == "interaction")
    {
        read_section(node, target.interaction.emplace(), interaction_fields);
    }
    else
    {
        return false;
    }

    return true;
}
} // namespace

/// @brief Identify source line context via the caller's node wrapper.
void read_element(pugi::xml_node node, element& target)
{
    fields xml{node, true};
    element_fields(xml, target);
    xml.finish();
    std::set<std::string_view> sections;
    for (auto child : node.children())
    {
        if (child.type() == pugi::node_comment)
        {
            continue;
        }

        if (child.type() != pugi::node_element)
        {
            throw std::invalid_argument("Unexpected element content");
        }

        if (read_metadata(child, target))
        {
            if (!sections.insert(child.name()).second)
            {
                throw std::invalid_argument(std::string{"Duplicate UI section: "} + child.name());
            }
        }
        else
        {
            read_binding(child, target);
        }
    }
}

/// @brief Emit common sections in a fixed order and preserve descriptor declaration order.
void write_element(pugi::xml_node node, const element& source)
{
    element attributes;
    attributes.id = source.id;
    attributes.caption = source.caption;
    attributes.parent = source.parent;
    attributes.panel = source.panel;
    attributes.requested = source.requested;
    fields xml{node, false};
    element_fields(xml, attributes);
    write_section(node, "layout", source.layout, layout_fields);
    attributes.paint = source.paint;
    attributes.visual = source.visual;
    attributes.texture = source.texture;
    fields paint{node.append_child("paint"), false};
    paint_fields(paint, attributes);
    if (source.canvas)
    {
        write_section(node, "canvas-slot", *source.canvas, canvas_fields);
    }

    if (source.stack)
    {
        write_section(node, "stack-slot", *source.stack, stack_fields);
    }

    if (source.horizontal)
    {
        write_section(node, "horizontal-boundary", *source.horizontal, boundary_fields);
    }

    if (source.vertical)
    {
        write_section(node, "vertical-boundary", *source.vertical, boundary_fields);
    }

    if (source.interaction)
    {
        write_section(node, "interaction", *source.interaction, interaction_fields);
    }

    write_bindings(node, source);
}
} // namespace atlas::ui_content::detail
