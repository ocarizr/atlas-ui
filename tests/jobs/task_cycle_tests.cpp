#include "job_system_fixture.hpp"

#include <atomic>
#include <string>

namespace atlas::jobs::tests
{
struct cycle_input
{
    signal_gate* gate;
    std::shared_ptr<task_handle> dependency;
    std::shared_ptr<std::atomic<bool>> rejected;
};

void attempt_cycle(job_context& context, cycle_input& data)
{
    data.gate->wait();
    try
    {
        context.after(*data.dependency, "forbidden", 0, [](job_context&, int&) { FAIL(); });
    }
    catch (const std::invalid_argument& error)
    {
        *data.rejected = std::string{error.what()}.find("cycle") != std::string::npos;
    }
}

TEST_F(job_system_fixture, a_child_cannot_depend_on_its_own_parent_completion)
{
    auto dependency = std::make_shared<task_handle>();
    auto rejected = std::make_shared<std::atomic<bool>>(false);
    auto parent =
        system.dispatcher().run("parent", cycle_input{&gate, dependency, rejected}, attempt_cycle);
    *dependency = parent;
    gate.open();
    parent.wait();
    EXPECT_TRUE(rejected->load());
    EXPECT_EQ(system.statistics().submitted, 1U);
}

TEST_F(job_system_fixture, a_child_cannot_wait_for_an_external_successor_of_its_parent)
{
    auto jobs = system.dispatcher();
    auto dependency = std::make_shared<task_handle>();
    auto rejected = std::make_shared<std::atomic<bool>>(false);
    auto parent = jobs.run("parent", cycle_input{&gate, dependency, rejected}, attempt_cycle);
    auto successor = jobs.after(parent, "successor", 0, [](job_context&, int&) {});
    *dependency = successor;
    gate.open();
    successor.wait();
    EXPECT_TRUE(rejected->load());
    EXPECT_EQ(system.statistics().submitted, 2U);
}

TEST_F(job_system_fixture,
       cycle_detection_follows_child_join_edges_as_well_as_explicit_dependencies)
{
    auto jobs = system.dispatcher();
    auto dependency = std::make_shared<task_handle>();
    auto rejected = std::make_shared<std::atomic<bool>>(false);
    auto parent = jobs.run("parent", cycle_input{&gate, dependency, rejected}, attempt_cycle);
    struct producer_input
    {
        task_handle prerequisite;
        std::shared_ptr<std::promise<void>> published;
    };
    auto published = std::make_shared<std::promise<void>>();
    auto ready = published->get_future();
    auto other = jobs.run("other parent", producer_input{parent, published},
                          [](job_context& context, producer_input& data)
                          {
                              context.after(data.prerequisite, "waiting child", 0,
                                            [](job_context&, int&) {});
                              data.published->set_value();
                          });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    *dependency = other;
    gate.open();
    other.wait();
    EXPECT_TRUE(rejected->load());
    EXPECT_EQ(system.statistics().submitted, 3U);
}
} // namespace atlas::jobs::tests
