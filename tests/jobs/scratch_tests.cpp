/**
 * @file
 * @brief Behavioral coverage for scratch with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <atomic>
#include <memory_resource>
#include <optional>
#include <vector>

namespace atlas::jobs::tests
{
/// @brief Scenario state for counting resource, isolated per test and released through RAII.
class counting_resource : public std::pmr::memory_resource
{
  public:
    /// @brief Scenario-owned live used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::atomic<std::size_t> live{0};

  private:
    /// @brief Allocate through the test upstream while observing the requested byte budget.
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++live;
        return result;
    }

    /// @brief Release the test allocation and observe cleanup accounting.
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        --live;
    }

    /// @brief Compare memory-resource identity; distinct resources are not interchangeable.
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

/// @brief Verify parent scratch survives children and is released before completion.
TEST(scratch_tests, parent_scratch_survives_children_and_is_released_before_completion)
{
    counting_resource upstream;
    job_system_options options;
    options.worker_count = 1;
    options.scratch = {256, 1024, &upstream};
    job_system system{options};

    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned values used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::optional<std::pmr::vector<int>> values;
        /// @brief Scenario-owned result used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<std::atomic<int>> result;
    };

    auto result = std::make_shared<std::atomic<int>>(0);
    auto root =
        system.dispatcher().run("owner", input{{}, result},
                                [](job_context& context, input& data)
                                {
                                    data.values.emplace(&context.scratch());
                                    data.values->assign(16, 21);
                                    /// @brief Scenario state for borrowed, isolated per test and
                                    /// released through RAII.
                                    struct borrowed
                                    {
                                        /// @brief Scenario-owned values used to observe behavior
                                        /// and cleanup without sharing mutable state between tests.
                                        std::span<int> values;
                                        /// @brief Scenario-owned result used to observe behavior
                                        /// and cleanup without sharing mutable state between tests.
                                        std::shared_ptr<std::atomic<int>> result;
                                    };
                                    context.spawn("borrower", borrowed{*data.values, data.result},
                                                  [](job_context&, borrowed& child)
                                                  {
                                                      child.result->store(child.values.front() * 2);
                                                  });
                                });
    root.wait();
    EXPECT_EQ(result->load(), 42);
    EXPECT_EQ(upstream.live.load(), 0U);
    EXPECT_GE(root.scratch_usage().allocation_calls, 1U);
    EXPECT_EQ(root.scratch_usage().block_count, 1U);
}

/// @brief Verify budget failure releases previous blocks and reports the original failure.
TEST(scratch_tests, budget_failure_releases_previous_blocks_and_reports_the_original_failure)
{
    counting_resource upstream;
    job_system_options options;
    options.worker_count = 1;
    options.scratch = {64, 64, &upstream};
    job_system system{options};
    auto root = system.dispatcher().run("budget",
                                        [](job_context& context)
                                        {
                                            auto& resource = context.scratch();
                                            const auto release = [&resource](void* buffer)
                                            {
                                                resource.deallocate(buffer, 64, 64);
                                            };
                                            const std::unique_ptr<void, decltype(release)> first{
                                                resource.allocate(64, 64), release};
                                            resource.deallocate(resource.allocate(1), 1);
                                        });
    EXPECT_THROW(root.wait(), job_error);
    EXPECT_THROW(std::rethrow_exception(root.exception()), std::bad_alloc);
    EXPECT_EQ(upstream.live.load(), 0U);
    EXPECT_EQ(root.scratch_usage().allocation_calls, 1U);
    EXPECT_EQ(root.scratch_usage().requested_bytes, 64U);
    EXPECT_EQ(root.scratch_usage().reserved_bytes, 64U);
}

/// @brief Verify allocations are aligned and small allocations reuse a block.
TEST(scratch_tests, allocations_are_aligned_and_small_allocations_reuse_a_block)
{
    job_system system{1};
    auto root =
        system.dispatcher().run("aligned",
                                [](job_context& context)
                                {
                                    auto& resource = context.scratch();
                                    auto* first = resource.allocate(1, 64);
                                    auto* second = resource.allocate(8, 64);
                                    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(first) % 64, 0U);
                                    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(second) % 64, 0U);
                                    EXPECT_NE(first, second);
                                });
    root.wait();
    EXPECT_EQ(root.scratch_usage().allocation_calls, 2U);
    EXPECT_EQ(root.scratch_usage().block_count, 1U);
}
} // namespace atlas::jobs::tests
