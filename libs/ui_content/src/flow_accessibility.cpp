/** @file @brief Flow accessibility shares active layers, modal policy and exclusive keyboard
 * ownership. */
#include "flow_viewport_state.hpp"
#include <algorithm>

namespace atlas::ui_content
{
/// @brief Admit requests only into live foreground owner phases, preserving normal focus routing.
void flow_viewport::impl::accessibility_begin(const flow_frame_options& options)
{
    if (options.accessibility)
    {
        for (std::size_t i = 0; i < layers.size(); ++i)
        {
            auto& layer = layers[i];
            if (!layer.item.input)
            {
                continue;
            }

            auto& input = layer.item.view->input();
            for (const auto& request : options.accessibility->requests())
            {
                const ui::element_handle target{request.target.index, request.target.generation,
                                                request.target.session};
                if (request.operation == accessibility::action::focus &&
                    request.target.session == layer.source.session() && input.can_focus(target))
                {
                    focus(i);
                }
            }

            options.accessibility->route(input);
        }
    }
}

/// @brief Editor chrome and other independent domains cannot also consume preview keyboard input.
void flow_viewport::impl::accessibility_route(const ui::input_event& event,
                                              const flow_frame_options& options)
{
    const auto keyboard_owner = options.accessibility ? options.accessibility->focus_owner() : 0;
    const bool keyboard_event = event.kind == ui::input_kind::key_down ||
                                event.kind == ui::input_kind::key_up ||
                                event.kind == ui::input_kind::action;
    if (keyboard_owner && keyboard_event &&
        std::ranges::none_of(layers,
                             [&](const auto& layer)
                             {
                                 return layer.item.input &&
                                        layer.source.session() == keyboard_owner;
                             }))
    {
        return;
    }

    route(event, options.back_action);
}

/// @brief Capture active views only, after their native and physical input requests have run.
void flow_viewport::impl::accessibility_capture(const flow_frame_options& options)
{
    if (options.accessibility)
    {
        for (auto& layer : layers)
        {
            if (layer.item.input)
            {
                options.accessibility->capture(layer.item.view->input(), options.accessible_text);
            }
        }
    }
}
} // namespace atlas::ui_content
