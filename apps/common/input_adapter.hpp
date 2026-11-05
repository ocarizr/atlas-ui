/** @file @brief Bounded application bridge from reusable input actions to UI routing. */
#pragma once
#include <atlas/input/key.hpp>
#include <atlas/ui/input_session.hpp>

namespace atlas::application
{
/// @brief One owner-dispatch recording; overflow is explicit rather than silently dropping
/// releases.
class ui_input_batch final : public input::input_listener
{
  public:
    explicit ui_input_batch(std::pmr::memory_resource* memory)
        : events_(memory ? memory : std::pmr::new_delete_resource())
    {
        events_.reserve(4096);
    } ///< Reserve bounded reusable bridge metadata at application composition.

    void clear()
    {
        events_.clear();
    } ///< Begin the next owner-dispatch recording.

    void on_input(const input::event& event) override
    {
        ui::input_event packet;
        switch (event.kind)
        {
            case input::event_kind::pointer_motion:
                packet.kind = ui::input_kind::pointer_move;
                packet.position = {event.value.x, event.value.y};
                break;
            case input::event_kind::control_changed:
                if (event.device.kind == input::device_kind::keyboard)
                {
                    packet.kind =
                        event.value.x != 0 ? ui::input_kind::key_down : ui::input_kind::key_up;
                    packet.key = event.control;
                }
                else if (event.device.kind == input::device_kind::pointer &&
                         event.control == input::pointer_control::primary)
                {
                    packet.kind = event.value.x != 0 ? ui::input_kind::pointer_down
                                                     : ui::input_kind::pointer_up;
                    packet.position = {event.position.x, event.position.y};
                }
                else
                {
                    return;
                }
                break;
            case input::event_kind::pointer_cancel:
                packet.kind = ui::input_kind::pointer_cancel;
                break;
            case input::event_kind::pointer_leave:
                packet.kind = ui::input_kind::pointer_leave;
                break;
            case input::event_kind::focus_lost:
            case input::event_kind::source_reset:
                packet.kind = ui::input_kind::window_blur;
                break;
            default:
                return;
        }

        append(packet);
    } ///< Physical samples preserve order alongside semantic action callbacks.

    void on_action(const input::action_event& event) override
    {
        ui::input_event packet;
        packet.kind = ui::input_kind::action;
        packet.action = event;
        append(packet);
    } ///< UI receives action identity/value without a device-code dependency.

    /// @brief Borrow normalized packets only during this owner dispatch.
    std::span<const ui::input_event> values() const
    {
        return events_;
    }

    ///< Borrow only until this synchronous bridge is destroyed.
  private:
    void append(ui::input_event event)
    {
        if (events_.size() == 4096)
        {
            throw std::overflow_error("UI input bridge capacity exhausted");
        }

        events_.push_back(event);
    } ///< Never overwrite a release or semantic cancellation.

    std::pmr::vector<ui::input_event> events_; ///< Reserved bounded bridge storage.
};
} // namespace atlas::application
