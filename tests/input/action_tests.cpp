/** @file @brief Actions consume portable events rather than device polling by applications. */
#include "input_test_support.hpp"

namespace atlas::input::tests
{
/// @brief Keyboard and virtual gamepad bindings drive the same action identity.
TEST(input_actions, ButtonTransitionsAndMultipleDevicesUseOneSemanticAction)
{
    input_system system;
    system.define_action({1, "confirm", value_type::button});
    const std::array bindings{
        binding{1, {device_kind::keyboard, static_cast<std::uint32_t>(key::enter)}},
        binding{1, {device_kind::gamepad, 1}}};
    system.define_context({1, "ui", 0, true, activation_policy::require_neutral, bindings});
    recorder listener;
    const std::array down{event{event_kind::control_changed,
                                {1, device_kind::keyboard},
                                static_cast<std::uint32_t>(key::enter),
                                {1, 0}}};
    system.dispatch(1, 0, down, listener);
    ASSERT_EQ(listener.actions.size(), 2);
    EXPECT_EQ(listener.actions[0].phase, action_phase::started);
    EXPECT_EQ(listener.actions[1].phase, action_phase::performed);
    const std::array up{event{event_kind::control_changed,
                              {1, device_kind::keyboard},
                              static_cast<std::uint32_t>(key::enter),
                              {0, 0}}};
    system.dispatch(2, 0.1, up, listener);
    EXPECT_EQ(listener.actions.back().phase, action_phase::completed);
    const std::array pad{event{event_kind::control_changed, {7, device_kind::gamepad}, 1, {1, 0}}};
    system.dispatch(3, 0.2, pad, listener);
    EXPECT_EQ(listener.actions.back().action, 1);
    EXPECT_EQ(listener.actions.back().phase, action_phase::performed);
}
} // namespace atlas::input::tests
