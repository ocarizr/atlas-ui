/**
 * @file
 * @brief Behavioral coverage for graph failure with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <atomic>
#include <stdexcept>
#include <string>

namespace atlas::jobs::tests
{
/// @brief Verify failed prerequisites skip descendants but not independent branches.
TEST_F(job_system_fixture, failed_prerequisites_skip_descendants_but_not_independent_branches)
{
    job_graph graph;
    std::atomic<int> forbidden{0};
    std::atomic<int> independent{0};
    const auto failed = graph.add("bad input",
                                  []
                                  {
                                      throw std::runtime_error{"invalid bytes"};
                                  });
    const auto child = graph.add("child",
                                 [&]
                                 {
                                     ++forbidden;
                                 });
    const auto descendant = graph.add("descendant",
                                      [&]
                                      {
                                          ++forbidden;
                                      });
    const auto other = graph.add("independent",
                                 [&]
                                 {
                                     ++independent;
                                 });
    graph.precedes(failed, child);
    graph.precedes(child, descendant);
    const auto execution = system.submit_graph(graph);
    try
    {
        system.wait(execution);
        FAIL() << "A failed graph must report the named callback failure";
    }
    catch (const graph_error& error)
    {
        EXPECT_EQ(error.job_name(), "bad input");
        EXPECT_NE(std::string{error.what()}.find("bad input"), std::string::npos);
        EXPECT_THROW(std::rethrow_exception(error.cause()), std::runtime_error);
    }

    EXPECT_EQ(forbidden.load(), 0);
    EXPECT_EQ(independent.load(), 1);
    EXPECT_EQ(execution.status(failed), job_status::failed);
    EXPECT_EQ(execution.status(child), job_status::skipped);
    EXPECT_EQ(execution.status(descendant), job_status::skipped);
    EXPECT_EQ(execution.status(other), job_status::succeeded);
    EXPECT_TRUE(execution.exception(failed));
    EXPECT_FALSE(execution.exception(child));
    EXPECT_FALSE(execution.exception(other));
    EXPECT_NO_THROW(system.wait(system.submit(
        []
        {
        })));
}

/// @brief Verify aggregate failure uses insertion order and preserves all causes.
TEST_F(job_system_fixture, aggregate_failure_uses_insertion_order_and_preserves_all_causes)
{
    job_graph graph;
    const auto first = graph.add("first",
                                 [&]
                                 {
                                     gate.wait();
                                     throw 42;
                                 });
    const auto second = graph.add("second",
                                  []
                                  {
                                      throw std::runtime_error{"second failure"};
                                  });
    const auto execution = system.submit_graph(graph);
    // Observing the second exception proves it failed before releasing the first.
    std::promise<void> observed;
    auto ready = observed.get_future();
    system.submit(
        [&]
        {
            observed.set_value();
        });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    EXPECT_EQ(execution.status(second), job_status::failed);
    gate.open();
    for (int observation = 0; observation < 2; ++observation)
    {
        try
        {
            system.wait(execution);
            FAIL() << "Expected graph_error";
        }
        catch (const graph_error& error)
        {
            EXPECT_EQ(error.job_name(), "first");
            EXPECT_THROW(std::rethrow_exception(error.cause()), int);
        }
    }

    EXPECT_THROW(std::rethrow_exception(execution.exception(first)), int);
    EXPECT_THROW(std::rethrow_exception(execution.exception(second)), std::runtime_error);
}

/// @brief Verify rejects default and foreign job ids without adding edges.
TEST_F(job_system_fixture, rejects_default_and_foreign_job_ids_without_adding_edges)
{
    job_graph graph;
    job_graph other;
    const auto first = graph.add("first",
                                 []
                                 {
                                 });
    const auto foreign = other.add("foreign",
                                   []
                                   {
                                   });
    EXPECT_THROW(graph.precedes({}, first), std::invalid_argument);
    EXPECT_THROW(graph.precedes(first, {}), std::invalid_argument);
    EXPECT_THROW(graph.precedes(foreign, first), std::invalid_argument);
    EXPECT_THROW(graph.precedes(first, foreign), std::invalid_argument);
    const auto execution = system.submit_graph(graph);
    system.wait(execution);
    EXPECT_THROW((void)execution.status({}), std::invalid_argument);
    EXPECT_THROW((void)execution.exception(foreign), std::invalid_argument);
}

/// @brief Verify rejects invalid graph waits and worker side graph waits.
TEST_F(job_system_fixture, rejects_invalid_graph_waits_and_worker_side_graph_waits)
{
    EXPECT_THROW(system.wait(graph_handle{}), std::invalid_argument);
    job_graph graph;
    const auto execution = system.submit_graph(graph);
    job_system other{1};
    EXPECT_THROW(other.wait(execution), std::invalid_argument);
    system.wait(system.submit(
        [&]
        {
            EXPECT_THROW(system.wait(execution), std::logic_error);
        }));
    EXPECT_THROW((void)graph_handle{}.status({}), std::invalid_argument);
}

/// @brief Verify rejects empty names and callbacks.
TEST(graph_builder_tests, rejects_empty_names_and_callbacks)
{
    job_graph graph;
    EXPECT_THROW(graph.add("",
                           []
                           {
                           }),
                 std::invalid_argument);
    EXPECT_THROW(graph.add("empty callback", {}), std::invalid_argument);
}

/// @brief Scenario state for skipped chain tests, isolated per test and released through RAII.
class skipped_chain_tests : public testing::TestWithParam<std::size_t>
{
};

/// @brief Verify deep failure propagation finishes without running descendants.
TEST_P(skipped_chain_tests, deep_failure_propagation_finishes_without_running_descendants)
{
    job_system scheduler{2};
    job_graph graph;
    std::atomic<int> forbidden{0};
    auto previous = graph.add("failed root",
                              []
                              {
                                  throw 1;
                              });
    for (std::size_t index = 0; index < GetParam(); ++index)
    {
        const auto next = graph.add("descendant",
                                    [&]
                                    {
                                        ++forbidden;
                                    });
        graph.precedes(previous, next);
        previous = next;
    }

    const auto execution = scheduler.submit_graph(graph);
    EXPECT_THROW(scheduler.wait(execution), graph_error);
    EXPECT_EQ(execution.status(previous), job_status::skipped);
    EXPECT_EQ(forbidden.load(), 0);
    const auto counts = scheduler.statistics();
    EXPECT_EQ(counts.failed, 1U);
    EXPECT_EQ(counts.skipped, GetParam());
    EXPECT_EQ(counts.completed, GetParam() + 1);
}

INSTANTIATE_TEST_SUITE_P(depths, skipped_chain_tests, testing::Values(1U, 128U, 4096U));
} // namespace atlas::jobs::tests
