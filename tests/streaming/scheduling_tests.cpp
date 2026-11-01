/** @file
 * @brief Runtime grain selection and cooperative read cancellation.
 */
#include <atlas/streaming/asset_streamer.hpp>
#include <gtest/gtest.h>
#include <numeric>

namespace atlas::streaming::tests
{
/// @brief Expected worker/size-dependent execution path for an otherwise identical read.
struct copy_case
{
    /// @brief Number of persistent CPU workers.
    std::size_t workers;
    /// @brief Requested byte count.
    std::size_t bytes;
    /// @brief Whether disjoint child jobs should be used.
    bool parallel;
};

/// @brief Print deterministic parameter descriptions instead of uninitialized struct padding.
std::ostream& operator<<(std::ostream& output, const copy_case& scenario)
{
    return output << "workers=" << scenario.workers << ",bytes=" << scenario.bytes
                  << ",parallel=" << scenario.parallel;
}

/// @brief Parameterized exact-copy contract across serial and parallel paths.
class copy_scheduling_tests : public testing::TestWithParam<copy_case>
{
};

/// @brief Small copies and single-worker pools avoid child dispatch overhead.
TEST_P(copy_scheduling_tests, chooses_grain_without_changing_result)
{
    const auto scenario = GetParam();
    jobs::job_system jobs{scenario.workers};
    asset_bytes source(scenario.bytes + 17);
    std::iota(source.begin(), source.end(), std::uint8_t{0});
    auto input = std::make_shared<const asset_bytes>(std::move(source));
    const asset_bytes expected(input->begin() + 17, input->end());
    streamer_limits limits;
    limits.parallel_copy_threshold = 128;
    limits.copy_grain_bytes = 64;
    asset_streamer streamer{jobs, {{"data", memory_asset{input}}}, limits};
    streamer
        .read({"data", 17, scenario.bytes},
              [&](const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
                  EXPECT_EQ(*std::get<std::shared_ptr<const asset_bytes>>(result), expected);
              })
        .get();
    streamer.shutdown();
    EXPECT_EQ(streamer.statistics().parallel_copies, scenario.parallel ? 1 : 0);
    EXPECT_EQ(streamer.statistics().serial_copies, scenario.parallel ? 0 : 1);
}

INSTANTIATE_TEST_SUITE_P(grain_paths, copy_scheduling_tests,
                         testing::Values(copy_case{1, 256, false}, copy_case{2, 32, false},
                                         copy_case{2, 256, true}, copy_case{2, 259, true}));

/// @brief Default serial copies avoid assuming a universal memory-bandwidth crossover point.
TEST(streaming_scheduling_tests, default_policy_avoids_uncalibrated_child_copy_overhead)
{
    jobs::job_system jobs{2};
    auto input = std::make_shared<const asset_bytes>(std::size_t{1024} * 1024, std::uint8_t{9});
    asset_streamer streamer{jobs, {{"data", memory_asset{input}}}};
    streamer
        .read({"data", 0, input->size()},
              [](const read_result& result)
              {
                  EXPECT_TRUE(std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
              })
        .get();
    EXPECT_EQ(streamer.statistics().parallel_copies, 0);
    EXPECT_EQ(streamer.statistics().serial_copies, 1);
}

/// @brief Already-cancelled reads deliver an error rather than skipping required cleanup.
TEST(streaming_scheduling_tests, pre_cancelled_reads_deliver_once_without_copying)
{
    jobs::job_system jobs{1};
    std::stop_source cancellation;
    cancellation.request_stop();
    auto input = std::make_shared<const asset_bytes>(asset_bytes{1, 2});
    asset_streamer streamer{jobs, {{"data", memory_asset{input}}}};
    streamer
        .read({"data", 0, 2},
              [](const read_result& result)
              {
                  ASSERT_TRUE(std::holds_alternative<std::exception_ptr>(result));
                  EXPECT_THROW(std::rethrow_exception(std::get<std::exception_ptr>(result)),
                               jobs::job_cancelled);
              },
              {jobs::job_priority::background, cancellation.get_token()})
        .get();
    EXPECT_EQ(streamer.statistics().completed, 1);
    EXPECT_EQ(streamer.statistics().failed, 1);
    EXPECT_EQ(streamer.statistics().resident_bytes, 0);
    EXPECT_EQ(streamer.statistics().serial_copies, 0);
}

/// @brief The current dispatch API records inherited priority and real parent/child ownership.
TEST(streaming_scheduling_tests, copy_children_inherit_priority_and_declare_disjoint_access)
{
    jobs::job_system_options options;
    options.worker_count = 2;
    options.trace_capacity = 8;
    jobs::job_system jobs{options};
    auto input = std::make_shared<const asset_bytes>(256, std::uint8_t{9});
    streamer_limits limits;
    limits.parallel_copy_threshold = 128;
    limits.copy_grain_bytes = 64;
    asset_streamer streamer{jobs, {{"data", memory_asset{input}}}, limits};
    streamer
        .read({"data", 0, 256},
              [](const read_result& result)
              {
                  EXPECT_TRUE(std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
              },
              {jobs::job_priority::high})
        .get();
    streamer.shutdown();
    jobs.wait_idle();
    const auto trace = jobs.trace_report();
    ASSERT_EQ(trace.tasks.size(), 3);
    for (const auto& task : trace.tasks)
    {
        EXPECT_EQ(task.priority, jobs::job_priority::high);
        EXPECT_EQ(task.status, jobs::job_status::succeeded);
        if (task.name == "stream.copy")
        {
            EXPECT_NE(task.parent_id, 0);
        }
    }

    EXPECT_TRUE(jobs.sanitizer_report().diagnostics.empty());
}

/// @brief A controlled busy worker verifies the serial fallback without relying on sleeps.
class busy_copy_tests : public testing::Test
{
  protected:
    /// @brief Release the controlled worker even after an assertion aborts the scenario.
    void TearDown() override
    {
        release.set_value();
        system.shutdown();
    }

    /// @brief Pool shared by the controlled load and the streamer.
    jobs::job_system system{2};
    /// @brief Worker release signal, opened by per-test teardown.
    std::promise<void> release;
};

/// @brief An occupied pool does not receive avoidable extra memory-copy children.
TEST_F(busy_copy_tests, busy_pool_uses_one_copy)
{
    std::promise<void> entered;
    auto started = entered.get_future();
    auto gate = release.get_future().share();
    auto blocker = system.submit(
        [&entered, gate]
        {
            entered.set_value();
            gate.wait();
        });
    ASSERT_EQ(started.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    auto input = std::make_shared<const asset_bytes>(256, std::uint8_t{9});
    streamer_limits limits;
    limits.parallel_copy_threshold = 128;
    limits.copy_grain_bytes = 64;
    asset_streamer streamer{system, {{"data", memory_asset{input}}}, limits};
    auto ready = streamer.read(
        {"data", 0, 256},
        [](const read_result& result)
        {
            EXPECT_TRUE(std::holds_alternative<std::shared_ptr<const asset_bytes>>(result));
        });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    EXPECT_EQ(streamer.statistics().serial_copies, 1);
    EXPECT_EQ(streamer.statistics().parallel_copies, 0);
}
} // namespace atlas::streaming::tests
