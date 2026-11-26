/** @file @brief Optional keyboard activation without fabricated pointers or accessibility metadata.
 */
#include "input_state.hpp"
#include <utility>

namespace atlas::ui
{
/// @brief Configure only between frames; custom raw handlers always have first refusal.
void input_session::configure_keyboard_activation(bool enabled)
{
    impl_->idle();
    impl_->keyboard_activation = enabled;
    impl_->activation_target = {};
    impl_->activation_key = 0;
}

/// @brief Clear a consumed key's default press so a later release cannot activate it.
void input_session::impl::process_key(input_event raw)
{
    const bool activates =
        keyboard_activation && (raw.key == static_cast<unsigned>(input::key::enter) ||
                                raw.key == static_cast<unsigned>(input::key::space));
    if (activates && raw.kind == input_kind::key_down && !activation_key)
    {
        activation_target = focus_target;
        activation_key = raw.key;
    }

    if (raw.key == static_cast<unsigned>(input::key::shift))
    {
        focus_shift = raw.kind == input_kind::key_down;
    }

    if (route(raw, focus_target,
              raw.kind == input_kind::key_down ? event_kind::key_down : event_kind::key_up))
    {
        if (activation_key == raw.key)
        {
            activation_target = {};
            if (raw.kind == input_kind::key_up)
            {
                activation_key = 0;
            }
        }

        return;
    }

    if (!keyboard_activation_packet(raw))
    {
        (void)focus_navigation_packet(raw, true);
    }
}

/// @brief Only an eligible original focus can receive the matching key-release activation.
bool input_session::impl::keyboard_activation_packet(input_event raw)
{
    if (!keyboard_activation || (raw.key != static_cast<unsigned>(input::key::enter) &&
                                 raw.key != static_cast<unsigned>(input::key::space)))
    {
        return false;
    }

    if (raw.kind == input_kind::key_down)
    {
        return true;
    }

    if (raw.key != activation_key)
    {
        return true;
    }

    const auto target = std::exchange(activation_target, {});
    activation_key = 0;
    if (target.generation && target == focus_target && eligible(target))
    {
        (void)route(raw, target, event_kind::activate);
    }

    return true;
}
} // namespace atlas::ui
