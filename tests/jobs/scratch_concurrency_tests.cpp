/**
 * @file
 * @brief Behavioral coverage for scratch concurrency with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"

#include <atomic>
#include <barrier>

namespace atlas::jobs::tests
{
/// @brief Verify descendants can allocate concurrently from a borrowed parent resource.
TEST(scratch_tests, descendants_can_allocate_concurrently_from_a_borrowed_parent_resource)
{
    job_system system{2};

    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned start used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        std::shared_ptr<std::barrier<>> start;
        /// @brief Scenario-owned count used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        std::shared_ptr<std::atomic<int>> count;
    };

    auto count = std::make_shared<std::atomic<int>>(0);
    auto start = std::make_shared<std::barrier<>>(2);
    auto root = system.dispatcher().run(
        "parent", input{start, count},
        [](job_context& context, input& data)
        {
            /// @brief Scenario state for child input, isolated per test and released through RAII.
            struct child_input
            {
                /// @brief Scenario-owned resource used to observe behavior and cleanup without
                /// sharing mutable state between tests.
                std::pmr::memory_resource* resource;
                /// @brief Scenario-owned shared used to observe behavior and cleanup without
                /// sharing mutable state between tests.
                input shared;
            };
            for (int index = 0; index < 2; ++index)
            {
                context.spawn("child", child_input{&context.scratch(), data},
                              [](job_context&, child_input& child)
                              {
                                  child.shared.start->arrive_and_wait();
                                  for (int allocation = 0; allocation < 32; ++allocation)
                                  {
                                      auto* value =
                                          static_cast<int*>(child.resource->allocate(64, 64));
                                      std::construct_at(value, 1);
                                      child.shared.count->fetch_add(*value);
                                  }
                              });
            }
        });
    root.wait();
    EXPECT_EQ(count->load(), 64);
    EXPECT_EQ(root.scratch_usage().allocation_calls, 64U);
    EXPECT_EQ(root.scratch_usage().block_count, 1U);
}

/// @brief Verify invalid scratch configuration is rejected before starting workers.
TEST(scratch_tests, invalid_scratch_configuration_is_rejected_before_starting_workers)
{
    job_system_options options;
    options.scratch.block_size = 0;
    EXPECT_THROW((job_system{options}), std::invalid_argument);
    options.scratch.block_size = 64;
    options.scratch.max_reserved_bytes = 0;
    EXPECT_THROW((job_system{options}), std::invalid_argument);
    options.scratch.max_reserved_bytes = 64;
    options.scratch.upstream = nullptr;
    EXPECT_THROW((job_system{options}), std::invalid_argument);
}
} // namespace atlas::jobs::tests
