/**
 * @file
 * @brief Behavioral coverage for observer with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"
#include <atlas/jobs/job_observer.hpp>

#include <atomic>
#include <thread>

namespace atlas::jobs::tests
{
/// @brief Verify completion observers run outside pool locks and idle waits include delivery.
TEST(observer_tests, completion_observers_run_outside_pool_locks_and_idle_waits_include_delivery)
{
    /// @brief Optional adapter translating terminal direct-task metadata into independent
    /// logging/capture destinations.
    class observer final : public job_observer
    {
      public:
        /// @brief Capture terminal job metadata and exercise the scenario's observer contract.
        void completed(const task_event& event) noexcept override
        {
            delivered = event.status == job_status::succeeded && event.name == "observed" &&
                        event.id == 1 && event.parent_id == 0;
            try
            {
                unlocked = pool->statistics().completed == 1;
                pool->wait_idle();
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
            catch (...)
            {
                unexpected = true;
            }
        }

        /// @brief Scenario-owned pool used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        job_system* pool{nullptr};
        /// @brief Scenario-owned delivered used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        bool delivered{false};
        /// @brief Scenario-owned unlocked used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        bool unlocked{false}, rejected{false}, unexpected{false};
    };

    auto listener = std::make_shared<observer>();
    job_system_options options;
    options.observer = listener;
    job_system pool{options};
    listener->pool = &pool;
    pool.dispatcher().run("observed",
                          [](job_context&)
                          {
                          });
    pool.wait_idle();
    EXPECT_TRUE(listener->delivered);
    EXPECT_TRUE(listener->unlocked);
    EXPECT_TRUE(listener->rejected);
    EXPECT_FALSE(listener->unexpected);
}

/// @brief Verify logical parents emit after children with original execution threads.
TEST(observer_tests, logical_parents_emit_after_children_with_original_execution_threads)
{
    /// @brief Optional adapter translating terminal direct-task metadata into independent
    /// logging/capture destinations.
    class observer final : public job_observer
    {
      public:
        /// @brief Capture terminal job metadata and exercise the scenario's observer contract.
        void completed(const task_event& event) noexcept override
        {
            if (event.execution_thread == std::thread::id{} ||
                event.started > event.body_finished || event.body_finished > event.completed)
            {
                ++invalid;
            }

            ++calls;
        }

        /// @brief Scenario-owned calls used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        std::atomic<int> calls{0};
        /// @brief Scenario-owned invalid used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::atomic<int> invalid{0};
    };

    auto listener = std::make_shared<observer>();
    job_system_options options;
    options.observer = listener;
    job_system pool{options};
    pool.dispatcher().run("parent",
                          [](job_context& context)
                          {
                              context.spawn("child",
                                            [](job_context&)
                                            {
                                            });
                          });
    pool.shutdown();
    EXPECT_EQ(listener->calls.load(), 2);
    EXPECT_EQ(listener->invalid.load(), 0);
}

/// @brief Scenario state for caller observer tests, isolated per test and released through RAII.
class caller_observer_tests : public testing::TestWithParam<bool>
{
};

/// @brief Verify caller identity survives optional worker child completion.
TEST_P(caller_observer_tests, caller_identity_survives_optional_worker_child_completion)
{
    /// @brief Optional adapter translating terminal direct-task metadata into independent
    /// logging/capture destinations.
    class observer final : public job_observer
    {
      public:
        /// @brief Capture terminal job metadata and exercise the scenario's observer contract.
        void completed(const task_event& event) noexcept override
        {
            if (event.name == "notify")
            {
                notified = event.execution_thread == caller;
                try
                {
                    pool->wait_idle();
                }
                catch (const std::logic_error&)
                {
                    rejected = true;
                }
                catch (...)
                {
                    unexpected = true;
                }
            }
        }

        /// @brief Scenario-owned pool used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        job_system* pool{nullptr};
        /// @brief Scenario-owned caller used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::thread::id caller{std::this_thread::get_id()};
        /// @brief Scenario-owned notified used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        bool notified{false};
        /// @brief Scenario-owned rejected used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        bool rejected{false}, unexpected{false};
    };

    auto listener = std::make_shared<observer>();
    job_system_options options;
    options.observer = listener;
    job_system pool{options};
    listener->pool = &pool;
    auto jobs = pool.dispatcher();
    auto source = jobs.run("source",
                           [](job_context&)
                           {
                           });
    auto notification =
        jobs.callbacks().on_complete(source, "notify", GetParam(),
                                     [](job_context& context, const task_handle&, bool& spawn)
                                     {
                                         if (spawn)
                                         {
                                             context.spawn("child",
                                                           [](job_context&)
                                                           {
                                                           });
                                         }
                                     });
    notification.wait();
    pool.wait_idle();
    EXPECT_TRUE(listener->notified);
    EXPECT_TRUE(listener->rejected);
    EXPECT_FALSE(listener->unexpected);
}

INSTANTIATE_TEST_SUITE_P(children, caller_observer_tests, testing::Bool());
} // namespace atlas::jobs::tests
