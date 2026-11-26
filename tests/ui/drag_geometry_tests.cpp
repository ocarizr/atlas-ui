/** @file @brief Drag destinations share painting transforms and inherited clipping. */
#include "drag_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Use the same masked translated child at reproducible logical pointer positions.
class drag_geometry_test : public drag_fixture,
                           public testing::WithParamInterface<std::pair<float, bool>>
{
};

/// @brief Hits outside the transformed border or ancestor mask cannot accept a transfer.
TEST_P(drag_geometry_test, NegotiatesOnlyInsideTransformedClippedDestination)
{
    destination.reset_drop_target(target);
    layout_style style;
    style.constraints.minimum = style.constraints.maximum = {40, 80};
    style.horizontal = layout_alignment::start;
    other.set_layout(target, style);
    paint_style mask;
    mask.clip = true;
    other.set_paint(target, mask);
    const auto child = other.create(target);
    style.constraints.minimum = style.constraints.maximum = {100, 80};
    other.set_layout(child, style);
    paint_style translated;
    translated.translation = {10, 0};
    other.set_paint(child, translated);
    destination.bind(child, std::make_shared<recording_handler>());
    destination.configure_drop_target(child, {{"test.item"}});
    begin();
    packet({input_kind::pointer_down, {10, 10}});
    packet({input_kind::pointer_move, {GetParam().first, 20}});
    EXPECT_EQ(drag.feedback().target == child, GetParam().second);
    packet({input_kind::pointer_up, {GetParam().first, 20}});
    EXPECT_EQ(drag.pending_drop().has_value(), GetParam().second);
    if (const auto request = drag.pending_drop())
    {
        drag.resolve(request->transfer, false);
    }

    finish();
}

INSTANTIATE_TEST_SUITE_P(logical_bounds, drag_geometry_test,
                         testing::Values(std::pair{105.F, false}, std::pair{115.F, true},
                                         std::pair{145.F, false}),
                         [](const auto& info)
                         {
                             return info.index == 0   ? "before_transformed_border"
                                    : info.index == 1 ? "inside_mask"
                                                      : "outside_mask";
                         });
} // namespace atlas::ui::tests
