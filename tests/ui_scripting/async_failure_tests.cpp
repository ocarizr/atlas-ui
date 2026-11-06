/** @file @brief Retired behavior generations reject late jobs and cancel unissued native work. */
#include <atlas/scripting/session.hpp>
#include <atomic>
#include <barrier>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::ui_scripting::tests
{
/// @brief A small owner-held session used to exercise transport independent of UI geometry.
class script_transport : public ::testing::Test
{
  protected:
    /// @brief Per-test retained empty model; all state belongs to the fixture owner.
    std::shared_ptr<mvvm::view_model> model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    scripting::session script{model}; ///< Closes transport before releasing the model.

    /// @brief Load the request producer and admit matching owner frames for each case.
    void SetUp() override
    {
        script.load("return {tick=function() model.request('compute',7) end}");
        model->begin_frame(1);
        script.begin_frame(1);
    }
};

/// @brief Public metadata cannot retarget the private completion capability.
TEST_F(script_transport, MetadataChangesDoNotForgeCompletionIdentity)
{
    script.call("tick");
    auto work = script.take_requests();
    work.front().id = 999;
    EXPECT_TRUE(work.front().complete());
    script.deliver_results();
    EXPECT_EQ(script.inspect().pending_work, 0);
}

/// @brief Rejected payloads leave tickets available for a later bounded error/status response.
TEST(script_transport_limits, CompletionPayloadFailureDoesNotConsumeTicket)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    scripting::session_limits limits;
    limits.max_pending_bytes = 64;
    scripting::session script{model, limits};
    script.load("return {tick=function() model.request('compute') end}");
    model->begin_frame(1);
    script.begin_frame(1);
    script.call("tick");
    auto work = script.take_requests();
    const std::vector<mvvm::value> too_large(30, std::int64_t{7});
    EXPECT_THROW(work.front().complete(too_large), std::overflow_error);
    EXPECT_TRUE(work.front().complete({}, false));
    script.deliver_results();
    EXPECT_EQ(script.inspect().pending_work, 0);
}

/// @brief Competing workers cannot accept the same ticket more than once.
TEST_F(script_transport, ConcurrentDuplicateHasExactlyOneAcceptance)
{
    script.call("tick");
    auto work = script.take_requests();
    std::atomic<int> accepted = 0;
    std::barrier gate{3};
    auto complete = [&]
    {
        gate.arrive_and_wait();
        if (work.front().complete())
        {
            ++accepted;
        }
    };
    std::jthread first{complete}, second{complete};
    gate.arrive_and_wait();
    first.join();
    second.join();
    EXPECT_EQ(accepted.load(), 1);
}

/// @brief Stop callbacks may synchronously attempt a late completion without a lock inversion.
TEST_F(script_transport, CancellationCallbacksCannotDeadlockCompletion)
{
    script.call("tick");
    auto work = script.take_requests();
    bool called = false;
    std::stop_callback callback{work.front().cancellation, [&]
                                {
                                    called = true;
                                    EXPECT_FALSE(work.front().complete());
                                }};
    script.shutdown();
    EXPECT_TRUE(called);
}

/// @brief Results can be accepted exactly once, including failed native work.
TEST_F(script_transport, RejectsDuplicateCompletionAndDrainsFailureStatus)
{
    script.call("tick");
    auto work = script.take_requests();
    ASSERT_EQ(work.size(), 1);
    EXPECT_TRUE(work.front().complete({}, false));
    EXPECT_FALSE(work.front().complete());
    script.deliver_results();
    EXPECT_EQ(script.inspect().pending_work, 0);
}

/// @brief A reload replaces the mailbox identity even though numeric request IDs may restart.
TEST_F(script_transport, ReloadCancelsAndRejectsStaleGeneration)
{
    script.call("tick");
    auto work = script.take_requests();
    script.load("return {}");
    EXPECT_TRUE(work.front().cancellation.stop_requested());
    EXPECT_FALSE(work.front().complete());
    EXPECT_EQ(script.inspect().pending_work, 0);
}

/// @brief Destruction does not wait on native jobs and their tokens cannot keep the VM alive.
TEST(script_transport_lifetime, LateWorkerDoesNotRetainDestroyedVm)
{
    scripting::work_request pending;
    {
        auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
        scripting::session script{model};
        script.load("return {tick=function() model.request('compute') end}");
        model->begin_frame(1);
        script.begin_frame(1);
        script.call("tick");
        pending = std::move(script.take_requests().front());
    }
    std::jthread worker{[&]
                        {
                            EXPECT_FALSE(pending.complete());
                        }};
    worker.join();
    EXPECT_TRUE(pending.cancellation.stop_requested());
}

/// @brief The queue bound applies across calls, including work already transferred to workers.
TEST(script_transport_limits, PendingQuotaDoesNotLeakCommands)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    scripting::session_limits limits;
    limits.max_pending_work = 1;
    scripting::session script{model, limits};
    script.load("return {tick=function() model.request('compute') end}");
    model->begin_frame(1);
    script.begin_frame(1);
    script.call("tick");
    auto work = script.take_requests();
    EXPECT_THROW(script.call("tick"), std::overflow_error);
    EXPECT_TRUE(work.front().cancellation.stop_requested());
    EXPECT_FALSE(work.front().complete());
}
} // namespace atlas::ui_scripting::tests
