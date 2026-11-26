/** @file @brief Confirmed cross-panel transfer and cancellation invariants. */
#include "drag_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Requests copy across panels without mutating application data.
TEST_F(drag_fixture, RequestsCopyAcrossPanelsWithoutMutatingApplicationData)
{
    begin();
    pointer_drop();
    const auto request = pending();
    EXPECT_EQ(request.source, source);
    EXPECT_EQ(request.target, target);
    EXPECT_EQ(request.payload.text(), "item-42");
    EXPECT_EQ(request.operation, drag_operation::copy);
    EXPECT_EQ(count(*source_handler, event_kind::drag_completed), 0);
    EXPECT_TRUE(drag.can_commit(request.transfer));
    drag.resolve(request.transfer, true);
    EXPECT_TRUE(drag.completion().committed);
    EXPECT_EQ(input.captured(), element_handle{});
    EXPECT_EQ(count(*source_handler, event_kind::drag_completed), 1);
    EXPECT_EQ(count(*target_handler, event_kind::drag_completed), 1);
    EXPECT_EQ(count(*source_handler, event_kind::pointer_up), 0);
    finish();
}

/// @brief Rejecting or cancelling never reports move completion.
TEST_F(drag_fixture, RejectingOrCancellingNeverReportsMoveCompletion)
{
    begin();
    ASSERT_TRUE(drag.lift(input, source, drag_operation::move));
    EXPECT_EQ(drag.feedback().target, target);
    packet({input_kind::key_down, {}, std::uint32_t(input::key::enter)});
    const auto request = pending();
    EXPECT_EQ(request.operation, drag_operation::move);
    drag.resolve(request.transfer, false);
    EXPECT_FALSE(drag.completion().committed);
    EXPECT_EQ(drag.completion().reason, drag_cancel_reason::rejected);
    EXPECT_EQ(count(*source_handler, event_kind::drag_completed), 0);
    EXPECT_EQ(count(*source_handler, event_kind::drag_cancelled), 1);
    EXPECT_THROW(drag.resolve(request.transfer, true), std::invalid_argument);
    finish();
}

/// @brief Threshold preserves ordinary clicks without preparing payload.
TEST_F(drag_fixture, ThresholdPreservesOrdinaryClicksWithoutPreparingPayload)
{
    begin();
    EXPECT_FALSE(packet({input_kind::pointer_down, {10, 10}}));
    EXPECT_FALSE(packet({input_kind::pointer_move, {12, 10}}));
    EXPECT_FALSE(packet({input_kind::pointer_up, {12, 10}}));
    EXPECT_EQ(count(*source_handler, event_kind::drag_prepare), 0);
    EXPECT_EQ(count(*source_handler, event_kind::pointer_up), 1);
    EXPECT_EQ(drag.feedback().phase, drag_phase::idle);
    finish();
}

/// @brief Confirmed move may remove source before completion without false cancellation.
TEST_F(drag_fixture, ConfirmedMoveMayRemoveSourceBeforeCompletionWithoutFalseCancellation)
{
    begin();
    ASSERT_TRUE(drag.lift(input, source, drag_operation::move));
    packet({input_kind::key_down, {}, std::uint32_t(input::key::enter)});
    const auto request = pending();
    ASSERT_TRUE(drag.can_commit(request.transfer));
    tree.remove(source); // Simulate the already successful atomic application transaction.
    drag.resolve(request.transfer, true);
    EXPECT_TRUE(drag.completion().committed);
    EXPECT_FALSE(source_handler->events.back().target_alive);
    finish();
}

/// @brief Window loss cancels and releases capture across frames.
TEST_F(drag_fixture, WindowLossCancelsAndReleasesCaptureAcrossFrames)
{
    begin();
    packet({input_kind::pointer_down, {10, 10}});
    packet({input_kind::pointer_move, {150, 20}});
    finish();
    begin(2);
    EXPECT_EQ(drag.feedback().phase, drag_phase::dragging);
    EXPECT_FALSE(packet({input_kind::window_blur}));
    EXPECT_EQ(drag.completion().reason, drag_cancel_reason::window_lost);
    EXPECT_EQ(input.captured(), element_handle{});
    finish();
}
} // namespace atlas::ui::tests
