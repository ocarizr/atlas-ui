#include "job_system_fixture.hpp"

#include <atomic>

namespace atlas::jobs::tests
{
TEST_F(job_system_fixture, fan_in_reports_declared_dependency_order_after_reversed_completion)
{
    auto jobs = system.dispatcher();
    auto first = jobs.run("first", &gate,
                          [](job_context&, signal_gate*& held)
                          {
                              held->wait();
                              throw 1;
                          });
    auto second = jobs.run("second", [](job_context&) { throw 2; });
    auto forbidden = std::make_shared<std::atomic<int>>(0);
    auto joined = jobs.after({first, second}, "joined", forbidden,
                             [](job_context&, auto& calls) { ++*calls; });
    EXPECT_THROW(second.wait(), job_error);
    gate.open();
    try
    {
        joined.wait();
        FAIL() << "A failed prerequisite must skip the successor";
    }
    catch (const job_error& error)
    {
        EXPECT_EQ(error.job_name(), "first");
    }
    EXPECT_EQ(forbidden->load(), 0);
    EXPECT_EQ(joined.status(), job_status::skipped);
}

TEST_F(job_system_fixture, a_parent_body_failure_takes_precedence_over_child_failure)
{
    auto root = system.dispatcher().run("parent",
                                        [](job_context& context)
                                        {
                                            context.spawn("child", [](job_context&) { throw 1; });
                                            throw std::runtime_error{"parent failure"};
                                        });
    try
    {
        root.wait();
        FAIL() << "Both callbacks fail";
    }
    catch (const job_error& error)
    {
        EXPECT_EQ(error.job_name(), "parent");
        EXPECT_THROW(std::rethrow_exception(error.cause()), std::runtime_error);
    }
    EXPECT_EQ(system.statistics().failed, 2U);
}

TEST_F(job_system_fixture, direct_pending_dependencies_leave_workers_available_for_ready_work)
{
    auto jobs = system.dispatcher();
    auto held =
        jobs.run("held", &gate, [](job_context&, signal_gate*& blocked) { blocked->wait(); });
    auto dependent = jobs.after(held, "dependent", [](job_context&) {});
    auto observed = std::make_shared<std::promise<void>>();
    auto ready = observed->get_future();
    auto independent =
        jobs.run("ready", observed, [](job_context&, auto& signal) { signal->set_value(); });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    EXPECT_FALSE(dependent.ready());
    gate.open();
    jobs.fence("all", {dependent, independent}).wait();
}
} // namespace atlas::jobs::tests
