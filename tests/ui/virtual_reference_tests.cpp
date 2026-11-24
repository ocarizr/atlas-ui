/** @file @brief Reproducible retained sequential reference and nested presentation equivalence. */
#include "virtual_list_support.hpp"

namespace atlas::ui::tests
{
/// @brief Seeded exact sizes compare visible paint and scroll ranges against a fully retained list.
TEST_F(virtual_list_fixture, variable_rows_match_nonvirtualized_sequential_reference)
{
    std::uint32_t seed = 122;
    std::vector<item_description> descriptions;
    element_tree reference;
    const auto owner = reference.create();
    reference.set_panel(owner, scroll_layout{});
    const auto stack = reference.create(owner);
    reference.set_panel(stack, vertical_layout{});
    reference.set_viewport({0, 0, 100, 80});
    for (std::size_t index = 0; index < 40; ++index)
    {
        seed = seed * 1664525U + 1013904223U;
        const float height = static_cast<float>(8 + seed % 33);
        descriptions.push_back({index + 1, height});
        const auto row = reference.create(stack);
        auto style = reference.layout(row);
        style.intrinsic.height = height;
        reference.set_layout(row, style);
        paint_style visual;
        visual.fill = true;
        reference.set_paint(row, visual);
    }

    data = std::make_shared<variable_item_source>(descriptions);
    list->update(data, 80);
    for (float offset : {0.0F, 55.0F, 333.0F, 1000.0F, 22.0F, 700.0F, 0.0F})
    {
        SCOPED_TRACE(offset);
        scroll(offset);
        reference.set_panel(owner, scroll_layout{false, true, {0, offset}});
        const auto actual_source = tree.snapshot();
        const auto expected_source = reference.snapshot();
        const auto actual_layout = layout.calculate(actual_source);
        const auto expected_layout = layout.calculate(expected_source);
        EXPECT_EQ(actual_layout.find_scroll(viewport), expected_layout.find_scroll(owner));
        const auto actual = paint.calculate(actual_source, actual_layout);
        const auto expected = paint.calculate(expected_source, expected_layout);
        ASSERT_EQ(actual.quads().size(), expected.quads().size());
        for (std::size_t i = 0; i < actual.quads().size(); ++i)
        {
            EXPECT_EQ(actual.quads()[i].bounds, expected.quads()[i].bounds);
            EXPECT_EQ(actual.quads()[i].clip, expected.quads()[i].clip);
        }

        open();
        input.process({});
        finish();
    }
}

/// @brief Parent scroll masks still bound hit-testing and paint for realized item subtrees.
TEST_F(virtual_list_fixture, nested_viewport_masks_share_paint_and_input_geometry)
{
    const auto outer = tree.create();
    tree.set_panel(outer, scroll_layout{});
    auto outer_style = tree.layout(outer);
    outer_style.constraints.maximum.height = 40;
    tree.set_layout(outer, outer_style);
    tree.reparent(viewport, outer);
    auto inner_style = tree.layout(viewport);
    inner_style.intrinsic.height = 80;
    tree.set_layout(viewport, inner_style);
    update();
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    const auto painted = paint.calculate(source, geometry);
    ASSERT_FALSE(painted.quads().empty());
    for (const auto& quad : painted.quads())
    {
        EXPECT_LE(quad.clip.height, 40);
    }

    input.begin_frame(++frame, source, geometry);
    EXPECT_EQ(input.hit_test({10, 10}), list->rows().front().ticket.root);
    EXPECT_NE(input.hit_test({10, 50}), list->rows()[2].ticket.root);
    input.process({});
    finish();
}

/// @brief Reordering stable keys preserves bindings while changing logical child/paint order.
TEST_F(virtual_list_fixture, reordered_source_changes_sibling_order_without_rebinding)
{
    update(3);
    const auto third = list->rows()[2].ticket;
    const std::array<item_description, 3> reordered{{{3, 20}, {1, 20}, {2, 20}}};
    data = std::make_shared<variable_item_source>(reordered);
    list->update(data, 80);
    EXPECT_EQ(list->statistics().created, 0);
    EXPECT_EQ(list->rows().front().ticket, third);
    EXPECT_EQ(tree.first_child(tree.first_child(viewport)), third.root);
}
} // namespace atlas::ui::tests
