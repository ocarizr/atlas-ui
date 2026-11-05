/** @file @brief Generic viewport interaction demonstration without application-specific widgets. */
#pragma once
#include <atlas/ui/input_session.hpp>

namespace atlas::application
{
class demo_interaction final : public ui::input_handler
{
  public:
    ui::event_response on_event(const ui::routed_event& event,
                                ui::command_buffer& commands) override
    {
        switch (event.kind)
        {
            case ui::event_kind::pointer_enter:
                hovered_ = true;
                break;
            case ui::event_kind::pointer_leave:
                hovered_ = false;
                break;
            case ui::event_kind::focus_gained:
                focused_ = true;
                break;
            case ui::event_kind::focus_lost:
                focused_ = false;
                pressed_ = false;
                break;
            case ui::event_kind::pointer_down:
                if (event.phase == ui::route_phase::target)
                {
                    pressed_ = true;
                    return {.capture_pointer = true, .focus = true};
                }
                break;
            case ui::event_kind::pointer_up:
            case ui::event_kind::pointer_cancel:
            case ui::event_kind::capture_lost:
                pressed_ = false;
                break;
            case ui::event_kind::action:
                if (event.phase == ui::route_phase::target && event.action)
                {
                    if (event.action->action == 1)
                    {
                        pressed_ = event.action->phase == input::action_phase::started ||
                                   event.action->phase == input::action_phase::performed;
                    }

                    if (event.action->action == 2 &&
                        event.action->phase == input::action_phase::performed)
                    {
                        return {.blur = true};
                    }
                }
                break;
            case ui::event_kind::late_tick:
            {
                const auto opacity = pressed_ ? 0.6F : hovered_ || focused_ ? 1.F : 0.85F;
                if (opacity != published_)
                {
                    commands.set(event.current, ui::property::opacity, opacity,
                                 std::uint64_t{event.current.index} + 1);
                    published_ = opacity;
                }

                break;
            }
            default:
                break;
        }

        return {};
    } ///< Coalesce local demands and emit at most one absolute opacity write per frame.
  private:
    bool hovered_ = false; ///< Serial geometric hover state.
    bool focused_ = false; ///< Serial focus state.
    bool pressed_ = false; ///< Capture-backed press state.
    float published_ = 1;  ///< Last admitted opacity request.
}; ///< Expensive work remains in later shared layout/paint/recording phases.
} // namespace atlas::application
