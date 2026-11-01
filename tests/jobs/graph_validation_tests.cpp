/**
 * @file
 * @brief Behavioral coverage for graph validation with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>

namespace atlas::jobs::tests
{
/// @brief Scenario state for cyclic graph tests, isolated per test and released through RAII.
class cyclic_graph_tests : public testing::TestWithParam<std::size_t>
{
};

/// @brief Verify rejects cycles before any callback is published.
TEST_P(cyclic_graph_tests, rejects_cycles_before_any_callback_is_published)
{
    job_system scheduler{2};
    job_graph graph;
    std::atomic<int> executed{0};
    graph.add("independent root",
              [&]
              {
                  ++executed;
              });
    std::vector<job_id> cycle;
    cycle.reserve(GetParam());
    for (std::size_t index = 0; index < GetParam(); ++index)
    {
        cycle.push_back(graph.add("cycle node",
                                  [&]
                                  {
                                      ++executed;
                                  }));
    }

    for (std::size_t index = 0; index < cycle.size(); ++index)
    {
        graph.precedes(cycle[index], cycle[(index + 1) % cycle.size()]);
    }

    try
    {
        scheduler.submit_graph(graph);
        FAIL() << "Expected cycle rejection";
    }
    catch (const std::invalid_argument& error)
    {
        EXPECT_NE(std::string{error.what()}.find("cycle"), std::string::npos);
    }

    scheduler.wait_idle();
    EXPECT_EQ(executed.load(), 0);
    EXPECT_EQ(scheduler.statistics().submitted, 0U);
}

INSTANTIATE_TEST_SUITE_P(cycle_lengths, cyclic_graph_tests, testing::Values(1U, 2U, 7U));

/// @brief Verify callback copy failure cannot publish a partial graph.
TEST_F(job_system_fixture, callback_copy_failure_cannot_publish_a_partial_graph)
{
    /// @brief Scenario state for throwing callback, isolated per test and released through RAII.
    struct throwing_callback
    {
        /// @brief Raise the scenario's expected callback failure without losing its worker.
        explicit throwing_callback(std::shared_ptr<bool> control) : fail{std::move(control)}
        {
        }

        /// @brief Raise the scenario's expected callback failure without losing its worker.
        throwing_callback(const throwing_callback& other) : fail{other.fail}
        {
            if (*fail)
            {
                throw std::bad_alloc{};
            }
        }

        /// @brief Execute this scenario/workload callable with its explicitly retained input.
        void operator()() const
        {
        }

        /// @brief Scenario-owned fail used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        std::shared_ptr<bool> fail;
    };

    job_graph graph;
    std::atomic<int> executed{0};
    graph.add("first",
              [&]
              {
                  ++executed;
              });
    auto fail = std::make_shared<bool>(false);
    graph.add("copy throws", throwing_callback{fail});
    *fail = true;
    EXPECT_THROW(system.submit_graph(graph), std::bad_alloc);
    EXPECT_EQ(system.statistics().submitted, 0U);
    EXPECT_EQ(executed.load(), 0);
    *fail = false;
    system.wait(system.submit_graph(graph));
    EXPECT_EQ(executed.load(), 1);
}
} // namespace atlas::jobs::tests
