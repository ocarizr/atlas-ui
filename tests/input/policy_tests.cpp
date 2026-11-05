/** @file @brief Processor policy, admission boundaries and isolated source routing. */
#include "input_test_support.hpp"
#include <limits>

namespace atlas::input::tests
{
/// @brief Invalid policy is rejected before entering a live action map.
TEST(input_policy, RejectsInvalidConfigurationAndProcessorPolicy)
{
    input_system system;
    EXPECT_THROW(system.define_action({0, "invalid"}), std::invalid_argument);
    system.define_action({1, "confirm"});
    EXPECT_THROW(system.define_action({1, "duplicate"}), std::invalid_argument);
    const std::array unknown{binding{2, {device_kind::keyboard, 13}}};
    EXPECT_THROW(system.define_context({1, "bad", 0, true, activation_policy::allow_held, unknown}),
                 std::invalid_argument);
    EXPECT_THROW(system.request_context(1, true), std::invalid_argument);
    EXPECT_THROW(deadzone_modifier(1), std::invalid_argument);
    EXPECT_THROW(hold_trigger(0), std::invalid_argument);
    EXPECT_THROW(tap_trigger(-1), std::invalid_argument);
    EXPECT_THROW(modifier_chain({nullptr}), std::invalid_argument);
}

/// @brief Radial deadzone preserves direction and normalization limits diagonal magnitude.
TEST(input_policy, ComposesFiniteModifiersInDeclaredOrder)
{
    const modifier_chain chain{{std::make_shared<deadzone_modifier>(0.25F),
                                std::make_shared<scale_modifier>(input_value{2, 2}),
                                std::make_shared<normalize_modifier>()}};
    EXPECT_EQ(chain.apply({0.1F, 0}), input_value{});
    EXPECT_NEAR(chain.apply({0.5F, 0}).x, 2.F / 3, 0.00001F);
    const auto diagonal = chain.apply({1, 1});
    EXPECT_NEAR(diagonal.x, 0.70710678F, 0.00001F);
    EXPECT_NEAR(diagonal.y, 0.70710678F, 0.00001F);
}

/// @brief Device/source selectors prevent a second window or instance activating a map.
TEST(input_policy, FiltersSourcesAndDevicesWithoutLosingOtherHeldInstances)
{
    input_system system;
    system.define_action({1, "confirm"});
    const std::array map{binding{1, {device_kind::gamepad, 1}, {}, true, 7, 4}};
    system.define_context({1, "player", 0, true, activation_policy::allow_held, map});
    recorder listener;
    const std::array other{
        event{event_kind::control_changed, {8, device_kind::gamepad}, 1, {1, 0}, 4},
        event{event_kind::control_changed, {7, device_kind::gamepad}, 1, {1, 0}, 5}};
    system.dispatch(1, 0, other, listener);
    EXPECT_TRUE(listener.actions.empty());
    const std::array own{
        event{event_kind::control_changed, {7, device_kind::gamepad}, 1, {1, 0}, 4}};
    system.dispatch(2, 0.1, own, listener);
    ASSERT_EQ(listener.actions.size(), 2);
    const std::array gone{
        event{event_kind::device_disconnected, {8, device_kind::gamepad}, 0, {}, 4}};
    system.dispatch(3, 0.2, gone, listener);
    EXPECT_EQ(listener.actions.size(), 2);
}

/// @brief Whole-batch syntax rejection preserves the frame and suppresses all callbacks.
TEST(input_policy, InvalidCoordinatesAndTimeRejectBeforePublication)
{
    input_system system;
    recorder listener;
    auto invalid = key_sample(key::a, true);
    invalid.position.x = std::numeric_limits<float>::infinity();
    const std::array packets{key_sample(key::b, true), invalid};
    EXPECT_THROW(system.dispatch(1, 0, packets, listener), std::invalid_argument);
    EXPECT_TRUE(listener.raw.empty());
    EXPECT_EQ(system.statistics().frame, 0);
    system.dispatch(1, 1, {}, listener);
    EXPECT_THROW(system.dispatch(2, 0, {}, listener), std::invalid_argument);
    EXPECT_THROW(system.dispatch(1, 1, {}, listener), std::invalid_argument);
}

/// @brief Delivery limits latch failure; structural callback reentry is rejected.
TEST(input_policy, CallbackBudgetAndReentryFailWithExplicitRecovery)
{
    input_system system{{.max_callbacks = 1}};
    system.define_action({1, "confirm"});
    const std::array map{binding{1, {device_kind::keyboard, 13}}};
    system.define_context({1, "ui", 0, true, activation_policy::allow_held, map});
    recorder listener;
    const std::array down{key_sample(key::enter, true)};
    EXPECT_THROW(system.dispatch(1, 0, down, listener), std::overflow_error);
    EXPECT_TRUE(system.statistics().failed);
    system.reset();
    EXPECT_NO_THROW(system.dispatch(2, 0.1, {}, listener));
    listener.action = [&](const auto&)
    {
        system.define_action({2, "forbidden"});
    };
    // A separate mapper has room to enter the listener rather than failing its budget.
    input_system other;
    other.define_action({1, "confirm"});
    other.define_context({1, "ui", 0, true, activation_policy::allow_held, map});
    listener.action = [&](const auto&)
    {
        other.define_action({2, "forbidden"});
    };
    EXPECT_THROW(other.dispatch(1, 0, down, listener), std::logic_error);
    EXPECT_TRUE(other.statistics().failed);
}
} // namespace atlas::input::tests
