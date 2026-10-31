#include "job_system_fixture.hpp"

#include <array>
#include <vector>

namespace atlas::jobs::tests
{
class priority_tests : public testing::Test
{
  protected:
    void hold_worker(job_priority priority = job_priority::normal)
    {
        auto started = std::make_shared<std::promise<void>>();
        auto ready = started->get_future();
        struct input
        {
            signal_gate* gate;
            std::shared_ptr<std::promise<void>> started;
        };
        system.dispatcher()
            .with_options({.priority = priority})
            .run("held", input{&gate, started},
                 [](job_context&, input& data)
                 {
                     data.started->set_value();
                     data.gate->wait();
                 });
        EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    }
    void TearDown() override
    {
        gate.open();
        system.shutdown();
    }
    job_system system{1};
    signal_gate gate;
};

TEST_F(priority_tests, weighted_dispatch_preserves_fifo_and_progress_for_every_ready_class)
{
    hold_worker();
    std::vector<int> order;
    struct input
    {
        std::vector<int>* order;
        int value;
    };
    constexpr std::array priorities{job_priority::high, job_priority::normal,
                                    job_priority::background};
    for (std::size_t index = 0; index < priorities.size(); ++index)
    {
        auto jobs = system.dispatcher().with_options({.priority = priorities[index]});
        for (int offset = 0; offset < 12; ++offset)
        {
            jobs.run("ordered", input{&order, static_cast<int>(index) * 100 + offset},
                     [](job_context&, input& data) { data.order->push_back(data.value); });
        }
    }
    gate.open();
    system.wait_idle();
    ASSERT_EQ(order.size(), 36U);
    std::array<int, 3> expected{};
    std::array<int, 3> first{-1, -1, -1};
    for (std::size_t index = 0; index < order.size(); ++index)
    {
        const auto group = static_cast<std::size_t>(order[index] / 100);
        EXPECT_EQ(order[index] % 100, expected[group]++);
        if (first[group] < 0)
        {
            first[group] = static_cast<int>(index);
        }
    }
    for (const auto position : first)
    {
        EXPECT_GE(position, 0);
        EXPECT_LT(position, 7);
    }
}

TEST_F(priority_tests, high_priority_overtakes_a_background_backlog_without_preempting_work)
{
    hold_worker();
    std::vector<job_priority> order;
    const auto record = [](job_context& context, std::vector<job_priority>*& output)
    { output->push_back(context.priority()); };
    auto background = system.dispatcher().with_options({.priority = job_priority::background});
    for (int index = 0; index < 128; ++index)
    {
        background.run("background", &order, record);
    }
    system.dispatcher()
        .with_options({.priority = job_priority::high})
        .run("urgent", &order, record);
    EXPECT_TRUE(order.empty());
    gate.open();
    system.wait_idle();
    ASSERT_EQ(order.size(), 129U);
    EXPECT_EQ(order.front(), job_priority::high);
}

TEST_F(priority_tests, invalid_priority_is_rejected_without_admitting_work)
{
    // Deliberately construct invalid input to test the public admission guard.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    const auto invalid = static_cast<job_priority>(3);
    EXPECT_THROW((void)system.dispatcher().with_options({.priority = invalid}),
                 std::invalid_argument);
    EXPECT_EQ(system.statistics().submitted, 0U);
}

TEST_F(priority_tests, late_arrivals_cannot_starve_a_class_with_previously_spent_credit)
{
    hold_worker(job_priority::background);
    std::vector<job_priority> order;
    const auto record = [](job_context& context, std::vector<job_priority>*& output)
    { output->push_back(context.priority()); };
    for (const auto priority : {job_priority::high, job_priority::normal})
    {
        auto jobs = system.dispatcher().with_options({.priority = priority});
        for (int index = 0; index < 16; ++index)
        {
            jobs.run("competing", &order, record);
        }
    }
    system.dispatcher()
        .with_options({.priority = job_priority::background})
        .run("progress", &order, record);
    gate.open();
    system.wait_idle();
    ASSERT_EQ(order.size(), 33U);
    EXPECT_EQ(order[12], job_priority::background);
}

TEST_F(priority_tests, blocked_high_priority_work_does_not_prevent_its_background_dependency)
{
    hold_worker();
    std::atomic<int> value{0};
    auto background = system.dispatcher().with_options({.priority = job_priority::background});
    auto first = background.run("dependency", &value,
                                [](job_context&, std::atomic<int>*& data) { data->store(42); });
    auto high = system.dispatcher().with_options({.priority = job_priority::high});
    auto next = high.after(first, "dependent", &value, [](job_context&, std::atomic<int>*& data)
                           { EXPECT_EQ(data->load(), 42); });
    for (int index = 0; index < 32; ++index)
    {
        high.run("competing", [](job_context&) {});
    }
    gate.open();
    EXPECT_NO_THROW(next.wait());
    system.wait_idle();
    EXPECT_EQ(system.statistics().waiting, 0U);
}
} // namespace atlas::jobs::tests
