/**
 * @file
 * @brief Behavioral coverage for task lifetime with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <atomic>

namespace atlas::jobs::tests
{
/// @brief Scenario state for cleanup probe, isolated per test and released through RAII.
struct cleanup_probe
{
    /// @brief Scenario-owned system used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    job_system* system;
    /// @brief Scenario-owned released used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::shared_ptr<std::atomic<int>> released;

    /// @brief Release cleanup probe resources and complete its scoped cleanup.
    ~cleanup_probe()
    {
        (void)system->statistics();
        ++*released;
    }
};

/// @brief Scenario state for probe failure, isolated per test and released through RAII.
struct probe_failure
{
    /// @brief Scenario-owned probe used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::shared_ptr<cleanup_probe> probe;
};

/// @brief Scenario state for replacement input, isolated per test and released through RAII.
struct replacement_input
{
    /// @brief Scenario-owned system used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    job_system* system;
    /// @brief Scenario-owned gate used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    signal_gate* gate;
    /// @brief Scenario-owned started used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::shared_ptr<std::promise<void>> started;
    /// @brief Scenario-owned published used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::shared_ptr<std::promise<void>> published;
    /// @brief Scenario-owned released used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::shared_ptr<std::atomic<int>> released;
};

/// @brief Admit child failures in deterministic registration order for propagation checks.
void spawn_ordered_failures(job_context& context, replacement_input& data)
{
    context.spawn("first child", data,
                  [](job_context&, replacement_input& child)
                  {
                      child.started->set_value();
                      child.gate->wait();
                      throw 0;
                  });
    context.spawn("second child", data,
                  [](job_context&, replacement_input& child)
                  {
                      auto probe = std::make_shared<cleanup_probe>();
                      probe->system = child.system;
                      probe->released = child.released;
                      throw probe_failure{std::move(probe)};
                  });
    data.published->set_value();
}

/// @brief Verify replacing an earlier child failure does not destroy exceptions under lock.
TEST_F(job_system_fixture,
       replacing_an_earlier_child_failure_does_not_destroy_exceptions_under_lock)
{
    auto started = std::make_shared<std::promise<void>>();
    auto ready = started->get_future();
    auto published = std::make_shared<std::promise<void>>();
    auto children_published = published->get_future();
    auto released = std::make_shared<std::atomic<int>>(0);
    auto parent = system.dispatcher().run(
        "parent", replacement_input{&system, &gate, started, published, released},
        spawn_ordered_failures);
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    EXPECT_EQ(children_published.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    system.wait(system.submit(
        []
        {
        }));
    gate.open();
    EXPECT_THROW(parent.wait(), job_error);
    EXPECT_THROW(std::rethrow_exception(parent.exception()), int);
    EXPECT_EQ(released->load(), 1);
}

/// @brief Verify discarded parent failures release exception objects outside pool locks.
TEST_F(job_system_fixture, discarded_parent_failures_release_exception_objects_outside_pool_locks)
{
    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned system used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        job_system* system;
        /// @brief Scenario-owned gate used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        signal_gate* gate;
        /// @brief Scenario-owned started used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<std::promise<void>> started;
        /// @brief Scenario-owned released used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<std::atomic<int>> released;
    };

    auto started = std::make_shared<std::promise<void>>();
    auto ready = started->get_future();
    auto released = std::make_shared<std::atomic<int>>(0);
    auto parent = system.dispatcher().run("parent", input{&system, &gate, started, released},
                                          [](job_context& context, input& data)
                                          {
                                              context.spawn("held child", data,
                                                            [](job_context&, input& child)
                                                            {
                                                                child.started->set_value();
                                                                child.gate->wait();
                                                            });
                                              auto probe = std::make_shared<cleanup_probe>();
                                              probe->system = data.system;
                                              probe->released = data.released;
                                              throw probe_failure{std::move(probe)};
                                          });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    system.wait(system.submit(
        []
        {
        })); // The other worker has closed the parent body.
    EXPECT_EQ(parent.status(), job_status::joining);
    parent = {};
    gate.open();
    system.shutdown();
    EXPECT_EQ(released->load(), 1);
}
} // namespace atlas::jobs::tests
