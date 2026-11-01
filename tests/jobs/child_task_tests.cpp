/**
 * @file
 * @brief Behavioral coverage for child task with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <atomic>
#include <optional>

namespace atlas::jobs::tests
{
/// @brief Verify parent completion joins children and grandchildren on one worker.
TEST_F(job_system_fixture, parent_completion_joins_children_and_grandchildren_on_one_worker)
{
    job_system single{1};
    auto count = std::make_shared<std::atomic<int>>(0);
    auto root =
        single.dispatcher().run("root", count,
                                [](job_context& context, auto& value)
                                {
                                    context.spawn("child", value,
                                                  [](job_context& child, auto& data)
                                                  {
                                                      child.spawn("grandchild", data,
                                                                  [](job_context&, auto& result)
                                                                  {
                                                                      ++*result;
                                                                  });
                                                  });
                                });
    root.wait();
    EXPECT_EQ(count->load(), 1);
    EXPECT_EQ(single.statistics().completed, 3U);
}

/// @brief Verify parent payload survives until children finish.
TEST_F(job_system_fixture, parent_payload_survives_until_children_finish)
{
    job_system single{1};

    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned value used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        std::unique_ptr<int> value;
        /// @brief Scenario-owned result used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<std::atomic<int>> result;
    };

    auto result = std::make_shared<std::atomic<int>>(0);
    single.dispatcher()
        .run("parent", input{std::make_unique<int>(42), result},
             [](job_context& context, input& data)
             {
                 /// @brief Scenario state for borrowed, isolated per test and released through
                 /// RAII.
                 struct borrowed
                 {
                     /// @brief Scenario-owned value used to observe behavior and cleanup without
                     /// sharing mutable state between tests.
                     int* value;
                     /// @brief Scenario-owned result used to observe behavior and cleanup without
                     /// sharing mutable state between tests.
                     std::shared_ptr<std::atomic<int>> result;
                 };
                 context.spawn("borrowed", borrowed{data.value.get(), data.result},
                               [](job_context&, borrowed& child)
                               {
                                   *child.result = *child.value;
                               });
             })
        .wait();
    EXPECT_EQ(result->load(), 42);
    // Async unique_ptr ownership escapes analyzer modeling; payload cleanup is tested separately.
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

/// @brief Verify child failure reaches the parent and its dependents.
TEST_F(job_system_fixture, child_failure_reaches_the_parent_and_its_dependents)
{
    auto jobs = system.dispatcher();
    auto parent = jobs.run("parent", 0,
                           [](job_context& context, int&)
                           {
                               context.spawn("broken child", 0,
                                             [](job_context&, int&)
                                             {
                                                 throw 42;
                                             });
                           });
    EXPECT_THROW(parent.wait(), job_error);
    EXPECT_THROW(std::rethrow_exception(parent.exception()), int);
    auto skipped = jobs.after(parent, "dependent", 0,
                              [](job_context&, int&)
                              {
                                  FAIL();
                              });
    EXPECT_THROW(skipped.wait(), job_error);
    EXPECT_EQ(skipped.status(), job_status::skipped);
}

/// @brief Verify a retained context cannot spawn after its body closes.
TEST_F(job_system_fixture, a_retained_context_cannot_spawn_after_its_body_closes)
{
    auto retained = std::make_shared<std::optional<job_context>>();
    auto root = system.dispatcher().run("context", retained,
                                        [](job_context& context, auto& saved)
                                        {
                                            saved->emplace(context);
                                        });
    root.wait();
    const auto& saved = *retained;
    if (!saved.has_value())
    {
        FAIL() << "The callback did not retain its context";
        return;
    }

    EXPECT_THROW(saved->spawn("late", 0,
                              [](job_context&, int&)
                              {
                              }),
                 std::logic_error);
}

/// @brief Verify child waits are rejected without blocking the worker.
TEST_F(job_system_fixture, child_waits_are_rejected_without_blocking_the_worker)
{
    auto observed = std::make_shared<std::atomic<bool>>(false);
    system.dispatcher()
        .run("parent", observed,
             [](job_context& context, auto& result)
             {
                 auto child = context.spawn("child", 0,
                                            [](job_context&, int&)
                                            {
                                            });
                 try
                 {
                     child.wait();
                 }
                 catch (const std::logic_error&)
                 {
                     *result = true;
                 }
             })
        .wait();
    EXPECT_TRUE(observed->load());
}
} // namespace atlas::jobs::tests
