/**
 * @file
 * @brief Behavioral coverage for graph lifecycle with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <atomic>
#include <stdexcept>
#include <utility>

namespace atlas::jobs::tests
{
/// @brief Verify accepted graph owns a snapshot after builder changes and destruction.
TEST_F(job_system_fixture, accepted_graph_owns_a_snapshot_after_builder_changes_and_destruction)
{
    graph_handle execution;
    job_id root;
    job_id later;
    std::atomic<int> executed{0};
    {
        job_graph graph;
        root = graph.add("root",
                         [&]
                         {
                             gate.wait();
                             ++executed;
                         });
        execution = system.submit_graph(graph);
        later = graph.add("later",
                          [&]
                          {
                              ++executed;
                          });
        // Even a subsequent invalid edit must not affect accepted work.
        graph.precedes(later, later);
    }
    gate.open();
    system.wait(execution);
    EXPECT_EQ(executed.load(), 1);
    EXPECT_EQ(execution.status(root), job_status::succeeded);
    EXPECT_THROW((void)execution.status(later), std::invalid_argument);
}

/// @brief Verify graph can be submitted repeatedly and from an independent job.
TEST_F(job_system_fixture, graph_can_be_submitted_repeatedly_and_from_an_independent_job)
{
    job_graph graph;
    std::atomic<int> executed{0};
    const auto root = graph.add("root",
                                [&]
                                {
                                    ++executed;
                                });
    const auto leaf = graph.add("leaf",
                                [&]
                                {
                                    ++executed;
                                });
    graph.precedes(root, leaf);
    system.wait(system.submit(
        [&]
        {
            system.submit_graph(graph);
        }));
    system.wait(system.submit_graph(graph));
    system.wait_idle();
    EXPECT_EQ(executed.load(), 4);
}

/// @brief Verify shutdown drains dependency waiting jobs and rejects later graphs.
TEST_F(job_system_fixture, shutdown_drains_dependency_waiting_jobs_and_rejects_later_graphs)
{
    job_graph graph;
    std::atomic<int> executed{0};
    auto previous = graph.add("first",
                              [&]
                              {
                                  ++executed;
                              });
    for (int index = 0; index < 100; ++index)
    {
        const auto next = graph.add("next",
                                    [&]
                                    {
                                        ++executed;
                                    });
        graph.precedes(previous, next);
        previous = next;
    }

    const auto execution = system.submit_graph(graph);
    system.shutdown();
    EXPECT_EQ(executed.load(), 101);
    EXPECT_EQ(execution.status(previous), job_status::succeeded);
    EXPECT_NO_THROW(system.wait(execution));
    EXPECT_THROW(system.submit_graph(graph), std::logic_error);
    job_graph empty;
    EXPECT_THROW(system.submit_graph(empty), std::logic_error);
}

/// @brief Verify dropping handles and builder does not abandon accepted graphs.
TEST(graph_lifecycle_tests, dropping_handles_and_builder_does_not_abandon_accepted_graphs)
{
    std::atomic<int> executed{0};
    {
        job_system scheduler{2};
        {
            job_graph graph;
            const auto root = graph.add("root",
                                        [&]
                                        {
                                            ++executed;
                                        });
            const auto leaf = graph.add("leaf",
                                        [&]
                                        {
                                            ++executed;
                                        });
            graph.precedes(root, leaf);
            scheduler.submit_graph(graph);
        }
    }
    EXPECT_EQ(executed.load(), 2);
}

/// @brief Verify completed handle keeps diagnostics after scheduler destruction.
TEST(graph_lifecycle_tests, completed_handle_keeps_diagnostics_after_scheduler_destruction)
{
    graph_handle execution;
    job_id job;
    {
        job_system scheduler{1};
        job_graph graph;
        job = graph.add("job",
                        []
                        {
                        });
        execution = scheduler.submit_graph(graph);
    }
    EXPECT_EQ(execution.status(job), job_status::succeeded);
    EXPECT_FALSE(execution.exception(job));
}

/// @brief Verify moving a builder preserves ids and rejects moved from use.
TEST_F(job_system_fixture, moving_a_builder_preserves_ids_and_rejects_moved_from_use)
{
    job_graph graph;
    const auto root = graph.add("root",
                                []
                                {
                                });
    auto moved = std::move(graph);
    const auto leaf = moved.add("leaf",
                                []
                                {
                                });
    moved.precedes(root, leaf);
    system.wait(system.submit_graph(moved));
    // The public contract explicitly rejects moved-from builder operations.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move,bugprone-use-after-move)
    EXPECT_THROW(graph.add("invalid",
                           []
                           {
                           }),
                 std::logic_error);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move,bugprone-use-after-move)
    EXPECT_THROW(graph.precedes(root, leaf), std::logic_error);
    EXPECT_THROW(system.submit_graph(graph), std::logic_error);
    job_graph destination;
    const auto replaced = destination.add("replaced",
                                          []
                                          {
                                          });
    destination = std::move(moved);
    const auto execution = system.submit_graph(destination);
    system.wait(execution);
    EXPECT_EQ(execution.status(root), job_status::succeeded);
    EXPECT_THROW((void)execution.status(replaced), std::invalid_argument);
}

/// @brief Verify skipped callback captures are released outside scheduler locks.
TEST_F(job_system_fixture, skipped_callback_captures_are_released_outside_scheduler_locks)
{
    std::atomic<int> released{0};
    graph_handle execution;
    {
        job_graph graph;
        auto resource = std::shared_ptr<int>(new int{1},
                                             [&](int* value)
                                             {
                                                 (void)system.statistics();
                                                 delete value;
                                                 ++released;
                                             });
        const auto root = graph.add("failure",
                                    [&]
                                    {
                                        gate.wait();
                                        throw 1;
                                    });
        const auto leaf = graph.add("skipped",
                                    [resource]
                                    {
                                    });
        graph.precedes(root, leaf);
        execution = system.submit_graph(graph);
    }
    gate.open();
    EXPECT_THROW(system.wait(execution), graph_error);
    system.shutdown();
    EXPECT_EQ(released.load(), 1);
}
} // namespace atlas::jobs::tests
