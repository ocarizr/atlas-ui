/** @file @brief Failures and cancellation drain attached work before private outputs are released.
 */
#include <array>
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/frame_executor.hpp>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <semaphore>
#include <thread>

namespace atlas::ui::tests
{
/// @brief Observe each written output's cleanup without mutable test-global state.
struct tracked_output
{
    std::atomic<int>* retired = nullptr; ///< Per-case counter, valid through the joined call.

    ~tracked_output()
    {
        if (retired)
        {
            retired->fetch_add(1);
        }
    } ///< Must run only after the worker using this object has returned.
};

/// @brief Isolated, timeout-protected synchronization shared by two explicit input records.
struct failure_control
{
    std::binary_semaphore entered{0}; ///< Second child has written its output.
    std::binary_semaphore failed{0};  ///< First child is about to throw its original error.
    std::binary_semaphore release{0}; ///< Controller permits the surviving child to finish.
    std::atomic<int> retired{0};      ///< No output may retire while the surviving child is held.
};

/// @brief No arbitrary sleep; a broken join test reports a bounded diagnostic failure.
void wait_signal(std::binary_semaphore& signal)
{
    if (!signal.try_acquire_for(std::chrono::seconds{5}))
    {
        throw std::runtime_error("controlled phase signal timed out");
    }
}

/// @brief Failure from one child cannot release output storage still borrowed by another.
TEST(frame_failures, failed_child_joins_surviving_child_before_output_cleanup)
{
    jobs::job_system pool{2};
    frame_limits limits;
    limits.min_parallel_items = 1;
    limits.grain = 1;
    limits.max_chunks = 2;
    frame_executor executor{pool.dispatcher(), limits};
    failure_control control;
    std::array input{&control, &control};
    std::jthread controller{[&]
                            {
                                const bool signalled =
                                    control.failed.try_acquire_for(std::chrono::seconds{5});
                                EXPECT_TRUE(signalled);
                                EXPECT_EQ(control.retired.load(), 0);
                                control.release.release();
                            }};
    executor.begin_frame(1);
    EXPECT_THROW(executor.run_phase<tracked_output>(
                     "failing", std::span<failure_control* const>{input},
                     [](const phase_range<failure_control*, tracked_output>& range)
                     {
                         auto& state = *range.inputs[0];
                         range.outputs[0].retired = &state.retired;
                         if (range.first == 0)
                         {
                             wait_signal(state.entered);
                             state.failed.release();
                             throw std::domain_error("original child failure");
                         }

                         state.entered.release();
                         wait_signal(state.release);
                     }),
                 std::domain_error);
    controller.join();
    EXPECT_EQ(control.retired.load(), 2);
    EXPECT_TRUE(executor.statistics().failed);
    EXPECT_EQ(executor.statistics().admitted_jobs, 3U);
    EXPECT_THROW(executor.dispatch("publish",
                                   []
                                   {
                                       ADD_FAILURE();
                                   }),
                 std::logic_error);
}

/// @brief Exercise the same cancellation contract with owner execution and bounded worker ranges.
class phase_cancellation_test : public ::testing::TestWithParam<bool>
{
};

/// @brief A requested stop publishes no result and forbids later phases in the failed frame.
TEST_P(phase_cancellation_test, kernel_stop_joins_and_latches_the_frame)
{
    jobs::job_system pool{2};
    frame_limits limits;
    limits.min_parallel_items = GetParam() ? 1 : 100;
    limits.grain = 1;
    frame_executor executor{pool.dispatcher(), limits};
    std::stop_source stop;
    std::array input{&stop, &stop};
    executor.begin_frame(1, {jobs::job_priority::normal, stop.get_token()});
    EXPECT_THROW(executor.run_phase<int>("cancel", std::span<std::stop_source* const>{input},
                                         [](const phase_range<std::stop_source*, int>& range)
                                         {
                                             range.inputs[0]->request_stop();
                                             range.outputs[0] = 1;
                                         }),
                 jobs::job_cancelled);
    EXPECT_TRUE(executor.statistics().failed);
    EXPECT_THROW(executor.end_frame(), std::logic_error);
    executor.begin_frame(2);
    executor.end_frame();
}

INSTANTIATE_TEST_SUITE_P(paths, phase_cancellation_test, ::testing::Bool());

/// @brief Stop-before-entry suppresses serial event producers as well as work kernels.
TEST(frame_failures, prior_cancellation_runs_no_serial_callback)
{
    jobs::job_system pool{1};
    frame_executor executor{pool.dispatcher()};
    std::stop_source stop;
    stop.request_stop();
    executor.begin_frame(1, {jobs::job_priority::normal, stop.get_token()});
    EXPECT_THROW(executor.dispatch("tick",
                                   []
                                   {
                                       ADD_FAILURE();
                                   }),
                 jobs::job_cancelled);
    EXPECT_EQ(executor.statistics().admitted_jobs, 0U);
}

/// @brief A rejected root leaves no accepted worker borrowing output storage.
TEST(frame_failures, stopped_pool_rejects_parallel_admission_safely)
{
    jobs::job_system pool{1};
    frame_limits limits;
    limits.min_parallel_items = 1;
    limits.grain = 1;
    frame_executor executor{pool.dispatcher(), limits};
    pool.shutdown();
    executor.begin_frame(1);
    std::array input{1, 2};
    EXPECT_THROW(executor.run_phase<int>("stopped", std::span<const int>{input},
                                         [](const phase_range<int, int>&)
                                         {
                                             ADD_FAILURE();
                                         }),
                 std::logic_error);
    EXPECT_TRUE(executor.statistics().failed);
    EXPECT_EQ(executor.statistics().admitted_jobs, 0U);
}

/// @brief A consumer-thrown job_error is payload failure, not the adapter's transport wrapper.
TEST_P(phase_cancellation_test, explicit_consumer_job_error_preserves_its_identity)
{
    jobs::job_system pool{2};
    frame_limits limits;
    limits.min_parallel_items = GetParam() ? 1 : 100;
    limits.grain = 1;
    frame_executor executor{pool.dispatcher(), limits};
    const std::array input{1, 2};
    executor.begin_frame(1);
    try
    {
        (void)executor.run_phase<int>("original", std::span<const int>{input},
                                      [](const phase_range<int, int>&)
                                      {
                                          throw jobs::job_error{
                                              "consumer",
                                              std::make_exception_ptr(std::domain_error{"cause"})};
                                      });
        FAIL() << "kernel failure was not reported";
    }
    catch (const jobs::job_error& error)
    {
        EXPECT_EQ(error.job_name(), "consumer");
    }
    catch (...)
    {
        ADD_FAILURE() << "the consumer's exception wrapper was replaced";
    }
}
} // namespace atlas::ui::tests
