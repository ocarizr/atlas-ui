/** @file @brief Joined grid execution, allocation rollback and nested scroll presentation. */
#include "resource_test_support.hpp"
#include "scroll_test_support.hpp"
#include <limits>

namespace atlas::ui::tests
{
/// @brief Independent grids and nested subtrees produce identical serial and jobified geometry.
TEST_F(paint_fixture, grid_parallel_forest_matches_sequential_geometry)
{
    for (unsigned root_index = 0; root_index < 4; ++root_index)
    {
        const auto root = tree.create();
        grid_definition policy;
        policy.columns.resize(4, {grid_unit::fraction, 1});
        tree.set_panel(root, grid_layout{std::move(policy)});
        for (unsigned index = 0; index < 20; ++index)
        {
            const auto panel = rectangle(root);
            tree.set_panel(panel, grid_layout{});
            const auto leaf = rectangle(panel);
            auto style = tree.layout(leaf);
            style.intrinsic = {static_cast<float>(10 + index), 12};
            tree.set_layout(leaf, style);
        }
    }

    tree.set_viewport({0, 0, 100, 80});
    const auto source = tree.snapshot();
    layout_engine parallel{workers.dispatcher(),
                           {.min_parallel_nodes = 1, .max_chunks = 4, .subtree_grain = 1}};
    const auto reference = layout.calculate(source);
    const auto actual = parallel.calculate(source);
    EXPECT_EQ(std::vector(reference.items().begin(), reference.items().end()),
              std::vector(actual.items().begin(), actual.items().end()));
    EXPECT_GT(parallel.statistics().admitted_jobs, 0);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW((void)parallel.calculate(source, {jobs::job_priority::normal, stop.get_token()}),
                 jobs::job_cancelled);
}

/// @brief Every observed PMR grid calculation allocation failure preserves previous cached output.
TEST_F(paint_fixture, grid_allocation_failures_preserve_outputs_and_allow_retry)
{
    const auto root = tree.create();
    tree.set_panel(root, grid_layout{});
    for (unsigned index = 0; index < 8; ++index)
    {
        const auto child = rectangle(root);
        auto style = tree.layout(child);
        style.intrinsic = {20, 10};
        tree.set_layout(child, style);
    }

    tree.set_viewport({0, 0, 100, 80});
    const auto source = tree.snapshot();
    auto probe = std::make_shared<failing_resource>();
    std::size_t allocations = 0;
    {
        layout_engine engine{workers.dispatcher(), {}, probe};
        const auto before = probe->calls;
        (void)engine.calculate(source);
        allocations = probe->calls - before;
    }
    ASSERT_GT(allocations, 0);
    for (std::size_t failure = 0; failure < allocations; ++failure)
    {
        SCOPED_TRACE(failure);
        layout_engine engine{workers.dispatcher(), {}, probe};
        const auto bytes = probe->live_bytes;
        probe->remaining = failure;
        EXPECT_THROW((void)engine.calculate(source), std::bad_alloc);
        probe->remaining = std::numeric_limits<std::size_t>::max();
        EXPECT_EQ(probe->live_bytes, bytes);
        EXPECT_EQ(engine.calculate(source).items().size(), 9);
    }
}

/// @brief Scroll extent, mask, painter order and hit testing consume ordinary grid geometry.
TEST_F(scroll_fixture, grid_content_scrolls_and_clips_like_retained_stack_content)
{
    tree.set_panel(content, grid_layout{});
    tree.set_paint(content, {});
    resize_content({});
    std::vector<element_handle> children;
    for (unsigned index = 0; index < 6; ++index)
    {
        const auto child = rectangle(content);
        children.push_back(child);
        auto style = tree.layout(child);
        style.intrinsic = {100, 20};
        tree.set_layout(child, style);
        input.bind(child, handler, {true, true, true});
    }

    offset({0, 40});
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    EXPECT_EQ(scrolling(geometry).range.y, 40);
    const auto painted = paint.calculate(source, geometry);
    ASSERT_EQ(painted.quads().size(), 4);
    EXPECT_EQ(painted.quads().front().target, children[2]);
    EXPECT_EQ(painted.quads().front().clip, (layout_rect{0, 0, 100, 80}));
    input.begin_frame(1, source, geometry);
    EXPECT_EQ(input.hit_test({10, 10}), children[2]);
    input.process({});
    finish();
}
} // namespace atlas::ui::tests
