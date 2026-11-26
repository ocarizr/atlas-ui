/** @file @brief Stale identity, callback failure and borrowing safety. */
#include "drag_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Recycled target invalidates pending transfer.
TEST_F(drag_fixture, RecycledTargetInvalidatesPendingTransfer)
{
    begin();
    pointer_drop();
    const auto request = pending();
    other.remove(target);
    const auto replacement = other.create();
    EXPECT_NE(replacement, target);
    EXPECT_FALSE(drag.can_commit(request.transfer));
    drag.cancel();
    EXPECT_EQ(count(*target_handler, event_kind::drag_completed), 0);
    finish();
}

/// @brief Failing query leaves no capture or unsealed demands.
TEST_F(drag_fixture, FailingQueryLeavesNoCaptureOrUnsealedDemands)
{
    target_handler->action = [](const routed_event& event, command_buffer&)
    {
        if (event.kind == event_kind::drag_query)
        {
            throw std::runtime_error("query failed");
        }

        return event_response{};
    };
    begin();
    EXPECT_THROW((void)drag.lift(input, source), std::runtime_error);
    EXPECT_EQ(drag.feedback().phase, drag_phase::idle);
    EXPECT_EQ(input.captured(), element_handle{});
    EXPECT_FALSE(drag.completion().committed);
    EXPECT_TRUE(destination.statistics().failed);
    drag.end_frame();
}

/// @brief Completion failure does not reclassify committed data as cancelled.
TEST_F(drag_fixture, CompletionFailureDoesNotReclassifyCommittedDataAsCancelled)
{
    source_handler->action = [](const routed_event& event, command_buffer&)
    {
        if (event.kind == event_kind::drag_completed)
        {
            throw std::runtime_error("completion failed");
        }

        return event_response{};
    };
    begin();
    pointer_drop();
    const auto request = pending();
    EXPECT_THROW(drag.resolve(request.transfer, true), std::runtime_error);
    EXPECT_TRUE(drag.completion().committed);
    EXPECT_EQ(drag.feedback().phase, drag_phase::idle);
    EXPECT_EQ(count(*source_handler, event_kind::drag_cancelled), 0);
    drag.end_frame();
}

/// @brief Input cannot close before domain releases its frame.
TEST_F(drag_fixture, InputCannotCloseBeforeDomainReleasesItsFrame)
{
    begin();
    EXPECT_THROW(input.tick(), std::logic_error);
    finish();
}

/// @brief Explicit move never silently becomes copy.
TEST_F(drag_fixture, ExplicitMoveNeverSilentlyBecomesCopy)
{
    destination.configure_drop_target(target, {{"test.item"}, drag_operations::copy});
    begin();
    ASSERT_TRUE(drag.lift(input, source, drag_operation::move));
    EXPECT_EQ(drag.feedback().operation, drag_operation::none);
    packet({input_kind::key_down, {}, std::uint32_t(input::key::enter)});
    EXPECT_FALSE(drag.pending_drop());
    EXPECT_FALSE(drag.completion().committed);
    finish();
}

/// @brief A failed destination completion cannot suppress a healthy source's terminal notification.
TEST_F(drag_fixture, CompletionAttemptsBothParticipantsAfterCommit)
{
    target_handler->action = [](const routed_event& event, command_buffer&)
    {
        if (event.kind == event_kind::drag_completed)
        {
            throw std::runtime_error("target completion failed");
        }

        return event_response{};
    };
    begin();
    pointer_drop();
    const auto request = pending();
    EXPECT_THROW(drag.resolve(request.transfer, true), std::runtime_error);
    EXPECT_TRUE(drag.completion().committed);
    EXPECT_EQ(count(*source_handler, event_kind::drag_completed), 1);
    EXPECT_EQ(count(*source_handler, event_kind::drag_cancelled), 0);
    EXPECT_EQ(input.captured(), element_handle{});
    drag.end_frame();
}

} // namespace atlas::ui::tests
