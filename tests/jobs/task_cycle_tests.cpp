/**
 * @file
 * @brief Behavioral coverage for task cycle with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <atomic>
#include <string>

namespace atlas::jobs::tests
{
/// @brief Scenario state for cycle input, isolated per test and released through RAII.
struct cycle_input
{
    /// @brief Scenario-owned gate used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    signal_gate* gate;
    /// @brief Scenario-owned dependency used to observe behavior and cleanup without sharing
    /// mutable state between tests.
    std::shared_ptr<task_handle> dependency;
    /// @brief Scenario-owned rejected used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::shared_ptr<std::atomic<bool>> rejected;
};

/// @brief Attempt the scenario's dependency cycle and retain its expected contract failure.
void attempt_cycle(job_context& context, cycle_input& data)
{
    data.gate->wait();
    try
    {
        context.after(*data.dependency, "forbidden", 0,
                      [](job_context&, int&)
                      {
                          FAIL();
                      });
    }
    catch (const std::invalid_argument& error)
    {
        *data.rejected = std::string{error.what()}.find("cycle") != std::string::npos;
    }
}

/// @brief Verify a child cannot depend on its own parent completion.
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

/// @brief Verify a child cannot wait for an external successor of its parent.
TEST_F(job_system_fixture, a_child_cannot_wait_for_an_external_successor_of_its_parent)
{
    auto jobs = system.dispatcher();
    auto dependency = std::make_shared<task_handle>();
    auto rejected = std::make_shared<std::atomic<bool>>(false);
    auto parent = jobs.run("parent", cycle_input{&gate, dependency, rejected}, attempt_cycle);
    auto successor = jobs.after(parent, "successor", 0,
                                [](job_context&, int&)
                                {
                                });
    *dependency = successor;
    gate.open();
    successor.wait();
    EXPECT_TRUE(rejected->load());
    EXPECT_EQ(system.statistics().submitted, 2U);
}

/// @brief Verify cycle detection follows child join edges as well as explicit dependencies.
TEST_F(job_system_fixture,
       cycle_detection_follows_child_join_edges_as_well_as_explicit_dependencies)
{
    auto jobs = system.dispatcher();
    auto dependency = std::make_shared<task_handle>();
    auto rejected = std::make_shared<std::atomic<bool>>(false);
    auto parent = jobs.run("parent", cycle_input{&gate, dependency, rejected}, attempt_cycle);

    /// @brief Scenario state for producer input, isolated per test and released through RAII.
    struct producer_input
    {
        /// @brief Scenario-owned prerequisite used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        task_handle prerequisite;
        /// @brief Scenario-owned published used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<std::promise<void>> published;
    };

    auto published = std::make_shared<std::promise<void>>();
    auto ready = published->get_future();
    auto other = jobs.run("other parent", producer_input{parent, published},
                          [](job_context& context, producer_input& data)
                          {
                              context.after(data.prerequisite, "waiting child", 0,
                                            [](job_context&, int&)
                                            {
                                            });
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
