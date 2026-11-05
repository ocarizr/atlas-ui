/** @file @brief Deserialize editable definitions from bounded streamer-provided UTF-8 buffers. */
#include "content_state.hpp"
#include <stdexcept>

namespace atlas::mvvm_content
{
namespace
{
/// @brief Bound each collection before appending owned authoring metadata.
void room(std::size_t size, std::size_t maximum)
{
    if (size >= maximum)
    {
        throw std::overflow_error("MVVM XML collection limit exhausted");
    }
}

/// @brief Actions/events use the same signature grammar without sharing their namespace.
void read_signals(pugi::xml_node parent, const char* tag,
                  std::vector<mvvm::signal_definition>& result, content_limits limits)
{
    detail::attributes(parent, {});
    for (const auto node : detail::children(parent))
    {
        if (std::string_view{node.name()} != tag)
        {
            throw std::invalid_argument("unexpected MVVM signal element");
        }

        room(result.size(), limits.schema.max_signals);
        detail::attributes(node, {"name"});
        mvvm::signal_definition signal{detail::required(node, "name")};
        for (const auto parameter : detail::children(node))
        {
            if (std::string_view{parameter.name()} != "Param")
            {
                throw std::invalid_argument("unexpected MVVM parameter element");
            }

            room(signal.parameters.size(), limits.schema.max_parameters);
            detail::attributes(parameter, {"name", "type"});
            detail::empty(parameter);
            signal.parameters.push_back({detail::required(parameter, "name"),
                                         detail::parse_type(detail::required(parameter, "type"))});
        }

        result.push_back(std::move(signal));
    }
}
} // namespace

/// @brief The parser never owns a path or performs application-managed file I/O.
mvvm::definition parse_xml(std::string_view bytes, content_limits limits,
                           const diagnostics::services& destinations)
{
    const diagnostics::operation_scope scope{destinations, {"mvvm.xml.read", "mvvm.content"}};
    detail::check_limits(limits);
    mvvm::validate_text(bytes, limits.max_bytes);
    pugi::xml_document document;
    const auto parsed =
        document.load_buffer(bytes.data(), bytes.size(), pugi::parse_full, pugi::encoding_utf8);
    if (!parsed)
    {
        throw std::invalid_argument("invalid MVVM XML at byte " + std::to_string(parsed.offset) +
                                    ": " + parsed.description());
    }

    pugi::xml_node root;
    for (const auto node : document.children())
    {
        if (node.type() == pugi::node_declaration || node.type() == pugi::node_comment)
        {
            continue;
        }

        if (node.type() != pugi::node_element || root)
        {
            throw std::invalid_argument("MVVM XML requires one root without DTD/PI");
        }

        root = node;
    }

    if (!root || std::string_view{root.name()} != "ViewModel")
    {
        throw std::invalid_argument("missing ViewModel XML root");
    }

    detail::attributes(root, {"version", "name"});
    if (detail::required(root, "version") != "1")
    {
        throw std::invalid_argument("unsupported MVVM XML version");
    }

    mvvm::definition result{detail::required(root, "name")};
    bool fields = false, actions = false, events = false;
    for (const auto group : detail::children(root))
    {
        const std::string_view name = group.name();
        if (name == "Fields" && !fields)
        {
            fields = true;
            detail::attributes(group, {});
            for (const auto node : detail::children(group))
            {
                if (std::string_view{node.name()} != "Field")
                {
                    throw std::invalid_argument("unexpected MVVM field element");
                }

                room(result.fields.size(), limits.schema.max_fields);
                detail::attributes(node, {"name", "type", "value"});
                detail::empty(node);
                result.fields.push_back(
                    {detail::required(node, "name"),
                     detail::parse_value(detail::parse_type(detail::required(node, "type")),
                                         detail::required(node, "value"))});
            }
        }
        else if (name == "Actions" && !actions)
        {
            actions = true;
            read_signals(group, "Action", result.actions, limits);
        }
        else if (name == "Events" && !events)
        {
            events = true;
            read_signals(group, "Event", result.events, limits);
        }
        else
        {
            throw std::invalid_argument("unknown or duplicate MVVM XML group");
        }
    }

    detail::check_definition(result, limits);
    return result;
}
} // namespace atlas::mvvm_content
