/** @file @brief Logical ranges are independent of realized widget storage. */
#include <atlas/ui/virtual_source.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief A huge fixed source locates its final item without allocating per-item metadata.
TEST(virtual_source, fixed_range_uses_half_open_intersections)
{
    const fixed_item_source source{{10000000, 20}};
    EXPECT_EQ(source.count(), 10000000);
    EXPECT_EQ(source.locate(40), 2);
    EXPECT_EQ(source.locate(source.extent()), source.count());
    EXPECT_EQ(source.item(9999999).key, 10000000);
    EXPECT_FLOAT_EQ(source.extent(), 200000000);
}

/// @brief Large float positions share exact adjacent endpoints rather than leaving blank gaps.
TEST(virtual_source, far_fixed_rows_have_contiguous_representable_boundaries)
{
    const fixed_item_source source{{10000000, 20}};
    for (std::size_t i = 9999980; i + 1 < source.count(); ++i)
    {
        const auto item = source.item(i);
        const auto next = source.item(i + 1);
        EXPECT_EQ(item.offset + item.height, next.offset);
        EXPECT_EQ(source.locate(next.offset), i + 1);
    }
}

/// @brief Exact variable heights support stable keys and logarithmic lookup without widgets.
TEST(virtual_source, variable_source_preserves_keys_and_exact_offsets)
{
    const std::array<item_description, 3> items{{{7, 10}, {3, 30}, {9, 20}}};
    const variable_item_source source{items};
    EXPECT_EQ(source.locate(10), 1);
    EXPECT_EQ(source.find(9), 2);
    EXPECT_FALSE(source.find(4));
    EXPECT_FLOAT_EQ(source.item(2).offset, 40);
    EXPECT_FLOAT_EQ(source.extent(), 60);
}

/// @brief Ambiguous identities and invalid extents fail before any realization.
TEST(virtual_source, rejects_duplicate_keys_and_nonpositive_heights)
{
    const std::array<item_description, 2> duplicate{{{7, 10}, {7, 30}}};
    const std::array<item_description, 1> zero{{{7, 0}}};
    EXPECT_THROW((void)variable_item_source{duplicate}, std::invalid_argument);
    EXPECT_THROW((void)variable_item_source{zero}, std::invalid_argument);
    EXPECT_THROW((void)fixed_item_source({10, -1}), std::invalid_argument);
}
} // namespace atlas::ui::tests
