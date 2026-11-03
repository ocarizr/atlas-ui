/** @file @brief Failed sealing preserves pending work and published packets retain allocator life.
 */
#include "resource_test_support.hpp"
#include <atlas/ui/commands.hpp>
#include <atlas/ui/element_tree.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <optional>

namespace atlas::ui::tests
{
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

/// @brief Every injectable constructor failure releases PMR storage without a partial session.
TEST(tree_resources, construction_rolls_back_each_pmr_allocation_failure)
{
    auto baseline = std::make_shared<failing_resource>();
    std::size_t allocations = 0;
    {
        element_tree tree{{4}, baseline};
        allocations = baseline->calls;
    }
    EXPECT_EQ(baseline->live_bytes, 0U);
    for (std::size_t failure = 0; failure < allocations; ++failure)
    {
        SCOPED_TRACE(failure);
        auto resource = std::make_shared<failing_resource>();
        resource->remaining = failure;
        EXPECT_THROW((element_tree{{4}, resource}), std::bad_alloc);
        EXPECT_EQ(resource->live_bytes, 0U);
    }
}

/// @brief Prepared publication and structural removal use already reserved PMR bookkeeping.
TEST(tree_resources, apply_and_removal_need_no_further_pmr_allocation)
{
    auto memory = std::make_shared<failing_resource>();
    element_tree tree{{4}, memory};
    const auto root = tree.create();
    const auto child = tree.create(root);
    tree.commands().begin_frame(1);
    tree.commands().set(child, property::width, 12.f, 1);
    const auto batch = tree.commands().seal();
    const auto calls = memory->calls;
    memory->remaining = 0;
    tree.apply(batch);
    tree.reparent(child);
    tree.remove(root);
    EXPECT_EQ(memory->calls, calls);
    EXPECT_EQ(tree.requested(child).width, 12.f);
}

/// @brief Batches keep allocator/session identity after the tree is destroyed.
TEST(tree_resources, retained_batch_outlives_tree_and_rejects_a_new_session)
{
    auto memory = std::make_shared<failing_resource>();
    std::weak_ptr<std::pmr::memory_resource> lifetime = memory;
    std::optional<command_batch> retained;
    std::uint64_t session = 0;
    {
        element_tree tree{{4}, memory};
        const auto root = tree.create();
        session = root.session;
        tree.commands().begin_frame(1);
        tree.commands().set(root, property::width, 12.f, 1);
        retained = tree.commands().seal();
        memory.reset();
    }
    ASSERT_FALSE(lifetime.expired());
    EXPECT_EQ(retained->session(), session);
    element_tree replacement;
    EXPECT_THROW(replacement.apply(*retained), std::invalid_argument);
    retained.reset();
    EXPECT_TRUE(lifetime.expired());
}

} // namespace atlas::ui::tests
