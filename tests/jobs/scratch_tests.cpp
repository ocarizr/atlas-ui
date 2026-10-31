#include "job_system_fixture.hpp"

#include <atomic>
#include <memory_resource>
#include <optional>
#include <vector>

namespace atlas::jobs::tests
{
class counting_resource : public std::pmr::memory_resource
{
  public:
    std::atomic<std::size_t> live{0};

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++live;
        return result;
    }
    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        --live;
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    }
};

TEST(scratch_tests, parent_scratch_survives_children_and_is_released_before_completion)
{
    counting_resource upstream;
    job_system_options options;
    options.worker_count = 1;
    options.scratch = {256, 1024, &upstream};
    job_system system{options};
    struct input
    {
        std::optional<std::pmr::vector<int>> values;
        std::shared_ptr<std::atomic<int>> result;
    };
    auto result = std::make_shared<std::atomic<int>>(0);
    auto root = system.dispatcher().run("owner", input{{}, result},
                                        [](job_context& context, input& data)
                                        {
                                            data.values.emplace(&context.scratch());
                                            data.values->assign(16, 21);
                                            struct borrowed
                                            {
                                                std::span<int> values;
                                                std::shared_ptr<std::atomic<int>> result;
                                            };
                                            context.spawn(
                                                "borrower", borrowed{*data.values, data.result},
                                                [](job_context&, borrowed& child)
                                                { child.result->store(child.values.front() * 2); });
                                        });
    root.wait();
    EXPECT_EQ(result->load(), 42);
    EXPECT_EQ(upstream.live.load(), 0U);
    EXPECT_GE(root.scratch_usage().allocation_calls, 1U);
    EXPECT_EQ(root.scratch_usage().block_count, 1U);
}

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
                                            { resource.deallocate(buffer, 64, 64); };
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
