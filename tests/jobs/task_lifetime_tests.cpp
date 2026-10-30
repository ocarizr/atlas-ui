#include "job_system_fixture.hpp"

#include <atomic>

namespace atlas::jobs::tests
{
struct cleanup_probe
{
    job_system* system;
    std::shared_ptr<std::atomic<int>> released;
    ~cleanup_probe()
    {
        (void)system->statistics();
        ++*released;
    }
};

struct probe_failure
{
    std::shared_ptr<cleanup_probe> probe;
};

struct replacement_input
{
    job_system* system;
    signal_gate* gate;
    std::shared_ptr<std::promise<void>> started;
    std::shared_ptr<std::promise<void>> published;
    std::shared_ptr<std::atomic<int>> released;
};

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
    system.wait(system.submit([] {}));
    gate.open();
    EXPECT_THROW(parent.wait(), job_error);
    EXPECT_THROW(std::rethrow_exception(parent.exception()), int);
    EXPECT_EQ(released->load(), 1);
}

TEST_F(job_system_fixture, discarded_parent_failures_release_exception_objects_outside_pool_locks)
{
    struct input
    {
        job_system* system;
        signal_gate* gate;
        std::shared_ptr<std::promise<void>> started;
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
    system.wait(system.submit([] {})); // The other worker has closed the parent body.
    EXPECT_EQ(parent.status(), job_status::joining);
    parent = {};
    gate.open();
    system.shutdown();
    EXPECT_EQ(released->load(), 1);
}
} // namespace atlas::jobs::tests
