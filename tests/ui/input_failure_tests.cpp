/** @file @brief Replay preflight, frame failure and callback ownership guards. */
#include "input_test_support.hpp"
#include <cmath>

namespace atlas::ui::tests
{
/// @brief Invalid packets and stage order consume no input callbacks or partial events.
TEST_F(input_fixture, RejectedBatchCanBeCorrectedInTheSameFrame)
{
    input.bind(tree.create(), handler);
    begin();
    EXPECT_THROW(input.tick(), std::logic_error);
    const std::array invalid{
        input_event{input_kind::pointer_down, {1, 1}},
        input_event{input_kind::pointer_move, {std::numeric_limits<float>::quiet_NaN(), 0}}};
    EXPECT_THROW(input.process(invalid), std::invalid_argument);
    EXPECT_EQ(count(event_kind::process_input), 0);
    EXPECT_EQ(count(event_kind::pointer_down), 0);
    EXPECT_FALSE(input.statistics().failed);
    input.process({});
    finish();
    EXPECT_THROW(input.process({}), std::logic_error);
}

/// @brief Failure preserves the original exception, discards unsealed writes and latches the frame.
TEST_F(input_fixture, CallbackFailureDiscardsPendingWritesAndAllowsExplicitRecovery)
{
    const auto target = tree.create();
    input.bind(target, handler);
    handler->action = [target](const routed_event& event, command_buffer& commands)
    {
        if (event.kind == event_kind::tick)
        {
            commands.set(target, property::opacity, 0.25f, 1);
            throw std::runtime_error("intentional input failure");
        }

        return event_response{};
    };
    begin();
    input.process({});
    EXPECT_THROW(input.tick(), std::runtime_error);
    EXPECT_TRUE(input.statistics().failed);
    EXPECT_EQ(tree.commands().pending(), 0);
    EXPECT_EQ(tree.requested(target).opacity, 1);
    EXPECT_THROW(input.late_tick(), std::logic_error);
    handler->action = {};
    begin(2);
    input.process({});
    finish();
    EXPECT_EQ(count(event_kind::start), 1);
    EXPECT_FALSE(input.statistics().failed);
}

/// @brief Callback reentry and worker access are rejected before mutable-state observation.
TEST_F(input_fixture, CallbacksCannotReenterAndWorkersCannotDispatch)
{
    const auto target = tree.create();
    input.bind(target, handler);
    handler->action = [&](const routed_event&, command_buffer&)
    {
        EXPECT_THROW(input.focused(), std::logic_error);
        EXPECT_THROW(input.unbind(target), std::logic_error);
        return event_response{};
    };
    begin();
    input.process({});
    finish();
    auto work = workers.dispatcher().run({"input.misuse"}, &input,
                                         [](jobs::job_context&, input_session*& session)
                                         {
                                             EXPECT_THROW(session->tick(), std::logic_error);
                                         });
    work.wait();
    EXPECT_FALSE(input.statistics().failed);
}

/// @brief One throwing cleanup handler cannot prevent later handlers from attempting cleanup.
TEST_F(input_fixture, ShutdownAttemptsEveryLifecycleCleanupAndPreservesTheFirstFailure)
{
    input.bind(tree.create(), handler);
    input.bind(tree.create(), handler);
    handler->action = [](const routed_event& event, command_buffer&)
    {
        if (event.kind == event_kind::disable)
        {
            throw std::runtime_error("cleanup failure");
        }

        return event_response{};
    };
    begin();
    input.process({});
    finish();
    EXPECT_THROW(input.shutdown(2), std::runtime_error);
    EXPECT_EQ(count(event_kind::disable), 2);
    EXPECT_EQ(count(event_kind::end), 2);
    EXPECT_EQ(input.captured(), element_handle{});
    EXPECT_EQ(input.focused(), element_handle{});
}

/// @brief Finite viewport coordinates can still overflow a float local-coordinate difference.
TEST_F(input_fixture, CapturedCoordinateOverflowNeverReachesAHandler)
{
    const auto target = tree.create();
    input.bind(target, handler);
    handler->action = [](const routed_event& event, command_buffer&)
    {
        EXPECT_TRUE(std::isfinite(event.local.x));
        return event_response{.capture_pointer = event.kind == event_kind::pointer_down};
    };
    const auto maximum = std::numeric_limits<float>::max();
    tree.set_viewport({-maximum, 0, maximum, 80});
    const auto source = tree.snapshot();
    input.begin_frame(1, source, layout.calculate(source));
    const std::array packets{input_event{input_kind::pointer_down, {-maximum, 1}},
                             input_event{input_kind::pointer_move, {maximum, 1}}};
    EXPECT_THROW(input.process(packets), std::overflow_error);
    EXPECT_TRUE(input.statistics().failed);
    EXPECT_EQ(input.captured(), element_handle{});
}

/// @brief An aborted frame is quiescent so a faulty handler can be retired before recovery.
TEST_F(input_fixture, FailedFrameAllowsRetiringTheFaultyHandlerBeforeRecovery)
{
    const auto target = tree.create();
    input.bind(target, handler);
    handler->action = [](const routed_event& event, command_buffer&)
    {
        if (event.kind == event_kind::tick)
        {
            throw std::runtime_error("faulty handler");
        }

        return event_response{};
    };
    begin();
    input.process({});
    EXPECT_THROW(input.tick(), std::runtime_error);
    EXPECT_NO_THROW(input.unbind(target));
    handler->action = {};
    begin(2);
    input.process({});
    finish();
    EXPECT_EQ(count(event_kind::tick), 1);
    EXPECT_EQ(count(event_kind::end), 1);
}
} // namespace atlas::ui::tests
