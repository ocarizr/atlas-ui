/** @file @brief Bounded realization, logical geometry and physical generation reuse. */
#include "virtual_list_support.hpp"

namespace atlas::ui::tests
{
/// @brief Ten million logical rows produce six initial physical rows with two-item overscan.
TEST_F(virtual_list_fixture, logical_count_does_not_determine_live_tree_size)
{
    update(10000000);
    EXPECT_EQ(list->rows().size(), 6);
    EXPECT_EQ(tree.size(), 8);
    EXPECT_EQ(list->statistics().admitted_jobs, 0);
    const auto geometry = layout.calculate(tree.snapshot());
    ASSERT_TRUE(geometry.find_scroll(viewport));
    EXPECT_FLOAT_EQ(geometry.find_scroll(viewport).value_or(scroll_geometry{}).range.y, 199999920);
}

/// @brief Logical geometry agrees with the retained sequential reference at each visible index.
TEST_F(virtual_list_fixture, fixed_rows_follow_half_open_visible_range_and_bounded_overscan)
{
    update();
    scroll(40);
    ASSERT_EQ(list->rows().size(), 8);
    EXPECT_EQ(list->rows().front().index, 0);
    EXPECT_EQ(list->rows().back().index, 7);
    const auto result = layout.calculate(tree.snapshot());
    for (const auto& row : list->rows())
    {
        const auto geometry = require_geometry(result.find(row.ticket.root));
        EXPECT_FLOAT_EQ(geometry.bounds.y, static_cast<float>(row.index) * 20);
        EXPECT_FLOAT_EQ(geometry.bounds.height, 20);
    }

    scroll(1920);
    EXPECT_EQ(list->rows().back().index, 99);
    EXPECT_EQ(list->rows().size(), 6);
}

/// @brief Stable keys retain rows; physical slots rebound to other keys invalidate tickets.
TEST_F(virtual_list_fixture, recycling_invalidates_late_completions_and_reuses_capacity)
{
    update();
    const auto first = list->rows().front().ticket;
    const auto retained = list->rows().back().ticket;
    scroll(20);
    EXPECT_TRUE(list->accepts(first));
    EXPECT_TRUE(list->accepts(retained));
    EXPECT_EQ(list->statistics().created, 1);
    scroll(1000);
    EXPECT_FALSE(list->accepts(first));
    EXPECT_FALSE(list->accepts(retained));
    EXPECT_LE(tree.size(), 10);
    scroll(0);
    EXPECT_FALSE(list->accepts(first));
    EXPECT_NE(list->rows().front().ticket.generation, first.generation);
}

/// @brief Short and empty sources clamp both ends without dummy physical items.
TEST_F(virtual_list_fixture, shrink_empty_and_resize_release_offscreen_rows)
{
    update();
    scroll(1000);
    update(1);
    EXPECT_FLOAT_EQ(position(), 0);
    EXPECT_EQ(tree.size(), 3);
    update(0);
    EXPECT_TRUE(list->rows().empty());
    EXPECT_EQ(tree.size(), 2);
    list->detach();
    EXPECT_EQ(tree.size(), 1);
}

/// @brief The end of a huge source remains contiguous after float coordinate quantization.
TEST_F(virtual_list_fixture, far_end_reveal_has_no_blank_gap_or_bottom_overscroll)
{
    update(10000000);
    list->request_focus(10000000);
    list->update(data, 80);
    const auto snapshot = tree.snapshot();
    const auto result = paint.calculate(snapshot, layout.calculate(snapshot));
    ASSERT_FALSE(result.quads().empty());
    float bottom = 0;
    for (const auto& quad : result.quads())
    {
        EXPECT_EQ(quad.bounds.y, bottom);
        bottom = quad.bounds.y + quad.bounds.height;
    }

    EXPECT_EQ(bottom, 80);
}
} // namespace atlas::ui::tests
