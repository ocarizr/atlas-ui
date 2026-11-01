/** @file @brief Common allocation contracts across configured system and mimalloc backends. */
#include <atlas/memory/memory_resource.hpp>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <memory_resource>
#include <vector>

namespace atlas::memory::tests
{
/// @brief Observe ownership/accounting with a fresh resource per test.
class resource_test : public ::testing::Test
{
  protected:
    /// @brief Enabled counters make cleanup observable without assuming allocator internals.
    std::shared_ptr<memory_resource> resource{make_resource({true})};
};

/// @brief Successful PMR allocations and releases preserve exact requested-byte accounting.
TEST_F(resource_test, accounts_for_owned_container_lifetimes)
{
    {
        std::pmr::vector<std::uint8_t> bytes{resource.get()};
        bytes.resize(1024);
        EXPECT_GE(resource->statistics().live_requested_bytes, 1024);
    }
    const auto report = resource->statistics();
    EXPECT_EQ(report.live_requested_bytes, 0);
    EXPECT_EQ(report.allocation_calls, report.deallocation_calls);
    EXPECT_GT(report.peak_requested_bytes, 0);
}

/// @brief Disabled instrumentation avoids updates to shared counters on the hot path.
TEST(resource_policy_tests, counters_are_opt_in)
{
    const auto resource = make_resource();
    auto* pointer = resource->allocate(64, 64);
    resource->deallocate(pointer, 64, 64);
    EXPECT_EQ(resource->statistics().allocation_calls, 0);
    EXPECT_EQ(resource->statistics().live_requested_bytes, 0);
    EXPECT_EQ(resource->backend_name(), ATLAS_EXPECTED_MEMORY_BACKEND);
}

/// @brief Different accounting resources cannot be used interchangeably for PMR deallocation.
TEST_F(resource_test, equality_preserves_resource_identity)
{
    const auto other = make_resource({true});
    EXPECT_TRUE(resource->is_equal(*resource));
    EXPECT_FALSE(resource->is_equal(*other));
}

/// @brief Reject unrepresentable requests before entering a vendor allocator or changing counters.
TEST_F(resource_test, rejects_unrepresentable_size_without_accounting_side_effects)
{
    EXPECT_THROW(
        {
            auto* pointer = resource->allocate(std::numeric_limits<std::size_t>::max(), 64);
            resource->deallocate(pointer, std::numeric_limits<std::size_t>::max(), 64);
        },
        std::bad_alloc);
    EXPECT_EQ(resource->statistics().allocation_calls, 0);
}

/// @brief Size/alignment combinations include zero bytes and over-aligned payloads.
class aligned_resource_test
    : public resource_test,
      public ::testing::WithParamInterface<std::tuple<std::size_t, std::size_t>>
{
};

/// @brief Allocations satisfy the caller's alignment and can be released with their original size.
TEST_P(aligned_resource_test, preserves_alignment_and_zero_size_cleanup)
{
    const auto& parameters = GetParam();
    const auto size = std::get<0>(parameters);
    const auto alignment = std::get<1>(parameters);
    const auto release = [owner = resource, size, alignment](void* pointer)
    {
        owner->deallocate(pointer, size, alignment);
    };
    std::unique_ptr<void, decltype(release)> allocation{resource->allocate(size, alignment),
                                                        release};
    auto* pointer = allocation.get();
    ASSERT_NE(pointer, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pointer) % alignment, 0);
    allocation.reset();
    EXPECT_EQ(resource->statistics().live_requested_bytes, 0);
    EXPECT_EQ(resource->statistics().allocation_calls, 1);
    EXPECT_EQ(resource->statistics().deallocation_calls, 1);
}

INSTANTIATE_TEST_SUITE_P(
    boundaries, aligned_resource_test,
    ::testing::Combine(::testing::Values(std::size_t{0}, std::size_t{1}, std::size_t{65536}),
                       ::testing::Values(std::size_t{8}, std::size_t{64}, std::size_t{4096})));
} // namespace atlas::memory::tests
