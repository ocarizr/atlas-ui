#include "job_system_fixture.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <vector>

namespace atlas::jobs::tests
{
TEST_F(job_system_fixture, graph_chain_observes_prerequisite_writes)
{
    job_graph graph;
    int value = 0;
    const auto first = graph.add("produce", [&] { value = 21; });
    const auto second = graph.add("transform", [&] { value *= 2; });
    const auto third = graph.add("consume", [&] { EXPECT_EQ(value, 42); });
    graph.precedes(first, second);
    graph.precedes(second, third);
    const auto execution = system.submit_graph(graph);
    system.wait(execution);
    EXPECT_EQ(execution.status(third), job_status::succeeded);
    EXPECT_NO_THROW(system.wait(execution));
}

class graph_width_tests : public testing::TestWithParam<std::size_t>
{
};

TEST_P(graph_width_tests, fan_out_and_fan_in_execute_each_callback_once)
{
    job_system scheduler{GetParam()};
    job_graph graph;
    std::array<int, 64> values{};
    const auto root = graph.add("root", [&] { values.fill(1); });
    const auto join = graph.add("join",
                                [&]
                                {
                                    for (const auto value : values)
                                    {
                                        EXPECT_EQ(value, 2);
                                    }
                                });
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        const auto branch = graph.add("branch", [&, index] { ++values[index]; });
        graph.precedes(root, branch);
        graph.precedes(branch, join);
    }
    scheduler.wait(scheduler.submit_graph(graph));
    EXPECT_EQ(scheduler.statistics().completed, 66U);
}

INSTANTIATE_TEST_SUITE_P(worker_counts, graph_width_tests, testing::Values(1U, 2U, 4U));

TEST_F(job_system_fixture, a_pending_dependency_does_not_occupy_the_other_worker)
{
    job_graph graph;
    std::promise<void> started;
    auto observed = started.get_future();
    const auto root = graph.add("held root", [&] { gate.wait(); });
    const auto dependent = graph.add("dependent", [] {});
    graph.precedes(root, dependent);
    const auto execution = system.submit_graph(graph);
    system.submit([&] { started.set_value(); });
    EXPECT_EQ(observed.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    EXPECT_EQ(execution.status(dependent), job_status::pending);
    gate.open();
    system.wait(execution);
}

TEST_F(job_system_fixture, empty_graph_completes_without_changing_job_counts)
{
    job_graph graph;
    system.wait(system.submit_graph(graph));
    EXPECT_EQ(system.statistics().submitted, 0U);
}

TEST_F(job_system_fixture, duplicate_edges_do_not_duplicate_execution_or_dependency_counts)
{
    job_graph graph;
    std::atomic<int> count{0};
    const auto root = graph.add("root", [] {});
    const auto leaf = graph.add("leaf", [&] { ++count; });
    graph.precedes(root, leaf);
    graph.precedes(root, leaf);
    system.wait(system.submit_graph(graph));
    EXPECT_EQ(count.load(), 1);
}

TEST_F(job_system_fixture, diagnostics_distinguish_ready_running_and_dependency_waiting_jobs)
{
    job_graph graph;
    std::promise<void> first_started;
    std::promise<void> second_started;
    auto first = first_started.get_future();
    auto second = second_started.get_future();
    const auto held = graph.add("held",
                                [&]
                                {
                                    first_started.set_value();
                                    gate.wait();
                                });
    graph.add("also held",
              [&]
              {
                  second_started.set_value();
                  gate.wait();
              });
    const auto ready = graph.add("ready", [] {});
    const auto pending = graph.add("pending", [] {});
    graph.precedes(held, pending);
    const auto execution = system.submit_graph(graph);
    EXPECT_EQ(first.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    EXPECT_EQ(second.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    EXPECT_EQ(execution.status(held), job_status::running);
    EXPECT_EQ(execution.status(ready), job_status::ready);
    EXPECT_EQ(execution.status(pending), job_status::pending);
    const auto counts = system.statistics();
    EXPECT_EQ(counts.running, 2U);
    EXPECT_EQ(counts.queued, 1U);
    EXPECT_EQ(counts.waiting, 1U);
    EXPECT_EQ(counts.submitted, counts.queued + counts.running + counts.waiting + counts.completed);
    gate.open();
    system.wait(execution);
}
} // namespace atlas::jobs::tests
