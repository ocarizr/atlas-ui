/** @file @brief Strict XML serialization for viewport Flow graphs. */
#include "flow_versions.hpp"
#include "storage.hpp"
#include "xml_fields.hpp"
#include <atlas/ui_content/flow.hpp>

namespace atlas::ui_content
{
namespace
{
constexpr std::size_t max_flow_bytes = std::size_t{512} * 1024; ///< Bounded Flow authoring input.

/// @brief Preserve the authored value type without numeric or Boolean coercion.
void scalar(detail::fields& fields, mvvm::value& value)
{
    auto tag = mvvm::type_of(value);
    if (fields.reading() && !fields.node().attribute("type"))
    {
        throw std::invalid_argument("Flow scalar requires a type");
    }

    fields.choice("type", tag, std::array{"boolean", "integer", "number", "text"});
    if (fields.reading())
    {
        switch (tag)
        {
            case mvvm::value_type::boolean:
                value = false;
                break;
            case mvvm::value_type::integer:
                value = std::int64_t{};
                break;
            case mvvm::value_type::number:
                value = double{};
                break;
            case mvvm::value_type::text:
                value = std::string{};
                break;
        }
    }

    std::visit(
        [&](auto& data)
        {
            fields.value("value", data, true);
        },
        value);
}

/// @brief Encode a root or a child region using stable IDs.
void region_fields(detail::fields& fields, flow_region& value)
{
    fields.value("id", value.id, true);
    fields.value("initial", value.initial, true);
    fields.optional("owner", value.owner, !value.owner.empty());
    fields.optional("host", value.host, !value.host.empty());
    fields.optional("remember", value.remember, value.remember);
}

/// @brief Decode typed, persistent UI identities without paths.
void screen_fields(detail::fields& fields, flow_screen& value)
{
    fields.value("id", value.id, true);
    fields.value("region", value.region, true);
    auto kind = std::string{assets::format_type(value.asset.type)};
    fields.optional("asset-type", kind, value.asset.type != assets::asset_type::ui);
    if (fields.reading())
    {
        value.asset.type = assets::parse_type(kind);
    }

    fields.reference("asset", value.asset, value.asset.type);
    const auto policy = [&](const char* name, std::optional<bool>& item)
    {
        if (fields.reading())
        {
            if (fields.node().attribute(name))
            {
                item = false;
                fields.value(name, *item);
            }
        }
        else if (item)
        {
            fields.value(name, *item);
        }
    };
    policy("modal", value.popup.modal);
    policy("dismiss-outside", value.popup.dismiss_outside);
    policy("dismiss-back", value.popup.dismiss_back);
    fields.optional("focus", value.focus, !value.focus.empty());
}

/// @brief Match values to named guard parameters.
void parameter_fields(detail::fields& fields, flow_parameter& value)
{
    fields.value("id", value.id, true);
    scalar(fields, value.value);
}

/// @brief Apply the same exact scalar vocabulary to a transition guard.
void guard_fields(detail::fields& fields, flow_guard& value)
{
    fields.value("parameter", value.parameter, true);
    scalar(fields, value.equals);
}

/// @brief Encode one transition and an optional single guard.
void edge_fields(detail::fields& fields, flow_transition& value)
{
    fields.value("id", value.id, true);
    fields.value("from", value.from, true);
    fields.optional("to", value.to, !value.to.empty());
    if (fields.reading() && !fields.node().attribute("operation"))
    {
        throw std::invalid_argument("Flow transition requires an operation");
    }

    fields.choice("operation", value.operation,
                  std::array{"push", "replace", "pop", "back", "overlay", "reload", "exit_menu",
                             "close_popup"});
    fields.optional("enabled", value.enabled, !value.enabled);
    fields.optional("control", value.control, !value.control.empty());
    fields.optional("action", value.action, value.action != 0);
    if (fields.reading())
    {
        for (const auto child : fields.node().children())
        {
            if (child.type() == pugi::node_comment)
            {
                continue;
            }

            if (child.type() != pugi::node_element || std::string_view{child.name()} != "guard" ||
                value.guard)
            {
                throw std::invalid_argument("Unexpected flow transition child");
            }

            detail::fields guard{child, true};
            guard_fields(guard, value.guard.emplace());
            guard.finish();
            detail::expect_empty(child);
        }
    }
    else if (value.guard)
    {
        detail::fields guard{fields.node().append_child("guard"), false};
        guard_fields(guard, *value.guard);
    }
}

/// @brief Bound each sequence before appending a new untrusted declaration.
template <class T, class Codec>
void read_record(pugi::xml_node node, std::vector<T>& records, std::size_t limit, Codec codec)
{
    if (records.size() >= limit)
    {
        throw std::invalid_argument("Too many flow declarations");
    }

    detail::fields fields{node, true};
    codec(fields, records.emplace_back());
    fields.finish();
    if constexpr (!std::is_same_v<T, flow_transition>)
    {
        detail::expect_empty(node);
    }
}

/// @brief Emit the canonical declaration order for each record kind.
template <class T, class Codec>
void write_records(pugi::xml_node root, const char* name, const std::vector<T>& records,
                   Codec codec)
{
    for (const auto& source : records)
    {
        auto record = source;
        detail::fields fields{root.append_child(name), false};
        codec(fields, record);
    }
}

/// @brief Own the canonical output without filesystem access.
struct writer : pugi::xml_writer
{
    std::string output; ///< Bounded output bytes.

    void write(const void* bytes, std::size_t size) override
    {
        if (size > max_flow_bytes - output.size())
        {
            throw std::length_error("Flow XML exceeds its byte budget");
        }

        output.append(static_cast<const char*>(bytes), size);
    } ///< Append a chunk from the XML writer.
};
} // namespace

/// @brief Parse a strict versioned document and publish only a validated graph.
flow_document parse_flow_xml(xml_source source)
{
    pugi::xml_document tree;
    const auto root = detail::read_root(tree, source.bytes, max_flow_bytes);
    detail::fields fields{root, true};
    std::string version;
    fields.value("version", version, true);
    fields.finish();
    if (std::string_view{root.name()} != "flow" || (version != "1" && version != "2"))
    {
        throw std::invalid_argument(std::string{source.name} +
                                    ": Expected Flow XML version 1 or 2");
    }

    flow_document result;
    for (const auto node : root.children())
    {
        if (node.type() == pugi::node_comment)
        {
            continue;
        }

        if (node.type() != pugi::node_element)
        {
            throw std::invalid_argument("Unexpected flow document content");
        }

        const std::string_view name = node.name();
        if (name == "region")
        {
            read_record(node, result.regions, 64, region_fields);
        }
        else if (name == "screen")
        {
            read_record(node, result.screens, 256, screen_fields);
        }
        else if (name == "parameter")
        {
            read_record(node, result.parameters, 64, parameter_fields);
        }
        else if (name == "transition")
        {
            read_record(node, result.transitions, 1024, edge_fields);
        }
        else
        {
            throw std::invalid_argument("Unexpected flow declaration");
        }
    }

    if (version == "1" && detail::flow_v2(result))
    {
        throw std::invalid_argument("Flow presentation extensions require XML version 2");
    }

    validate_flow(result);
    return result;
}

/// @brief Serialize validated graph data for authoring or source-control round trips.
std::string write_flow_xml(const flow_document& source)
{
    validate_flow(source);
    pugi::xml_document tree;
    auto root = tree.append_child("flow");
    root.append_attribute("version").set_value(detail::flow_v2(source) ? "2" : "1");
    write_records(root, "region", source.regions, region_fields);
    write_records(root, "screen", source.screens, screen_fields);
    write_records(root, "parameter", source.parameters, parameter_fields);
    write_records(root, "transition", source.transitions, edge_fields);
    writer result;
    tree.save(result, "  ");
    return std::move(result.output);
}

/// @brief Load source graph data from the same typed catalog as its screens.
flow_document load_flow(assets::asset_ref source, const construction& options)
{
    if (source.type != assets::asset_type::flow)
    {
        throw std::invalid_argument("Flow load requires a flow asset");
    }

    const auto bytes = detail::acquire(options.storage.describe(source), max_flow_bytes, options);
    return parse_flow_xml({bytes, assets::format_id(source.id)});
}
} // namespace atlas::ui_content
