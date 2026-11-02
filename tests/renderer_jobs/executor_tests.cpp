/** @file @brief Joined task success, failure and scheduler lifetime contracts. */
#include <atlas/jobs/job_system.hpp>
#include <atlas/renderer_jobs/executor.hpp>
#include <future>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::renderer_jobs::tests
{
/// @brief Controlled overlap proves two worker bodies are active without arbitrary sleeps.
struct overlap_gate
{
    std::atomic<unsigned> arrived{0}; ///< Publish simultaneous entry.
    std::promise<void> release;       ///< Fulfilled by the second participant.
    std::shared_future<void> ready = release.get_future().share(); ///< Diagnostic bounded wait.

    void enter()
    {
        if (arrived.fetch_add(1) == 1)
        {
            release.set_value();
        }

        if (ready.wait_for(std::chrono::seconds{5}) != std::future_status::ready)
        {
            throw std::runtime_error("recording overlap timeout");
        }
    } ///< Join both participants.
};

/// @brief Isolated task publishes its actual worker and optional original failure.
class test_task final : public renderer::recording_task
{
  public:
    bool fail = false;                  ///< Failure injection before successful publication.
    std::thread::id thread;             ///< Written only by the task and read after join.
    std::shared_ptr<overlap_gate> gate; ///< Optional controlled overlap.
  private:
    void record() override
    {
        thread = std::this_thread::get_id();
        if (gate)
        {
            gate->enter();
        }

        if (fail)
        {
            throw std::runtime_error("recording failed");
        }
    } ///< Simulate independent recording.
};

/// @brief Request cancellation from a completed recording body to test the post-body checkpoint.
class cancelling_task final : public renderer::recording_task
{
  public:
    std::stop_source stop; ///< Token shared with the injected dispatcher.
  private:
    void record() override
    {
        stop.request_stop();
    } ///< Complete native-style recording, then request cancellation.
};

/// @brief The injected pool executes tasks without coordinator-thread recording.
TEST(recording_executor, joins_existing_workers_and_enforces_exactly_once)
{
    jobs::job_system pool{2};
    executor adapter{pool.dispatcher()};
    test_task first, second;
    first.gate = std::make_shared<overlap_gate>();
    second.gate = first.gate;
    const std::array<renderer::recording_task*, 2> tasks{&first, &second};
    adapter.execute(tasks);
    EXPECT_NO_THROW(first.verify());
    EXPECT_NO_THROW(second.verify());
    EXPECT_NE(first.thread, std::this_thread::get_id());
    EXPECT_NE(second.thread, std::this_thread::get_id());
    EXPECT_EQ(pool.statistics().workers, 2U);
    EXPECT_THROW(first.run(), std::logic_error);
}

/// @brief Failures do not release borrowed task memory before sibling tasks terminate.
TEST(recording_executor, joins_siblings_before_rethrowing_failure)
{
    jobs::job_system pool{2};
    executor adapter{pool.dispatcher()};
    test_task first, second;
    first.fail = true;
    const std::array<renderer::recording_task*, 2> tasks{&first, &second};
    EXPECT_THROW(adapter.execute(tasks), jobs::job_error);
    EXPECT_THROW(first.verify(), std::runtime_error);
    EXPECT_NO_THROW(second.verify());
}

/// @brief A stopped pool accepts no recording work and leaves tasks untouched.
TEST(recording_executor, rejects_stopped_pool_and_null_tasks)
{
    jobs::job_system pool{1};
    executor adapter{pool.dispatcher()};
    const std::array<renderer::recording_task*, 1> invalid{nullptr};
    EXPECT_THROW(adapter.execute(invalid), std::invalid_argument);
    test_task task;
    EXPECT_THROW(task.verify(), std::logic_error);
    pool.shutdown();
    const std::array<renderer::recording_task*, 1> tasks{&task};
    EXPECT_THROW(adapter.execute(tasks), std::logic_error);
    EXPECT_EQ(task.thread, std::thread::id{});
}

/// @brief Worker callers must fail before admitting borrowed work whose join would be forbidden.
TEST(recording_executor, rejects_worker_before_admitting_recording)
{
    jobs::job_system pool{1};
    std::stop_source stop;
    stop.request_stop();
    executor adapter{pool.dispatcher().with_options({.cancellation = stop.get_token()})};
    test_task task;

    /// @brief Explicit borrowed inputs outlive the outer coordinator wait.
    struct input
    {
        executor* adapter;              ///< Adapter outlives the enclosing wait.
        renderer::recording_task* task; ///< Borrowed input kept alive through root completion.
        jobs::job_system* pool;         ///< Observe admission counts without blocking the worker.
    };

    auto outer =
        pool.dispatcher().run("worker misuse", input{&adapter, &task, &pool},
                              [](jobs::job_context&, input& data)
                              {
                                  const std::array<renderer::recording_task*, 1> tasks{data.task};
                                  const auto before = data.pool->statistics().submitted;
                                  EXPECT_THROW(data.adapter->execute(tasks), std::logic_error);
                                  EXPECT_EQ(data.pool->statistics().submitted, before);
                              });
    outer.wait();
    pool.wait_idle();
    EXPECT_EQ(task.thread, std::thread::id{});
}

/// @brief A completed native recording does not bypass a cancellation observed before joining.
TEST(recording_executor, observes_cancellation_after_recording_body)
{
    jobs::job_system pool{1};
    cancelling_task task;
    executor adapter{pool.dispatcher().with_options({.cancellation = task.stop.get_token()})};
    const std::array<renderer::recording_task*, 1> tasks{&task};
    EXPECT_THROW(adapter.execute(tasks), jobs::job_error);
    EXPECT_NO_THROW(task.verify());
}
} // namespace atlas::renderer_jobs::tests
