/** @file @brief Invalid canvas data rejects before state, revision or geometry publication changes.
 */
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace atlas::ui::tests
{
/// @brief Named isolated slot values keep parameter failures readable.
struct invalid_canvas_case
{
    const char* name; ///< Stable CTest identity.
    canvas_slot slot; ///< Exactly one malformed property per case.
};

/// @brief Explicit data seeds every invalid field without mutable state shared across facts.
std::vector<invalid_canvas_case> invalid_canvas_slots()
{
    std::vector<invalid_canvas_case> cases;
    const auto add = [&](const char* name, auto edit)
    {
        canvas_slot value;
        edit(value);
        cases.push_back({name, value});
    };
    add("negative_anchor",
        [](auto& slot)
        {
            slot.horizontal.anchor_min = -0.1f;
        });
    add("anchor_above_one",
        [](auto& slot)
        {
            slot.vertical.anchor_max = 1.1f;
        });
    add("reversed_anchors",
        [](auto& slot)
        {
            slot.horizontal.anchor_min = 0.5f;
        });
    add("nan_offset",
        [](auto& slot)
        {
            slot.vertical.offset_min = std::numeric_limits<float>::quiet_NaN();
        });
    add("infinite_end",
        [](auto& slot)
        {
            slot.horizontal.offset_max = std::numeric_limits<float>::infinity();
        });
    add("negative_pivot",
        [](auto& slot)
        {
            slot.vertical.pivot = -1;
        });
    add("pivot_above_one",
        [](auto& slot)
        {
            slot.horizontal.pivot = 2;
        });
    // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange) -- Deliberate malformed API input.
    add("unknown_sizing",
        [](auto& slot)
        {
            slot.vertical.sizing = static_cast<canvas_sizing>(255);
        });
    // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
    return cases;
}

/// @brief Each invalid assignment starts with previously published valid geometry.
class invalid_canvas_test : public ::testing::TestWithParam<invalid_canvas_case>
{
};

/// @brief Rejection must not silently replace a default slot or invalidate existing results.
TEST_P(invalid_canvas_test, rejects_without_mutation)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    tree.set_panel(root, canvas_layout{});
    const auto child = tree.create(root);
    layout_engine engine{pool.dispatcher()};
    engine.update(tree, {0, 0, 100, 50});
    const auto revision = tree.snapshot().revision();
    EXPECT_THROW(tree.set_canvas_slot(child, GetParam().slot), std::invalid_argument);
    EXPECT_EQ(tree.canvas_slot_for(child), std::optional{canvas_slot{}});
    EXPECT_EQ(tree.snapshot().revision(), revision);
    EXPECT_TRUE(tree.computed(child));
    EXPECT_TRUE(tree.snapshot().canvas_slots().empty());
}

INSTANTIATE_TEST_SUITE_P(slots, invalid_canvas_test, ::testing::ValuesIn(invalid_canvas_slots()),
                         [](const ::testing::TestParamInfo<invalid_canvas_case>& info)
                         {
                             return std::string{info.param.name};
                         });

/// @brief A relative child cannot become an input equation for its own parent extent.
TEST(canvas_validation, absolute_measure_rejects_relative_slots_and_conversion_atomically)
{
    element_tree tree;
    const auto root = tree.create();
    tree.set_panel(root, canvas_layout{});
    const auto child = tree.create(root);
    canvas_slot relative;
    relative.horizontal.anchor_min = relative.horizontal.anchor_max = 0.5f;
    tree.set_canvas_slot(child, relative);
    const auto revision = tree.snapshot().revision();
    EXPECT_THROW(tree.set_panel(root, canvas_layout{canvas_measure::absolute_children}),
                 std::invalid_argument);
    EXPECT_EQ(tree.panel(root), panel_layout{canvas_layout{}});
    EXPECT_EQ(tree.snapshot().revision(), revision);
    tree.reset_canvas_slot(child);
    EXPECT_TRUE(tree.snapshot().canvas_slots().empty());
    tree.set_panel(root, canvas_layout{canvas_measure::absolute_children});
    EXPECT_THROW(tree.set_canvas_slot(child, relative), std::invalid_argument);
    relative = {};
    relative.vertical.sizing = canvas_sizing::stretch;
    EXPECT_THROW(tree.set_canvas_slot(child, relative), std::invalid_argument);
}

/// @brief Typed spacing and measurement validation retain the previous policy and input identity.
TEST(canvas_validation, invalid_panels_and_visual_origins_preserve_state)
{
    element_tree tree;
    const auto root = tree.create();
    const auto revision = tree.snapshot().revision();
    EXPECT_THROW(tree.set_panel(root, horizontal_layout{-1}), std::invalid_argument);
    EXPECT_THROW(tree.set_panel(root, vertical_layout{std::numeric_limits<float>::infinity()}),
                 std::invalid_argument);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- Deliberate rejection.
    EXPECT_THROW(tree.set_panel(root, canvas_layout{static_cast<canvas_measure>(255)}),
                 std::invalid_argument);
    EXPECT_THROW(tree.set_visual(root, {{-1, 0}}), std::invalid_argument);
    EXPECT_THROW(tree.set_visual(root, {{0, std::numeric_limits<float>::quiet_NaN()}}),
                 std::invalid_argument);
    EXPECT_EQ(tree.panel(root), panel_layout{overlay_layout{}});
    EXPECT_EQ(tree.visual(root), visual_style{});
    EXPECT_EQ(tree.snapshot().revision(), revision);
    EXPECT_THROW(resolve_transform_origin({0, 0, -1, 1}), std::invalid_argument);
    EXPECT_THROW(resolve_transform_origin({0, 0, 1, 1}, {{0, 2}}), std::invalid_argument);
}

/// @brief Checked canvas edges reject overflow without publishing any invalid rectangles.
TEST(canvas_validation, numeric_overflow_preserves_retained_result_and_allows_retry)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    tree.set_panel(root, canvas_layout{});
    const auto child = tree.create(root);
    layout_style style;
    style.intrinsic.width = std::numeric_limits<float>::max();
    tree.set_layout(child, style);
    layout_engine engine{pool.dispatcher()};
    const auto before = engine.update(tree, {0, 0, 100, 50});
    canvas_slot slot;
    slot.horizontal.offset_min = std::numeric_limits<float>::max();
    tree.set_canvas_slot(child, slot);
    EXPECT_THROW(engine.update(tree, {0, 0, 100, 50}), std::overflow_error);
    EXPECT_FALSE(tree.computed(child));
    EXPECT_EQ(require_geometry(before.find(child)).bounds.x, 0.f);
    tree.reset_canvas_slot(child);
    EXPECT_EQ(require_geometry(engine.update(tree, {0, 0, 100, 50}).find(child)).bounds.x, 0.f);
}
} // namespace atlas::ui::tests
