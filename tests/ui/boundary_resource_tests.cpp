/** @file @brief Dependency preparation and retained input clean up every observed PMR failure. */
#include "boundary_test_support.hpp"
#include "resource_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Graph/order/output failures preserve the previously committed numeric cache.
TEST_F(boundary_test, allocation_failures_preserve_cache_and_can_retry)
{
    const auto root = canvas();
    const auto target = tree.create(root);
    const auto source = tree.create(root);
    height(source, 20);
    tree.set_viewport({0, 0, 100, 100});
    const auto original = tree.snapshot();
    tree.set_boundaries(target, after(source));
    const auto changed = tree.snapshot();
    auto counter = std::make_shared<failing_resource>();
    std::size_t allocations = 0;
    {
        layout_engine probe{pool.dispatcher(), {65536, 1, 1}, counter};
        probe.calculate(original);
        const auto before = counter->calls;
        probe.calculate(changed);
        allocations = counter->calls - before;
    }

    ASSERT_GT(allocations, 0U);
    for (std::size_t failure = 0; failure < allocations; ++failure)
    {
        SCOPED_TRACE(failure);
        auto memory = std::make_shared<failing_resource>();
        layout_engine probe{pool.dispatcher(), {65536, 1, 1}, memory};
        probe.calculate(original);
        const auto baseline = memory->live_bytes;
        memory->remaining = failure;
        EXPECT_THROW(probe.calculate(changed), std::bad_alloc);
        EXPECT_EQ(memory->live_bytes, baseline);
        EXPECT_EQ(require_geometry(probe.calculate(original).find(target)).bounds.y, 0.f);
        memory->remaining = std::numeric_limits<std::size_t>::max();
        EXPECT_EQ(require_geometry(probe.calculate(changed).find(target)).bounds.y, 20.f);
    }
}

/// @brief Authoring preflight allocation cannot change metadata or revision on failure.
TEST(boundary_resources, failed_authoring_and_retained_snapshot_release_cleanly)
{
    auto memory = std::make_shared<failing_resource>();
    std::weak_ptr<std::pmr::memory_resource> lifetime = memory;
    element_snapshot retained;
    {
        element_tree tree{{}, memory};
        const auto root = tree.create();
        tree.set_panel(root, canvas_layout{});
        const auto source = tree.create(root);
        const auto target = tree.create(root);
        const auto revision = tree.snapshot().revision();
        const layout_boundaries value{{}, boundary_relation{source}};
        memory->remaining = 0;
        EXPECT_THROW(tree.set_boundaries(target, value), std::bad_alloc);
        EXPECT_EQ(tree.boundaries(target), layout_boundaries{});
        memory->remaining = std::numeric_limits<std::size_t>::max();
        EXPECT_EQ(tree.snapshot().revision(), revision);
        tree.set_boundaries(target, value);
        retained = tree.snapshot();
        tree.reset_boundaries(target);
        tree.remove(root);
        memory.reset();
    }

    ASSERT_FALSE(lifetime.expired());
    ASSERT_EQ(retained.boundaries().size(), 1U);
    EXPECT_TRUE(retained.boundaries()[0].boundaries.vertical);
    retained = {};
    EXPECT_TRUE(lifetime.expired());
}
} // namespace atlas::ui::tests
