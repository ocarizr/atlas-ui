/** @file @brief Controlled cancellation joins borrowed painter ranges before returning. */
#include "paint_test_support.hpp"
#include <chrono>
#include <semaphore>
#include <thread>

namespace atlas::ui::tests
{
/// @brief Closed dispatchers cannot publish partially prepared packets.
/// @brief Verify closed pool rejects jobs but serial fallback does not need an open pool.
TEST_F(paint_fixture, ClosedPoolRejectsJobsButSerialFallbackDoesNotNeedAnOpenPool)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        (void)rectangle();
    }

    (void)capture();
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    paint_engine parallel{workers.dispatcher(), {4096, 1, 4, paint_execution::chunks}};
    workers.shutdown();
    EXPECT_THROW(parallel.calculate(source, geometry), std::logic_error);
    EXPECT_TRUE(parallel.statistics().failed);
    EXPECT_EQ(parallel.statistics().admitted_jobs, 0);
    EXPECT_EQ(paint.calculate(source, geometry).quads().size(), 4);
}

/// @brief A queued root remains attached until cancellation drains its borrowed descriptors.
/// @brief Verify queued cancellation joins before releasing buffers and allows retry.
TEST_F(paint_fixture, QueuedCancellationJoinsBeforeReleasingBuffersAndAllowsRetry)
{
    for (unsigned i = 0; i < 16; ++i)
    {
        (void)rectangle();
    }

    (void)capture();
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    paint_engine parallel{workers.dispatcher(), {4096, 1, 4, paint_execution::chunks}};

    struct gate
    {
        std::counting_semaphore<2> entered{0}; ///< Controlled worker occupancy.
        std::counting_semaphore<2> release{0}; ///< Released by controller after cancellation.
    }; ///< Test-only gates have diagnostic deadlines, never arbitrary sleeps.

    auto state = std::make_shared<gate>();

    const auto occupy = [](jobs::job_context&, std::shared_ptr<gate>& value)
    {
        value->entered.release();
        if (!value->release.try_acquire_for(std::chrono::seconds{5}))
        {
            throw std::runtime_error("paint controlled gate timeout");
        }
    };
    const auto first = workers.dispatcher().run({"paint gate"}, state, occupy);
    const auto second = workers.dispatcher().run({"paint gate"}, state, occupy);
    ASSERT_TRUE(state->entered.try_acquire_for(std::chrono::seconds{5}));
    ASSERT_TRUE(state->entered.try_acquire_for(std::chrono::seconds{5}));
    std::stop_source stop;
    const auto submitted = workers.statistics().submitted;
    std::jthread controller{[&]
                            {
                                const auto deadline =
                                    std::chrono::steady_clock::now() + std::chrono::seconds{5};
                                while (workers.statistics().submitted == submitted &&
                                       std::chrono::steady_clock::now() < deadline)
                                {
                                    std::this_thread::yield();
                                }

                                EXPECT_GT(workers.statistics().submitted, submitted);
                                stop.request_stop();
                                state->release.release(2);
                            }};
    EXPECT_THROW(
        parallel.calculate(source, geometry, {jobs::job_priority::normal, stop.get_token()}),
        jobs::job_cancelled);
    controller.join();
    first.wait();
    second.wait();
    workers.wait_idle();
    EXPECT_TRUE(parallel.statistics().failed);
    EXPECT_EQ(parallel.statistics().admitted_jobs, 1);
    EXPECT_EQ(parallel.calculate(source, geometry).quads().size(), 16);
}
} // namespace atlas::ui::tests
