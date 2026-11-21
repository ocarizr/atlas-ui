/** @file @brief UI-asset-owned semantic events bind to permitted Flow routes or typed callables. */
#include "flow_panel.hpp"
#include "flow_values.hpp"
#include <algorithm>

namespace atlas::editor
{
/// @brief Event metadata is stored in the UI asset and resolves against a selected Flow context.
void flow_panel::inspect_events(std::vector<control>& rows, const document_session& ui)
{
    const auto id = std::string{ui.selected()};
    const auto control = std::ranges::find(ui.document().elements, id, &ui_content::element::id);
    if (control == ui.document().elements.end())
    {
        return;
    }

    rows.push_back({{}, {0, 0, 0, 0}, "UI control: " + id});
    const auto supported = editor_flow::supported_events(*control);
    if (supported.empty())
    {
        button(rows, "Enable control events and focus", "control-events");
        return;
    }

    const std::array names{"Click / semantic activation", "Focus enter", "Focus exit"};
    button(rows, names[static_cast<std::size_t>(binding_.event)], "event");
    field(rows, "tab_region", "Tab Group region (empty: menu)", binding_.region,
          [this](std::string value)
          {
              binding_.region = std::move(value);
          });
    button(rows, binding_.action.empty() ? "Choose action | next" : "Action: " + binding_.action,
           "action");
    if (binding_.action == "atlas.navigate_to")
    {
        button(rows, "Allowed route: " + binding_.route + " | next", "event-route");
    }
    else if (binding_.action != "atlas.navigate_to" && binding_.action != "atlas.exit_menu" &&
             binding_.action != "atlas.close_popup")
    {
        field(rows, "action_context", "Model context", binding_.context,
              [this](std::string value)
              {
                  binding_.context = std::move(value);
              });
        const auto descriptor =
            std::ranges::find(descriptors_, binding_.action, &scripting::callable_descriptor::id);
        if (descriptor != descriptors_.end())
        {
            rows.push_back({{}, {0, 0, 0, 0}, descriptor->category + " | " + descriptor->label});
            for (std::size_t i = 0; i < binding_.arguments.size(); ++i)
            {
                field(rows, "action_arg_" + std::to_string(i), descriptor->parameters[i].name,
                      flow_value(binding_.arguments[i]),
                      [this, i](const std::string& value)
                      {
                          auto arguments = binding_.arguments;
                          arguments.at(i) = parse_flow_value(arguments.at(i), value);
                          const auto descriptor = std::ranges::find(
                              descriptors_, binding_.action, &scripting::callable_descriptor::id);
                          if (descriptor == descriptors_.end())
                          {
                              throw std::invalid_argument("Action descriptor is unavailable");
                          }

                          editor_flow::validate_arguments(*descriptor, arguments);
                          binding_.arguments = std::move(arguments);
                      });
            }
        }
    }

    field(rows, "input_action", "Semantic input action ID (0: pointer)",
          std::to_string(binding_.input_action),
          [this](std::string value)
          {
              std::uint32_t result{};
              const auto parsed =
                  std::from_chars(value.data(), value.data() + value.size(), result);
              if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
              {
                  throw std::invalid_argument("Expected an unsigned input action ID");
              }

              binding_.input_action = result;
          });
    button(rows, "Bind event in UI document", "bind");
    for (std::size_t i = 0; i < control->events.size(); ++i)
    {
        const auto& event = control->events[i];
        rows.push_back({{},
                        {0, 0, 0, 0},
                        std::string{names[static_cast<std::size_t>(event.event)]} + " -> " +
                            event.action + " " + event.route});
        button(rows, "Edit binding " + std::to_string(i), "event-edit:" + std::to_string(i));
        button(rows, "Remove binding " + std::to_string(i), "event-remove:" + std::to_string(i));
    }
}
} // namespace atlas::editor
