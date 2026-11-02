/** @file @brief Failed sealing preserves pending work and published packets retain allocator life.
 */
#include <atlas/ui/commands.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <optional>

namespace atlas::ui::tests
{
/// @brief Deterministic allocation failure injection, used only by one test owner.
class failing_resource final : public std::pmr::memory_resource
{
  public:
    std::size_t remaining =
        std::numeric_limits<std::size_t>::max(); ///< Fail after this many calls.
    std::size_t calls = 0;      ///< Count allocator activity, including debug iterator metadata.
    std::size_t live_bytes = 0; ///< Detect leaked partial preparation after an injected failure.
  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        ++calls;
        if (remaining == 0)
        {
            throw std::bad_alloc{};
        }

        --remaining;
        auto* data = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        live_bytes += bytes;
        return data;
    } ///< Forward successful allocations without borrowing external storage.

    void do_deallocate(void* data, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(data, bytes, alignment);
        live_bytes -= bytes;
    } ///< Cleanup remains available after an injected failure.

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    } ///< Resource identity is stable until all owned packets are released.
};

/// @brief Failed preparation cannot advance the batch or forget writes/conflicts on retry.
TEST(command_resources, sealing_can_be_retried_after_every_allocation_failure)
{
    const auto prepare = [](command_buffer& buffer)
    {
        buffer.begin_frame(1);
        buffer.set({0, 1}, property::width, 1.f, 1);
        buffer.set({0, 1}, property::width, 2.f, 2);
    };
    auto baseline_resource = std::make_shared<failing_resource>();
    command_buffer baseline{{8, 2, 0}, baseline_resource};
    prepare(baseline);
    const auto before = baseline_resource->calls;
    const auto reference = baseline.seal();
    const auto allocations = baseline_resource->calls - before;
    ASSERT_GE(allocations, 3U);
    for (std::size_t failure = 0; failure < allocations; ++failure)
    {
        SCOPED_TRACE(failure);
        auto resource = std::make_shared<failing_resource>();
        command_buffer buffer{{8, 2, 0}, resource};
        prepare(buffer);
        const auto retained_bytes = resource->live_bytes;
        resource->remaining = failure;
        EXPECT_THROW(buffer.seal(), std::bad_alloc);
        EXPECT_EQ(resource->live_bytes, retained_bytes);
        EXPECT_EQ(buffer.pending(), 2U);
        resource->remaining = std::numeric_limits<std::size_t>::max();
        const auto batch = buffer.seal();
        EXPECT_EQ(batch.ordinal(), 1U);
        ASSERT_EQ(batch.writes().size(), 1U);
        EXPECT_EQ(batch.writes()[0].value, reference.writes()[0].value);
        EXPECT_EQ(batch.conflicts().size(), 1U);
    }
}

/// @brief Iterator metadata failure during construction is recoverable on debug toolchains too.
TEST(command_resources, construction_propagates_allocation_failure)
{
    auto resource = std::make_shared<failing_resource>();
    resource->remaining = 0;
    EXPECT_THROW((command_buffer{{8, 2, 0}, resource}), std::bad_alloc);
    EXPECT_EQ(resource->live_bytes, 0U);
}

/// @brief A moved packet has safe empty queries; the destination owns all published data.
TEST(command_resources, moved_batches_remain_safely_inspectable)
{
    command_buffer buffer;
    buffer.begin_frame(1);
    auto source = buffer.seal();
    auto destination = std::move(source);
    // Exercise the documented moved-from query contract deliberately.
    // NOLINTBEGIN(clang-analyzer-cplusplus.Move,bugprone-use-after-move)
    EXPECT_EQ(source.frame(), 0U);
    EXPECT_EQ(source.ordinal(), 0U);
    EXPECT_TRUE(source.writes().empty());
    EXPECT_TRUE(source.conflicts().empty());
    // NOLINTEND(clang-analyzer-cplusplus.Move,bugprone-use-after-move)
    EXPECT_EQ(destination.frame(), 1U);
}

/// @brief The allocator remains alive until both producer and retained immutable data release it.
TEST(command_resources, batch_outlives_producer_and_allocator_owner)
{
    std::optional<command_batch> retained;
    std::weak_ptr<std::pmr::memory_resource> lifetime;
    {
        auto resource = std::make_shared<failing_resource>();
        lifetime = resource;
        command_buffer buffer{{8, 2, 0}, resource};
        buffer.begin_frame(1);
        buffer.set({0, 1}, property::width, 1.f, 1);
        retained = buffer.seal();
        resource.reset();
    }
    EXPECT_FALSE(lifetime.expired());
    ASSERT_EQ(retained->writes().size(), 1U);
    EXPECT_EQ(retained->writes()[0].value, property_value{1.f});
    retained.reset();
    EXPECT_TRUE(lifetime.expired());
}

/// @brief Rolled-back commands neither produce diagnostics nor replace prior sealed provenance.
TEST(command_resources, rollback_retains_only_published_conflict_history)
{
    command_buffer buffer;
    buffer.begin_frame(1);
    buffer.set({0, 1}, property::width, 1.f, 1);
    const auto first = buffer.seal();
    buffer.set({0, 1}, property::width, 2.f, 2);
    buffer.discard_pending();
    buffer.set({0, 1}, property::width, 3.f, 3);
    const auto second = buffer.seal();
    ASSERT_EQ(second.conflicts().size(), 1U);
    EXPECT_EQ(second.conflicts()[0].previous.producer, 1U);
    EXPECT_EQ(second.conflicts()[0].writes_this_frame, 2U);
    EXPECT_EQ(first.writes()[0].value, property_value{1.f});
}
} // namespace atlas::ui::tests
