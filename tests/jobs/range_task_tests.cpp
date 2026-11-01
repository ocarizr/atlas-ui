/**
 * @file
 * @brief Behavioral coverage for range task with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <numeric>
#include <vector>

namespace atlas::jobs::tests
{
/// @brief Scenario state for range case, isolated per test and released through RAII.
struct range_case
{
    /// @brief Scenario-owned size used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::size_t size;
    /// @brief Scenario-owned grain used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::size_t grain;
};

/// @brief Print a readable parameter-case description for GoogleTest failure diagnostics.
std::ostream& operator<<(std::ostream& output, const range_case& value)
{
    return output << "size=" << value.size << ", grain=" << value.grain;
}

/// @brief Scenario state for range task tests, isolated per test and released through RAII.
class range_task_tests : public testing::TestWithParam<range_case>
{
};

/// @brief Verify disjoint chunks visit each element once and use a small range path.
TEST_P(range_task_tests, disjoint_chunks_visit_each_element_once_and_use_a_small_range_path)
{
    job_system system{2};
    const auto parameters = GetParam();
    std::vector<int> values(parameters.size, 0);
    auto job = system.dispatcher().parallel_for("increment", std::span{values}, parameters.grain,
                                                [](job_context&, std::span<int> chunk)
                                                {
                                                    for (auto& value : chunk)
                                                    {
                                                        ++value;
                                                    }
                                                });
    job.wait();
    EXPECT_EQ(values, std::vector<int>(parameters.size, 1));
    const auto chunks =
        parameters.size <= parameters.grain ? 0U : 1 + (parameters.size - 1) / parameters.grain;
    EXPECT_EQ(system.statistics().completed, 1 + chunks);
}

INSTANTIATE_TEST_SUITE_P(boundaries, range_task_tests,
                         testing::Values(range_case{0, 8}, range_case{1, 8}, range_case{7, 8},
                                         range_case{8, 8}, range_case{9, 8}, range_case{257, 16},
                                         range_case{1000, 33}));

/// @brief Verify invalid grains are rejected before dispatch.
TEST_F(job_system_fixture, invalid_grains_are_rejected_before_dispatch)
{
    std::vector<int> values(8);
    EXPECT_THROW(system.dispatcher().parallel_for("invalid", std::span{values}, 0,
                                                  [](job_context&, std::span<int>)
                                                  {
                                                      FAIL();
                                                  }),
                 std::invalid_argument);
    EXPECT_EQ(system.statistics().submitted, 0U);
}

/// @brief Verify range failures drain sibling chunks and name the failed chunk.
TEST_F(job_system_fixture, range_failures_drain_sibling_chunks_and_name_the_failed_chunk)
{
    std::vector<int> values(64);
    std::iota(values.begin(), values.end(), 0);
    auto job = system.dispatcher().parallel_for("map", std::span{values}, 8,
                                                [](job_context&, std::span<int> chunk)
                                                {
                                                    if (chunk.front() == 0)
                                                    {
                                                        throw std::runtime_error{"chunk failure"};
                                                    }

                                                    for (auto& value : chunk)
                                                    {
                                                        ++value;
                                                    }
                                                });
    try
    {
        job.wait();
        FAIL();
    }
    catch (const job_error& error)
    {
        EXPECT_EQ(error.job_name(), "map [0]");
    }

    for (std::size_t index = 0; index < values.size(); ++index)
    {
        EXPECT_EQ(values[index], static_cast<int>(index + (index >= 8 ? 1 : 0)));
    }
}

/// @brief Verify child ranges keep the parent owned buffer alive until cleanup.
TEST_F(job_system_fixture, child_ranges_keep_the_parent_owned_buffer_alive_until_cleanup)
{
    /// @brief Scenario state for owned values, isolated per test and released through RAII.
    struct owned_values
    {
        /// @brief Scenario-owned values used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::vector<int> values;
        /// @brief Scenario-owned result used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<std::promise<std::vector<int>>> result;

        /// @brief Release owned values resources and complete its scoped cleanup.
        ~owned_values()
        {
            try
            {
                result->set_value(std::move(values));
            }
            catch (...)
            {
                std::terminate();
            }
        }
    };

    auto result = std::make_shared<std::promise<std::vector<int>>>();
    auto observed = result->get_future();
    auto data = std::make_unique<owned_values>();
    data->values.resize(257);
    data->result = result;
    system.dispatcher()
        .run("owner", std::move(data),
             [](job_context& context, auto& owned)
             {
                 context.parallel_for("children", std::span{owned->values}, 16,
                                      [](job_context&, std::span<int> chunk)
                                      {
                                          for (auto& value : chunk)
                                          {
                                              ++value;
                                          }
                                      });
             })
        .wait();
    EXPECT_EQ(observed.get(), std::vector<int>(257, 1));
}

/// @brief Verify null range functions are rejected even for empty ranges.
TEST_F(job_system_fixture, null_range_functions_are_rejected_even_for_empty_ranges)
{
    void (*function)(job_context&, std::span<int>&) = nullptr;
    EXPECT_THROW(system.dispatcher().parallel_for("null", std::span<int>{}, 8, function),
                 std::invalid_argument);
    system.wait_idle();
    EXPECT_EQ(system.statistics().submitted, 0U);
}
} // namespace atlas::jobs::tests
