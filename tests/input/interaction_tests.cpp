/** @file @brief Hold/tap timing and reset/disconnection cancellation use a caller replay clock. */
#include "input_test_support.hpp"

namespace atlas::input::tests
{
/// @brief Timed interactions advance on empty dispatch without blocking for a deadline.
TEST(input_interactions, HoldPerformsOnceAtTheDeadlineThenCompletesOnRelease)
{
    input_system system;
    system.define_action({1, "hold", value_type::button, {}, std::make_shared<hold_trigger>(0.5)});
    const std::array map{binding{1, {device_kind::keyboard, 13}}};
    system.define_context({1, "ui", 0, true, activation_policy::require_neutral, map});
    recorder listener;
    const std::array down{key_sample(key::enter, true)};
    system.dispatch(1, 0, down, listener);
    system.dispatch(2, 0.25, {}, listener);
    system.dispatch(3, 0.5, {}, listener);
    system.dispatch(4, 0.75, {}, listener);
    const std::array up{key_sample(key::enter, false)};
    system.dispatch(5, 1, up, listener);
    EXPECT_EQ(phases(listener),
              (std::vector{action_phase::started, action_phase::ongoing, action_phase::ongoing,
                           action_phase::performed, action_phase::completed}));
}

/// @brief A short tap performs on release; an expired tap cancels once until neutral.
TEST(input_interactions, TapDistinguishesReleaseFromTimeout)
{
    input_system system;
    system.define_action({1, "tap", value_type::button, {}, std::make_shared<tap_trigger>(0.3)});
    const std::array map{binding{1, {device_kind::keyboard, 13}}};
    system.define_context({1, "ui", 0, true, activation_policy::require_neutral, map});
    recorder listener;
    const std::array down{key_sample(key::enter, true)}, up{key_sample(key::enter, false)};
    system.dispatch(1, 0, down, listener);
    system.dispatch(2, 0.2, up, listener);
    EXPECT_EQ(listener.actions[2].phase, action_phase::performed);
    EXPECT_EQ(listener.actions.back().phase, action_phase::completed);
    system.dispatch(3, 1, down, listener);
    system.dispatch(4, 1.5, {}, listener);
    const auto count = listener.actions.size();
    EXPECT_EQ(listener.actions.back().phase, action_phase::cancelled);
    system.dispatch(5, 1.6, up, listener);
    EXPECT_EQ(listener.actions.size(), count);
}

/// @brief Source loss and gamepad removal cancel rather than completing a normal release.
TEST(input_interactions, DisconnectAndSourceResetClearLatchedControls)
{
    input_system system;
    system.define_action({1, "confirm"});
    const std::array map{binding{1, {device_kind::gamepad, 1}}};
    system.define_context({1, "ui", 0, true, activation_policy::require_neutral, map});
    recorder listener;
    const std::array down{
        event{event_kind::control_changed, {7, device_kind::gamepad}, 1, {1, 0}, 4}};
    system.dispatch(1, 0, down, listener);
    const std::array gone{
        event{event_kind::device_disconnected, {7, device_kind::gamepad}, 0, {}, 4}};
    system.dispatch(2, 0.1, gone, listener);
    EXPECT_EQ(listener.actions.back().phase, action_phase::cancelled);
    system.dispatch(3, 0.2, down, listener);
    const std::array reset{event{event_kind::source_reset, {}, 0, {}, 4}};
    system.dispatch(4, 0.3, reset, listener);
    EXPECT_EQ(listener.actions.back().phase, action_phase::cancelled);
    const auto count = listener.actions.size();
    system.dispatch(5, 0.4, {}, listener);
    EXPECT_EQ(listener.actions.size(), count);
}
} // namespace atlas::input::tests
