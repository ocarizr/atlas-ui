/** @file @brief Strict bounded version 2 outgoing predicate decorators in draft XML. */
#include "xml_fields.hpp"

namespace atlas::editor_flow::detail
{
namespace
{
/// @brief Decode leaf arguments and nested AND groups without unbounded recursion.
authored_condition read_node(pugi::xml_node node, std::size_t depth, std::size_t& count)
{
    if (++count > 256 || depth > 8)
    {
        throw std::length_error("Flow condition XML budget exhausted");
    }

    fields codec{node, true};
    authored_condition result;
    codec.value("callable", result.callable);
    codec.value("context", result.context);
    codec.value("negate", result.negate);
    codec.finish();
    for (const auto child : node.children())
    {
        if (child.type() == pugi::node_comment)
        {
            continue;
        }

        if (child.type() != pugi::node_element)
        {
            throw std::invalid_argument("Invalid Flow predicate content");
        }

        const std::string_view name = child.name();
        if (name == "predicate" && result.children.size() < 64)
        {
            result.children.push_back(read_node(child, depth + 1, count));
        }
        else if (name == "arg" && result.arguments.size() < 32)
        {
            fields argument{child, true};
            scalar_fields(argument, result.arguments.emplace_back());
            argument.finish();
            empty(child);
        }
        else
        {
            throw std::invalid_argument("Unknown or excess Flow predicate field");
        }
    }

    return result;
}

/// @brief Emit only persistent values; domain/generation handles never enter XML.
void write_node(pugi::xml_node node, const authored_condition& source)
{
    auto callable = source.callable;
    auto context = source.context;
    auto negate = source.negate;
    fields codec{node, false};
    codec.value("callable", callable);
    codec.value("context", context);
    codec.value("negate", negate);
    for (auto value : source.arguments)
    {
        fields arg{node.append_child("arg"), false};
        scalar_fields(arg, value);
    }

    for (const auto& child : source.children)
    {
        write_node(node.append_child("predicate"), child);
    }
}
} // namespace

/// @brief A route has one root expression; multiple predicate roots are rejected.
transition_conditions read_conditions(pugi::xml_node node)
{
    fields codec{node, true};
    transition_conditions result;
    codec.value("transition", result.transition);
    codec.finish();
    bool found = false;
    std::size_t count = 0;
    for (const auto child : node.children())
    {
        if (child.type() == pugi::node_comment)
        {
            continue;
        }

        if (found || child.type() != pugi::node_element ||
            std::string_view{child.name()} != "predicate")
        {
            throw std::invalid_argument("Expected one outgoing predicate root");
        }

        result.expression = read_node(child, 0, count);
        found = true;
    }

    if (!found)
    {
        throw std::invalid_argument("Missing outgoing predicate root");
    }

    return result;
}

/// @brief Authoring and recovery share identical condition records.
void write_conditions(pugi::xml_node root, const transition_conditions& source)
{
    auto node = root.append_child("conditions");
    node.append_attribute("transition").set_value(source.transition.c_str());
    write_node(node.append_child("predicate"), source.expression);
}
} // namespace atlas::editor_flow::detail
