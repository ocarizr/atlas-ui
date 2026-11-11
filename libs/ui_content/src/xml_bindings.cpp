/** @file @brief Typed model property, action and script declarations in UI XML. */
#include "xml_fields.hpp"

namespace atlas::ui_content::detail
{
namespace
{
/// @brief Decode an exact scalar variant without converting strings or numeric types implicitly.
void argument(fields& xml, mvvm::value& data)
{
    auto type = mvvm::type_of(data);
    xml.choice("type", type, std::array{"boolean", "integer", "number", "text"});
    if (xml.reading())
    {
        switch (type)
        {
            case mvvm::value_type::boolean:
                data = false;
                break;
            case mvvm::value_type::integer:
                data = std::int64_t{};
                break;
            case mvvm::value_type::number:
                data = 0.;
                break;
            case mvvm::value_type::text:
                data = std::string{};
                break;
        }
    }

    std::visit(
        [&](auto& scalar)
        {
            xml.value("value", scalar, true);
        },
        data);
}

/// @brief Encode model names and scalar property vocabulary, including explicit reverse writes.
void property_fields(fields& xml, property_binding& data)
{
    xml.value("model", data.model, true);
    xml.value("field", data.binding.source, true);
    if (xml.reading() && !xml.node().attribute("property"))
    {
        throw std::invalid_argument("Missing UI binding property");
    }

    xml.choice("property", data.binding.target,
               std::array{"width", "height", "opacity", "visible"});
    xml.choice("mode", data.binding.mode, std::array{"one-way", "two-way"});
}

/// @brief Serialize semantic input actions and their route/phase filters by authored name.
void action_fields(fields& xml, action_binding& data)
{
    xml.value("model", data.model, true);
    auto& route = data.binding;
    xml.value("name", route.action, true);
    xml.choice("event", route.event,
               std::array{"start",         "enable",        "disable",      "end",
                          "process-input", "tick",          "late-tick",    "end-frame",
                          "pointer-move",  "pointer-down",  "pointer-up",   "pointer-cancel",
                          "pointer-enter", "pointer-leave", "capture-lost", "key-down",
                          "key-up",        "focus-gained",  "focus-lost",   "action",
                          "ensure-visible"});
    xml.choice("route", route.route, std::array{"direct", "capture", "target", "bubble"});
    if (route.event == ui::event_kind::action)
    {
        xml.value("input-action", route.input_action, true);
        xml.choice("input-phase", route.input_phase,
                   std::array{"started", "ongoing", "performed", "completed", "cancelled"});
    }
}

/// @brief Script source and model context form one isolated shared-domain registration.
void behavior_fields(fields& xml, behavior& data)
{
    xml.reference("asset", data.source, assets::asset_type::script);
    xml.value("model", data.model, true);
}

/// @brief Admit a bounded ordered list of exact action arguments.
void read_arguments(pugi::xml_node node, std::vector<mvvm::value>& target)
{
    for (auto child : node.children())
    {
        if (child.type() == pugi::node_comment)
        {
            continue;
        }

        if (child.type() != pugi::node_element || std::string_view{child.name()} != "arg" ||
            target.size() == 32)
        {
            throw std::invalid_argument("Unexpected UI action argument or argument limit exceeded");
        }

        fields xml{child, true};
        if (!child.attribute("type"))
        {
            throw std::invalid_argument("Missing UI action argument type");
        }

        argument(xml, target.emplace_back());
        xml.finish();
        expect_empty(child);
    }
}
} // namespace

/// @brief Reject unknown sections and enforce per-element descriptor limits before appending.
void read_binding(pugi::xml_node node, element& target)
{
    fields xml{node, true};
    const std::string_view name = node.name();
    if (name == "bind" && target.properties.size() < 4)
    {
        property_fields(xml, target.properties.emplace_back());
    }
    else if (name == "action" && target.actions.size() < 64)
    {
        auto& action = target.actions.emplace_back();
        action_fields(xml, action);
        read_arguments(node, action.binding.arguments);
        xml.finish();
        return;
    }
    else if (name == "behavior" && !target.script)
    {
        behavior_fields(xml, target.script.emplace());
    }
    else
    {
        throw std::invalid_argument("Unexpected or excess UI descriptor: " + std::string{name});
    }

    xml.finish();
    expect_empty(node);
}

/// @brief Emit declarations without runtime schema slots or Lua registry references.
void write_bindings(pugi::xml_node node, const element& source)
{
    for (auto property : source.properties)
    {
        fields xml{node.append_child("bind"), false};
        property_fields(xml, property);
    }

    for (auto action : source.actions)
    {
        auto target = node.append_child("action");
        fields xml{target, false};
        action_fields(xml, action);
        for (auto& value : action.binding.arguments)
        {
            fields xml_arg{target.append_child("arg"), false};
            argument(xml_arg, value);
        }
    }

    if (source.script)
    {
        auto script = *source.script;
        fields xml{node.append_child("behavior"), false};
        behavior_fields(xml, script);
    }
}
} // namespace atlas::ui_content::detail
