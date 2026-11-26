/** @file @brief Window-level accessible composition preserves identities and exclusive focus. */
#include "input_test_support.hpp"
#include <atlas/ui/accessibility_frame.hpp>

namespace atlas::ui::tests
{
/// @brief Unchanged captures retain their shared snapshot instead of publishing redundant values.
TEST_F(input_fixture, AccessibilityFrameRetainsUnchangedPublication)
{
    const auto target = tree.create();
    tree.set_layout(target, {.intrinsic = {20, 20}});
    input.configure_accessibility(target, {.name = "Settings"});
    begin();
    accessibility::channel channel;
    accessibility_frame first{channel, {.x = -100, .y = 30, .scale = 2}};
    first.route(input);
    first.capture(input);
    first.publish();
    const auto snapshot = channel.read();
    ASSERT_EQ(snapshot->nodes().size(), 1);
    EXPECT_EQ(snapshot->nodes().front().bounds.x, -100);
    accessibility_frame second{channel, {.x = -100, .y = 30, .scale = 2}};
    second.route(input);
    second.capture(input);
    second.publish();
    EXPECT_EQ(snapshot, channel.read());
    accessibility_frame hidden{channel};
    hidden.clear();
    hidden.publish();
    EXPECT_TRUE(channel.read()->nodes().empty());
}

/// @brief A native request enters the normal serial route and preserves focus-versus-activation.
TEST_F(input_fixture, AccessibilityFrameDeliversQueuedFocusOnItsOwner)
{
    const auto target = tree.create();
    input.bind(target, handler, {.focusable = true});
    input.configure_accessibility(target, {.role = accessibility::role::button, .name = "Apply"});
    begin();
    accessibility::channel channel;
    channel.publish(input.accessibility_nodes());
    const auto id = channel.read()->nodes().front().id;
    ASSERT_TRUE(channel.request({id, accessibility::action::focus}));
    accessibility_frame frame{channel};
    frame.route(input);
    frame.capture(input);
    frame.publish();
    EXPECT_EQ(frame.focus_owner(), target.session);
    EXPECT_TRUE(channel.read()->nodes().front().focused);
    EXPECT_EQ(count(event_kind::activate), 0);
    EXPECT_TRUE(channel.take_requests().empty());
}

/// @brief Independent windows and hidden/retired trees cannot redirect requests by slot index.
TEST_F(input_fixture, AccessibilityFrameRejectsRemovedSourceRequests)
{
    const auto target = tree.create();
    input.bind(target, handler, {.focusable = true});
    input.configure_accessibility(target, {.role = accessibility::role::button, .name = "Old"});
    begin();
    accessibility::channel channel;
    channel.publish(input.accessibility_nodes());
    ASSERT_TRUE(
        channel.request({channel.read()->nodes().front().id, accessibility::action::invoke}));
    tree.remove(target);
    accessibility_frame frame{channel};
    frame.route(input);
    frame.capture(input);
    frame.publish();
    EXPECT_EQ(count(event_kind::activate), 0);
    EXPECT_TRUE(channel.read()->nodes().empty());
}
} // namespace atlas::ui::tests
