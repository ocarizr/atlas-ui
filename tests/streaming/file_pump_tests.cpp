/** @file @brief Bounded I/O occupancy, queued cancellation and CPU-worker independence. */
#include "controlled_file_io.hpp"

namespace atlas::streaming::tests
{
/// @brief The third read stays queued while two pending I/O operations leave the CPU worker free.
TEST_F(file_pump_test, capacity_limits_real_submissions_and_leaves_cpu_worker_available)
{
    auto first = enqueue();
    auto second = enqueue();
    auto third = enqueue();
    ASSERT_TRUE(files->await_starts(2));
    EXPECT_EQ(second.wait_for(std::chrono::seconds{0}), std::future_status::timeout);
    EXPECT_EQ(third.wait_for(std::chrono::seconds{0}), std::future_status::timeout);
    {
        const std::lock_guard lock{files->mutex};
        EXPECT_EQ(files->starts, 2);
        EXPECT_EQ(files->pending.size(), 2);
    }
    auto cpu_ready = std::make_shared<std::promise<void>>();
    auto cpu_future = cpu_ready->get_future();
    jobs.dispatcher().run(jobs::job_label{"cpu.ready"}, cpu_ready,
                          [](jobs::job_context&, auto& promise)
                          {
                              promise->set_value();
                          });
    EXPECT_EQ(cpu_future.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    files->release();
    ASSERT_TRUE(files->await_starts(3));
    EXPECT_EQ(first.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    EXPECT_NO_THROW(first.get());
}

/// @brief A queued stopped request retires without waiting for or opening an occupied native slot.
TEST_F(file_pump_test, queued_cancellation_retires_promptly_without_storage_admission)
{
    auto first = enqueue();
    auto second = enqueue();
    ASSERT_TRUE(files->await_starts(2));
    std::stop_source stop;
    auto cancelled = enqueue(stop.get_token());
    stop.request_stop();
    ASSERT_EQ(cancelled.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    EXPECT_NO_THROW(cancelled.get());
    {
        const std::lock_guard lock{files->mutex};
        EXPECT_EQ(files->starts, 2);
        EXPECT_EQ(files->pending.size(), 2);
    }
    EXPECT_EQ(state->io_in_flight.load(), 2);
    EXPECT_EQ(first.wait_for(std::chrono::seconds{0}), std::future_status::timeout);
    EXPECT_EQ(second.wait_for(std::chrono::seconds{0}), std::future_status::timeout);
}
} // namespace atlas::streaming::tests
