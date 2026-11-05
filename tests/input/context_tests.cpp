/** @file @brief Priority, consumption, held-control gating and deferred callback demands. */
#include "input_test_support.hpp"

namespace atlas::input::tests
{
/// @brief A modal map preempts lower actions but cannot activate from a previously held key.
TEST(input_contexts, CallbackRequestsApplyNextDispatchAndRequireNeutral)
{
    input_system system;
    system.define_action({1, "base", value_type::button});
    system.define_action({2, "modal", value_type::button});
    const std::array base{
        binding{1, {device_kind::keyboard, static_cast<std::uint32_t>(key::enter)}}};
    const std::array modal{
        binding{2, {device_kind::keyboard, static_cast<std::uint32_t>(key::enter)}}};
    system.define_context({1, "base", 0, true, activation_policy::require_neutral, base});
    system.define_context({2, "modal", 10, false, activation_policy::require_neutral, modal});
    recorder listener;
    listener.action = [&](const auto& a)
    {
        if (a.action == 1 && a.phase == action_phase::performed)
        {
            system.request_context(2, true);
        }
    };
    const std::array down{key_sample(key::enter, true)};
    system.dispatch(1, 0, down, listener);
    ASSERT_EQ(listener.actions.size(), 2);
    system.dispatch(2, 0.1, {}, listener);
    ASSERT_EQ(listener.actions.size(), 3);
    EXPECT_EQ(listener.actions.back().action, 1);
    EXPECT_EQ(listener.actions.back().phase, action_phase::cancelled);
    const std::array up{key_sample(key::enter, false)};
    system.dispatch(3, 0.2, up, listener);
    system.dispatch(4, 0.3, down, listener);
    EXPECT_EQ(listener.actions.back().action, 2);
    EXPECT_EQ(listener.actions.back().phase, action_phase::performed);
}

/// @brief Two bindings form a signed 2D composite, transformed independently of device family.
TEST(input_contexts, CompositeDirectionsAndRebindingRetainSemanticIdentity)
{
    input_system system;
    system.define_action(
        {7, "navigate", value_type::axis2d, std::make_shared<normalize_modifier>()});
    const std::array map{binding{7,
                                 {device_kind::keyboard, static_cast<std::uint32_t>(key::w)},
                                 std::make_shared<axis_modifier>(input_value{0, 1})},
                         binding{7,
                                 {device_kind::keyboard, static_cast<std::uint32_t>(key::d)},
                                 std::make_shared<axis_modifier>(input_value{1, 0})}};
    system.define_context({1, "ui", 0, true, activation_policy::require_neutral, map});
    recorder listener;
    const std::array keys{key_sample(key::w, true), key_sample(key::d, true)};
    system.dispatch(1, 0, keys, listener);
    EXPECT_NEAR(listener.actions.back().value.x, 0.70710678F, 0.00001F);
    EXPECT_NEAR(listener.actions.back().value.y, 0.70710678F, 0.00001F);
    const std::array replacement{binding{7, {device_kind::gamepad, 40}}};
    system.rebind(1, replacement);
    system.dispatch(2, 0.1, {}, listener);
    EXPECT_EQ(listener.actions.back().phase, action_phase::cancelled);
    const std::array pad{
        event{event_kind::control_changed, {9, device_kind::gamepad}, 40, {0.5F, -0.25F}}};
    system.dispatch(3, 0.2, pad, listener);
    EXPECT_EQ(listener.actions.back().action, 7);
    EXPECT_EQ(listener.actions.back().value, (input_value{0.5F, -0.25F}));
}

/// @brief Consumption is an authored policy rather than hardcoded priority-only suppression.
TEST(input_contexts, NonConsumingMapsCanShareAControl)
{
    input_system system;
    system.define_action({1, "one"});
    system.define_action({2, "two"});
    const std::array low{binding{1, {device_kind::keyboard, 13}}};
    const std::array high{binding{2, {device_kind::keyboard, 13}, {}, false}};
    system.define_context({1, "low", 0, true, activation_policy::allow_held, low});
    system.define_context({2, "high", 10, true, activation_policy::allow_held, high});
    recorder listener;
    const std::array down{key_sample(key::enter, true)};
    system.dispatch(1, 0, down, listener);
    EXPECT_EQ(listener.actions.size(), 4);
}
} // namespace atlas::input::tests
