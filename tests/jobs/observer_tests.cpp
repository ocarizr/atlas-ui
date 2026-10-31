#include "job_system_fixture.hpp"
#include <atlas/jobs/job_observer.hpp>

#include <atomic>
#include <thread>

namespace atlas::jobs::tests
{
TEST(observer_tests, completion_observers_run_outside_pool_locks_and_idle_waits_include_delivery)
{
    class observer final : public job_observer
    {
      public:
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
        job_system* pool{nullptr};
        bool delivered{false};
        bool unlocked{false}, rejected{false}, unexpected{false};
    };
    auto listener = std::make_shared<observer>();
    job_system_options options;
    options.observer = listener;
    job_system pool{options};
    listener->pool = &pool;
    pool.dispatcher().run("observed", [](job_context&) {});
    pool.wait_idle();
    EXPECT_TRUE(listener->delivered);
    EXPECT_TRUE(listener->unlocked);
    EXPECT_TRUE(listener->rejected);
    EXPECT_FALSE(listener->unexpected);
}

TEST(observer_tests, logical_parents_emit_after_children_with_original_execution_threads)
{
    class observer final : public job_observer
    {
      public:
        void completed(const task_event& event) noexcept override
        {
            if (event.execution_thread == std::thread::id{} ||
                event.started > event.body_finished || event.body_finished > event.completed)
            {
                ++invalid;
            }
            ++calls;
        }
        std::atomic<int> calls{0};
        std::atomic<int> invalid{0};
    };
    auto listener = std::make_shared<observer>();
    job_system_options options;
    options.observer = listener;
    job_system pool{options};
    pool.dispatcher().run("parent", [](job_context& context)
                          { context.spawn("child", [](job_context&) {}); });
    pool.shutdown();
    EXPECT_EQ(listener->calls.load(), 2);
    EXPECT_EQ(listener->invalid.load(), 0);
}

class caller_observer_tests : public testing::TestWithParam<bool>
{
};
TEST_P(caller_observer_tests, caller_identity_survives_optional_worker_child_completion)
{
    class observer final : public job_observer
    {
      public:
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
        job_system* pool{nullptr};
        std::thread::id caller{std::this_thread::get_id()};
        bool notified{false};
        bool rejected{false}, unexpected{false};
    };
    auto listener = std::make_shared<observer>();
    job_system_options options;
    options.observer = listener;
    job_system pool{options};
    listener->pool = &pool;
    auto jobs = pool.dispatcher();
    auto source = jobs.run("source", [](job_context&) {});
    auto notification =
        jobs.callbacks().on_complete(source, "notify", GetParam(),
                                     [](job_context& context, const task_handle&, bool& spawn)
                                     {
                                         if (spawn)
                                         {
                                             context.spawn("child", [](job_context&) {});
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
