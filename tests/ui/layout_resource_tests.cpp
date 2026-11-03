/** @file @brief Allocation failure, cancellation acknowledgment and retained layout ownership. */
#include "layout_test_support.hpp"
#include "resource_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <semaphore>
#include <thread>

namespace atlas::ui::tests
{
/// @brief Deterministically fail every PMR preparation stage without worker races.
TEST(layout_resources, allocation_failures_preserve_cache_and_allow_retry)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    tree.create(root);
    tree.set_viewport({0, 0, 100, 50});
    const auto input = tree.snapshot();
    auto baseline = std::make_shared<failing_resource>();
    std::size_t allocations = 0;
    {
        layout_engine engine{pool.dispatcher(), {}, baseline};
        const auto before = baseline->calls;
        engine.calculate(input);
        allocations = baseline->calls - before;
    }
    EXPECT_EQ(baseline->live_bytes, 0U);
    for (std::size_t failure = 0; failure < allocations; ++failure)
    {
        SCOPED_TRACE(failure);
        auto memory = std::make_shared<failing_resource>();
        layout_engine engine{pool.dispatcher(), {}, memory};
        const auto retained = memory->live_bytes;
        memory->remaining = failure;
        EXPECT_THROW(engine.calculate(input), std::bad_alloc);
        EXPECT_EQ(memory->live_bytes, retained);
        memory->remaining = std::numeric_limits<std::size_t>::max();
        EXPECT_EQ(engine.calculate(input).items().size(), 2U);
    }
}

/// @brief A retained result keeps only immutable geometry/resource ownership, not the live tree.
TEST(layout_resources, result_outlives_tree_and_engine)
{
    jobs::job_system pool{1};
    auto memory = std::make_shared<failing_resource>();
    std::weak_ptr<std::pmr::memory_resource> lifetime = memory;
    layout_snapshot retained;
    element_handle target;
    {
        element_tree tree;
        target = tree.create();
        layout_engine engine{pool.dispatcher(), {}, memory};
        retained = engine.update(tree, {0, 0, 100, 50});
        memory.reset();
    }
    ASSERT_FALSE(lifetime.expired());
    EXPECT_EQ(require_geometry(retained.find(target)).bounds.width, 100.f);
    EXPECT_FALSE(retained.find({target.index, target.generation + 1, target.session}));
    retained = {};
    EXPECT_TRUE(lifetime.expired());
}

/// @brief Gate only a worker allocation; coordinator metadata allocations remain nonblocking.
class gated_layout_resource final : public std::pmr::memory_resource
{
  public:
    std::atomic<bool> armed{false};   ///< Armed only after engine construction.
    std::atomic<std::size_t> live{0}; ///< Requested bytes retained across both threads.
    std::binary_semaphore entered{0}; ///< Worker is inside a controlled allocation.
    std::binary_semaphore release{0}; ///< Permit native allocation to finish after requesting stop.
  private:
    const std::thread::id owner_ = std::this_thread::get_id(); ///< Metadata coordinator.

    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        if (std::this_thread::get_id() != owner_ && armed.exchange(false))
        {
            entered.release();
            if (!release.try_acquire_for(std::chrono::seconds{5}))
            {
                throw std::runtime_error("controlled worker allocation timed out");
            }
        }

        auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        live.fetch_add(bytes);
        return result;
    } ///< Successful allocations are independently owned until their matching deallocation.

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        live.fetch_sub(bytes);
    } ///< Cleanup stays available while cancellation is draining.

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    } ///< Stable resource identity across all accepted jobs.
};

/// @brief Actual worker cancellation joins before the coordinator releases temporary buffers.
TEST(layout_resources, cancellation_joins_before_releasing_worker_buffers)
{
    jobs::job_system pool{2};
    element_tree tree;
    tree.create();
    tree.create();
    tree.set_viewport({0, 0, 100, 50});
    const auto input = tree.snapshot();
    auto memory = std::make_shared<gated_layout_resource>();
    layout_engine engine{pool.dispatcher(), {65536, 1, 2}, memory};
    const auto retained = memory->live.load();
    std::stop_source stop;
    std::jthread controller{
        [&]
        {
            EXPECT_TRUE(memory->entered.try_acquire_for(std::chrono::seconds{5}));
            stop.request_stop();
            memory->release.release();
        }};
    memory->armed = true;
    EXPECT_THROW(engine.calculate(input, {jobs::job_priority::normal, stop.get_token()}),
                 jobs::job_cancelled);
    controller.join();
    EXPECT_EQ(memory->live.load(), retained);
    EXPECT_TRUE(engine.statistics().failed);
    EXPECT_EQ(engine.calculate(input).items().size(), 2U);
}
} // namespace atlas::ui::tests
