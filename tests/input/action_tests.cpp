/** @file @brief Actions consume portable events rather than device polling by applications. */
#include "input_test_support.hpp"

namespace atlas::input::tests
{
/// @brief Wheel packets are transient observations, never latched button/axis state.
TEST(input_actions, WheelNotchesDoNotBecomeHeldControlState)
{
    input_system system;
    system.define_action({1, "scroll", value_type::axis2d});
    const std::array bindings{binding{1, {device_kind::pointer, pointer_control::wheel}}};
    system.define_context({1, "ui", 0, true, activation_policy::allow_held, bindings});
    recorder listener;
    const std::array packet{event{event_kind::pointer_scroll,
                                  {2, device_kind::pointer},
                                  pointer_control::wheel,
                                  {0, 0.5f},
                                  0,
                                  {13, 17}}};
    system.dispatch(1, 0, packet, listener);
    ASSERT_EQ(listener.raw.size(), 1U);
    EXPECT_EQ(listener.raw[0].position, (input_value{13, 17}));
    EXPECT_TRUE(listener.actions.empty());
    system.dispatch(2, 0, {}, listener);
    EXPECT_TRUE(listener.actions.empty());
}

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
