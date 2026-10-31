#include "job_system_fixture.hpp"

#include <algorithm>

namespace atlas::jobs::tests
{
TEST(trace_tests, disabled_tracing_retains_no_task_records)
{
    job_system system{1};
    system.dispatcher().run("plain", [](job_context&) {}).wait();
    const auto report = system.trace_report();
    EXPECT_FALSE(report.enabled);
    EXPECT_TRUE(report.tasks.empty());
}

TEST(trace_tests, bounded_tracing_records_identity_categories_dependencies_and_child_joins)
{
    job_system_options options;
    options.worker_count = 2;
    options.trace_capacity = 3;
    job_system system{options};
    auto root = system.dispatcher().run(job_label{"root", "phase-a"}, [](job_context& context)
                                        { context.spawn("child", [](job_context&) {}); });
    auto next = system.dispatcher().after(root, job_label{"next", "phase-b"}, [](job_context&) {});
    next.wait();
    system.dispatcher().run("overflow", [](job_context&) {}).wait();
    const auto report = system.trace_report();
    ASSERT_EQ(report.tasks.size(), 3U);
    EXPECT_EQ(report.dropped, 1U);
    EXPECT_EQ(report.pending, 0U);
    const auto child = std::ranges::find(report.tasks, std::string{"child"}, &task_trace::name);
    ASSERT_NE(child, report.tasks.end());
    EXPECT_EQ(child->parent_id, root.id());
    EXPECT_EQ(child->category, "phase-a");
    const auto successor = std::ranges::find(report.tasks, next.id(), &task_trace::id);
    ASSERT_NE(successor, report.tasks.end());
    EXPECT_EQ(successor->prerequisites, std::vector<std::uint64_t>{root.id()});
    EXPECT_EQ(next.category(), "phase-b");
    for (const auto& task : report.tasks)
    {
        EXPECT_LT(task.worker_index, options.worker_count);
        EXPECT_LE(task.submitted, task.started);
        EXPECT_LE(task.started, task.body_finished);
        EXPECT_LE(task.body_finished, task.completed);
    }
}

TEST_F(job_system_fixture, trace_snapshots_expose_pending_records_and_failed_successor_outcomes)
{
    job_system_options options;
    options.worker_count = 2;
    options.trace_capacity = 2;
    job_system traced{options};
    struct release_on_exit
    {
        signal_gate& gate;
        ~release_on_exit() noexcept
        {
            try
            {
                gate.open();
            }
            catch (...)
            {
                std::terminate();
            }
        }
    } release{gate};
    auto held = traced.dispatcher().run("held", &gate,
                                        [](job_context&, signal_gate*& waiting)
                                        {
                                            waiting->wait();
                                            throw 1;
                                        });
    auto skipped = traced.dispatcher().after(held, "skipped", [](job_context&) { FAIL(); });
    const auto live = traced.trace_report();
    EXPECT_EQ(live.pending, 2U);
    EXPECT_TRUE(live.tasks.empty());
    gate.open();
    EXPECT_THROW(skipped.wait(), job_error);
    const auto complete = traced.trace_report();
    EXPECT_EQ(complete.pending, 0U);
    ASSERT_EQ(complete.tasks.size(), 2U);
    EXPECT_EQ(complete.tasks[0].status, job_status::failed);
    EXPECT_EQ(complete.tasks[1].status, job_status::skipped);
}
} // namespace atlas::jobs::tests
