/** @file @brief Exclusive keyboard/capture routing and fully consumed popup dismissal gestures. */
#include "flow_viewport_state.hpp"
#include <algorithm>
#include <atlas/ui/root_presentation.hpp>

namespace atlas::ui_content
{
namespace
{
/// @brief Half-open boundaries match Atlas hit testing, including empty clipped regions.
bool inside(ui::layout_rect bounds, ui::layout_point point)
{
    return point.x >= bounds.x && point.y >= bounds.y && point.x < bounds.x + bounds.width &&
           point.y < bounds.y + bounds.height;
}
} // namespace

/// @brief Save authored focus before transferring exclusive keyboard ownership.
void flow_viewport::impl::focus(std::size_t selected)
{
    if (selected >= layers.size() || !layers[selected].item.input)
    {
        return;
    }

    const auto chosen = layers[selected].item.screen;
    for (auto& layer : layers)
    {
        if (!layer.item.input || layer.item.screen == chosen)
        {
            continue;
        }

        auto& view = *layer.item.view;
        const auto current = view.input().focused();
        if (current.generation)
        {
            bookmarks[std::string{layer.item.screen}] = view.authored_id(current);
            view.input().focus({});
        }
    }

    auto& view = *layers[selected].item.view;
    if (keyboard != chosen && !view.input().focused().generation)
    {
        if (const auto found = bookmarks.find(std::string{chosen}); found != bookmarks.end())
        {
            try
            {
                const auto target = view.find(found->second);
                if (view.input().can_focus(target))
                {
                    view.input().focus(target);
                }
            }
            catch (const std::out_of_range&)
            {
                bookmarks.erase(std::string{chosen});
            } // A rebuilt screen can remove its old control.
        }
    }

    keyboard = chosen;
}

/// @brief A view losing the pointer receives leave instead of retaining a ghost hover state.
void flow_viewport::impl::deliver(std::size_t target, const ui::input_event& event)
{
    const ui::input_event leave{ui::input_kind::pointer_leave, event.position};
    for (std::size_t index = 0; index < layers.size(); ++index)
    {
        if (index != target && layers[index].item.input &&
            layers[index].item.view->input().hovered().generation)
        {
            layers[index].item.view->input().process_more({&leave, 1});
        }
    }

    if (target < layers.size())
    {
        layers[target].item.view->input().process_more({&event, 1});
    }
}

/// @brief Route system/keyboard input before choosing a single pointer recipient.
void flow_viewport::impl::route(const ui::input_event& event, std::uint32_t back_action)
{
    if (swallow_release &&
        (event.kind == ui::input_kind::pointer_up || event.kind == ui::input_kind::pointer_cancel))
    {
        swallow_release = false;
        return;
    }

    const bool back = (event.kind == ui::input_kind::key_down && event.key == 27) ||
                      (event.kind == ui::input_kind::action && event.action && back_action &&
                       event.action->action == back_action &&
                       event.action->phase == input::action_phase::performed);
    if (back)
    {
        for (auto it = layers.rbegin(); it != layers.rend(); ++it)
        {
            if (!it->item.input)
            {
                continue;
            }

            if (const auto popup = it->item.popup)
            {
                if (popup->dismiss_back)
                {
                    (void)flow.close_popup(it->item.screen);
                }
            }
            else
            {
                (void)flow.exit_menu(it->item.screen);
            }

            return;
        }
    }

    if (event.kind == ui::input_kind::window_blur || event.kind == ui::input_kind::pointer_cancel ||
        event.kind == ui::input_kind::pointer_leave)
    {
        if (event.kind == ui::input_kind::window_blur)
        {
            swallow_release = false;
        }

        for (auto& layer : layers)
        {
            if (layer.item.input)
            {
                layer.item.view->input().process_more({&event, 1});
            }
        }

        return;
    }

    if (event.kind > ui::input_kind::pointer_leave)
    {
        for (auto& layer : layers)
        {
            if (layer.item.input && layer.item.screen == keyboard)
            {
                layer.item.view->input().process_more({&event, 1});
                return;
            }
        }

        return;
    }

    if (event.kind == ui::input_kind::pointer_down && !inside(viewport, event.position))
    {
        deliver(layers.size(), event);
        return;
    }

    route_pointer(event);
}

/// @brief Capture wins over modeless hit routing; modal/outside dismissal never leaks a click.
void flow_viewport::impl::route_pointer(const ui::input_event& event)
{
    const auto captured = std::ranges::find_if(
        layers,
        [](const auto& value)
        {
            return value.item.input && value.item.view->input().captured().generation != 0;
        });
    if (captured != layers.end())
    {
        deliver(static_cast<std::size_t>(captured - layers.begin()), event);
        return;
    }

    for (std::size_t index = layers.size(); index > 0; --index)
    {
        auto& layer = layers[index - 1];
        if (!layer.item.input)
        {
            continue;
        }

        const bool hit =
            inside(layer.bounds, event.position) &&
            (!layer.item.popup ||
             ui::root_visual_contains(layer.source, layer.geometry,
                                      layer.source.elements().front().target, event.position));
        if (layer.item.popup && !hit)
        {
            if (event.kind == ui::input_kind::pointer_down && layer.item.popup->dismiss_outside)
            {
                (void)flow.close_popup(layer.item.screen);
                swallow_release = true;
                deliver(layers.size(), event);
                return;
            }

            if (layer.item.popup->modal)
            {
                deliver(layers.size(), event);
                return;
            }
        }

        if (hit &&
            (layer.item.popup || layer.item.view->input().hit_test(event.position).generation))
        {
            if (event.kind == ui::input_kind::pointer_down)
            {
                focus(index - 1);
            }

            deliver(index - 1, event);
            return;
        }
    }

    deliver(layers.size(), event);
}
} // namespace atlas::ui_content
