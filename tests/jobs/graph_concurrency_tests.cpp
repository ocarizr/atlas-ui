#include "job_system_fixture.hpp"

#include <atomic>
#include <cstdint>
#include <ostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace atlas::jobs::tests
{
struct graph_case
{
    std::size_t workers;
    unsigned seed;
};

std::ostream& operator<<(std::ostream& output, const graph_case& parameters)
{
    return output << "workers=" << parameters.workers << ", seed=" << parameters.seed;
}

class generated_graph_tests : public testing::TestWithParam<graph_case>
{
};

TEST_P(generated_graph_tests, dependency_results_match_a_sequential_reference)
{
    const auto parameters = GetParam();
    std::mt19937 random{parameters.seed};
    job_system scheduler{parameters.workers};
    job_graph graph;
    std::vector<std::uint64_t> results(128);
    std::vector<std::uint64_t> reference(128);
    std::vector<job_id> ids;
    ids.reserve(results.size());
    for (std::size_t index = 0; index < results.size(); ++index)
    {
        std::vector<std::size_t> dependencies;
        reference[index] = 1;
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (random() % 7 == 0)
            {
                dependencies.push_back(previous);
                reference[index] += reference[previous];
            }
        }
        ids.push_back(graph.add(std::to_string(index),
                                [&, index, dependencies]
                                {
                                    std::uint64_t value = 1;
                                    for (const auto previous : dependencies)
                                    {
                                        value += results[previous];
                                    }
                                    results[index] = value;
                                }));
        for (const auto dependency : dependencies)
        {
            graph.precedes(ids[dependency], ids.back());
        }
    }
    const auto execution = scheduler.submit_graph(graph);
    scheduler.wait(execution);
    EXPECT_EQ(results, reference);
    for (const auto& id : ids)
    {
        EXPECT_EQ(execution.status(id), job_status::succeeded);
    }
}

INSTANTIATE_TEST_SUITE_P(reproducible_schedules, generated_graph_tests,
                         testing::Values(graph_case{1, 7}, graph_case{2, 7}, graph_case{4, 7},
                                         graph_case{1, 42}, graph_case{2, 42}, graph_case{4, 42},
                                         graph_case{4, 2026}),
                         [](const testing::TestParamInfo<graph_case>& info)
                         {
                             return "workers_" + std::to_string(info.param.workers) + "_seed_" +
                                    std::to_string(info.param.seed);
                         });

TEST_F(job_system_fixture, concurrent_graph_producers_share_the_pool_with_independent_jobs)
{
    std::atomic<int> executed{0};
    {
        std::vector<std::jthread> producers;
        producers.reserve(4);
        for (int producer = 0; producer < 4; ++producer)
        {
            producers.emplace_back(
                [&]
                {
                    for (int iteration = 0; iteration < 20; ++iteration)
                    {
                        job_graph graph;
                        const auto root = graph.add("root", [&] { ++executed; });
                        const auto leaf = graph.add("leaf", [&] { ++executed; });
                        graph.precedes(root, leaf);
                        system.submit_graph(graph);
                        system.submit([&] { ++executed; });
                    }
                });
        }
    }
    system.wait_idle();
    EXPECT_EQ(executed.load(), 240);
    const auto counts = system.statistics();
    EXPECT_EQ(counts.submitted, counts.completed);
    EXPECT_EQ(counts.waiting, 0U);
    EXPECT_EQ(counts.queued, 0U);
    EXPECT_EQ(counts.running, 0U);
}

TEST_F(job_system_fixture, failed_fan_in_waits_for_all_prerequisites_and_skips_once)
{
    job_graph graph;
    const auto held = graph.add("held", [&] { gate.wait(); });
    const auto failed = graph.add("failed", [] { throw 1; });
    const auto also_failed = graph.add("also failed", [] { throw 2; });
    std::atomic<int> forbidden{0};
    const auto join = graph.add("join", [&] { ++forbidden; });
    graph.precedes(held, join);
    graph.precedes(failed, join);
    graph.precedes(also_failed, join);
    const auto execution = system.submit_graph(graph);
    std::promise<void> observed;
    auto ready = observed.get_future();
    const auto observer = system.submit([&] { observed.set_value(); });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    EXPECT_EQ(execution.status(join), job_status::pending);
    EXPECT_EQ(system.statistics().waiting, 1U);
    gate.open();
    EXPECT_THROW(system.wait(execution), graph_error);
    EXPECT_EQ(execution.status(join), job_status::skipped);
    EXPECT_EQ(forbidden.load(), 0);
    // Signalling from the observer body does not publish its scheduler completion.
    // Graph completion joins only graph nodes, not this independent observer.
    system.wait(observer);
    const auto counts = system.statistics();
    EXPECT_EQ(counts.failed, 2U);
    EXPECT_EQ(counts.skipped, 1U);
    EXPECT_EQ(counts.completed, 5U);
}
} // namespace atlas::jobs::tests
