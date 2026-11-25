/** @file @brief Reconfigured ownership, nested clipping and bounded drag work. */
#include "drag_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief An old offer must not remain active after its source policy is removed.
TEST_F(drag_fixture, SourcePolicyRemovalCancelsAnActiveGesture)
{
    begin();
    ASSERT_TRUE(drag.lift(input, source));
    finish();
    input.reset_drag_source(source);
    begin(2);
    EXPECT_EQ(drag.feedback().phase, drag_phase::idle);
    EXPECT_EQ(drag.completion().reason, drag_cancel_reason::source_unavailable);
    finish();
}

/// @brief Reverse traversal wraps to the final eligible destination.
TEST_F(drag_fixture, ReverseTargetTraversalWrapsFromFirstToLast)
{
    const auto last = other.create(target);
    destination.bind(last, std::make_shared<recording_handler>());
    destination.configure_drop_target(last, {{"test.item"}});
    begin();
    ASSERT_TRUE(drag.lift(input, source));
    EXPECT_EQ(drag.feedback().target, target);
    drag.next_target(true);
    EXPECT_EQ(drag.feedback().target, last);
    finish();
}

/// @brief A policy rejection falls back to its accepting ancestor, never an occluded sibling.
TEST_F(drag_fixture, RejectedNestedTargetFallsBackToItsAncestor)
{
    const auto nested = other.create(target);
    auto handler = std::make_shared<recording_handler>();
    handler->action = [](const routed_event& event, command_buffer&)
    {
        return event.kind == event_kind::drag_query
                   ? event_response{.acceptance = drag_acceptance::reject}
                   : event_response{};
    };
    destination.bind(nested, handler);
    destination.configure_drop_target(nested, {{"test.item"}});
    begin();
    pointer_drop();
    const auto request = pending();
    EXPECT_EQ(request.target, target);
    drag.resolve(request.transfer, false);
    finish();
}

/// @brief Captured input callbacks cannot reenter the input session during gesture cleanup.
TEST_F(drag_fixture, CaptureCleanupRejectsReentrantInputCalls)
{
    bool rejected = false;
    source_handler->action = [&](const routed_event& event, command_buffer&)
    {
        if (event.kind == event_kind::pointer_down && event.phase == route_phase::target)
        {
            return event_response{.capture_pointer = true};
        }

        if (event.kind == event_kind::capture_lost)
        {
            try
            {
                (void)input.captured();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        }

        return event_response{};
    };
    begin();
    packet({input_kind::pointer_down, {10, 10}});
    packet({input_kind::pointer_move, {150, 20}});
    EXPECT_TRUE(rejected);
    drag.cancel();
    finish();
}

/// @brief A semantic action fires on performed, not on both started and performed packets.
TEST_F(drag_fixture, SemanticLiftDoesNotFireOnStarted)
{
    drag.configure({.lift = 42}, false);
    begin();
    input.focus(source);
    input_event event{input_kind::action};
    event.action =
        input::action_event{.action = 42, .phase = input::action_phase::started, .frame = 1};
    EXPECT_FALSE(drag.process(event));
    EXPECT_EQ(drag.feedback().phase, drag_phase::idle);
    event.action->phase = input::action_phase::performed;
    EXPECT_TRUE(drag.process(event));
    EXPECT_EQ(drag.feedback().phase, drag_phase::dragging);
    drag.cancel();
    finish();
}
} // namespace atlas::ui::tests
