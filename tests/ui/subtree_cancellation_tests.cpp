/** @file @brief Controlled queued cancellation and rejected admission drain borrowed frontier data.
 */
#include "subtree_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <semaphore>
#include <thread>

namespace atlas::ui::tests
{
struct worker_gate
{
    std::counting_semaphore<2> entered{
        0}; ///< Both workers deliberately occupied before submission.
    std::counting_semaphore<2> release{0}; ///< Controller releases them only after cancellation.
}; ///< Test-only synchronization, not a production worker wait pattern.

/// @brief Cancellation cannot release the plan while an admitted root still holds its descriptor.
TEST(subtree_cancellation, queued_root_joins_before_borrowed_plan_is_released)
{
    worker_gate gate;
    jobs::job_system pool{2};
    const auto block = [](jobs::job_context&, worker_gate*& state)
    {
        state->entered.release();
        if (!state->release.try_acquire_for(std::chrono::seconds{5}))
        {
            throw std::runtime_error("controlled worker release timed out");
        }
    };
    const auto first = pool.dispatcher().run({"controlled worker", "test"}, &gate, block);
    const auto second = pool.dispatcher().run({"controlled worker", "test"}, &gate, block);
    ASSERT_TRUE(gate.entered.try_acquire_for(std::chrono::seconds{5}));
    ASSERT_TRUE(gate.entered.try_acquire_for(std::chrono::seconds{5}));
    element_tree tree;
    const auto root = append_subtree_scenario(tree, {4, 4});
    tree.set_viewport({0, 0, 100, 100});
    layout_engine engine{pool.dispatcher(), {4096, 1, 4, 1}};
    std::stop_source stop;
    const auto submitted = pool.statistics().submitted;
    std::jthread controller{[&]
                            {
                                const auto deadline =
                                    std::chrono::steady_clock::now() + std::chrono::seconds{5};
                                while (pool.statistics().submitted == submitted &&
                                       std::chrono::steady_clock::now() < deadline)
                                {
                                    std::this_thread::yield();
                                }

                                EXPECT_GT(pool.statistics().submitted, submitted);
                                stop.request_stop();
                                gate.release.release(2);
                            }};
    EXPECT_THROW(engine.calculate(tree.snapshot(), {jobs::job_priority::normal, stop.get_token()}),
                 jobs::job_cancelled);
    controller.join();
    first.wait();
    second.wait();
    pool.wait_idle();
    EXPECT_FALSE(tree.computed(root));
    EXPECT_TRUE(engine.statistics().failed);
    EXPECT_EQ(engine.statistics().admitted_jobs, 1U);
    EXPECT_EQ(engine.calculate(tree.snapshot()).items().size(), tree.size());
}

/// @brief A still-live but closed scheduler rejects the root without leaving any plan borrowed.
TEST(subtree_cancellation, closed_pool_rejects_admission_and_preserves_existing_cache)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = append_subtree_scenario(tree, {4, 4});
    layout_engine engine{pool.dispatcher(), {4096, 1, 4, 1}};
    const auto result = engine.update(tree, {0, 0, 100, 100});
    const auto original = tree.snapshot();
    auto style = tree.layout(root);
    style.intrinsic = {100, 100};
    tree.set_layout(root, style);
    pool.shutdown();
    EXPECT_THROW(engine.calculate(tree.snapshot()), std::logic_error);
    EXPECT_EQ(engine.statistics().admitted_jobs, 0U);
    EXPECT_EQ(engine.calculate(original).find(root), result.find(root));
    EXPECT_FALSE(tree.computed(root));
}
} // namespace atlas::ui::tests
