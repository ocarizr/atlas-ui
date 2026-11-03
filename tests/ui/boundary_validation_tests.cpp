/** @file @brief Graph, reference and constraint failures must have no visible side effects. */
#include "boundary_test_support.hpp"
#include <limits>

namespace atlas::ui::tests
{
/// @brief Dependencies from either axis participate in the same nonrecursive cycle check.
TEST_F(boundary_test, rejects_direct_indirect_and_cross_axis_cycles_atomically)
{
    const auto root = canvas();
    const auto a = tree.create(root);
    const auto b = tree.create(root);
    const auto c = tree.create(root);
    tree.set_boundaries(b, after(a));
    tree.set_boundaries(c, {boundary_relation{b}, {}});
    const auto before = engine.update(tree, {0, 0, 100, 100});
    const auto revision = tree.snapshot().revision();
    for (const auto source : {a, b, c})
    {
        EXPECT_THROW(tree.set_boundaries(a, after(source)), std::invalid_argument);
        EXPECT_EQ(tree.snapshot().revision(), revision);
        EXPECT_EQ(tree.boundaries(a), layout_boundaries{});
        EXPECT_TRUE(tree.computed(a));
    }

    tree.publish_layout(before);
}

/// @brief Parent-relative measurement loops are excluded even for apparently absolute axes.
TEST_F(boundary_test, rejects_unsupported_scope_and_content_sized_domains)
{
    const auto root = canvas();
    const auto source = tree.create(root);
    const auto target = tree.create(root);
    const auto other_canvas = canvas();
    const auto foreign_sibling = tree.create(other_canvas);
    const auto descendant = tree.create(source);
    element_tree foreign;
    for (const auto bad :
         {root, target, descendant, foreign_sibling, foreign.create(), element_handle{}})
    {
        EXPECT_THROW(tree.set_boundaries(target, after(bad)), std::invalid_argument);
    }

    EXPECT_THROW(tree.set_boundaries(root, after(source)), std::invalid_argument);
    tree.set_panel(root, canvas_layout{canvas_measure::absolute_children});
    EXPECT_THROW(tree.set_boundaries(target, after(source)), std::invalid_argument);
    tree.set_panel(root, overlay_layout{});
    EXPECT_THROW(tree.set_boundaries(target, after(source)), std::invalid_argument);
    EXPECT_TRUE(tree.snapshot().boundaries().empty());
}

/// @brief Negative/nonfinite spacing and all unknown enums are diagnosed before sparse mutation.
TEST_F(boundary_test, rejects_invalid_values_and_conflicting_canvas_axes)
{
    const auto root = canvas();
    const auto source = tree.create(root);
    const auto target = tree.create(root);
    const auto revision = tree.snapshot().revision();
    for (const auto gap :
         {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        EXPECT_THROW(
            tree.set_boundaries(target, {{}, boundary_relation{source, boundary_side::after, gap}}),
            std::invalid_argument);
    }

    auto relation = boundary_relation{source};
    // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange) -- Deliberate malformed API inputs.
    relation.side = static_cast<boundary_side>(255);
    EXPECT_THROW(tree.set_boundaries(target, {{}, relation}), std::invalid_argument);
    relation = {source};
    relation.sizing = static_cast<boundary_sizing>(255);
    EXPECT_THROW(tree.set_boundaries(target, {{}, relation}), std::invalid_argument);
    relation = {source};
    relation.overflow = static_cast<boundary_overflow>(255);
    EXPECT_THROW(tree.set_boundaries(target, {{}, relation}), std::invalid_argument);
    // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
    EXPECT_EQ(tree.snapshot().revision(), revision);

    canvas_slot slot;
    slot.vertical.anchor_min = slot.vertical.anchor_max = 0.5f;
    tree.set_canvas_slot(target, slot);
    EXPECT_THROW(tree.set_boundaries(target, after(source)), std::invalid_argument);
    tree.reset_canvas_slot(target);
    tree.set_boundaries(target, after(source));
    EXPECT_THROW(tree.set_canvas_slot(target, slot), std::invalid_argument);
    slot.vertical = {};
    slot.horizontal.anchor_min = slot.horizontal.anchor_max = 1;
    EXPECT_NO_THROW(tree.set_canvas_slot(target, slot));
    EXPECT_EQ(tree.boundaries(target), after(source));
}

/// @brief A generation change never silently retargets a dependency at the recycled public slot.
TEST_F(boundary_test, source_lifetime_and_structural_domain_edits_require_explicit_reset)
{
    const auto root = canvas();
    const auto source = tree.create(root);
    const auto target = tree.create(root);
    tree.set_boundaries(target, after(source));
    const auto revision = tree.snapshot().revision();
    EXPECT_THROW(tree.remove(source), std::invalid_argument);
    EXPECT_THROW(tree.reparent(source), std::invalid_argument);
    EXPECT_THROW(tree.set_panel(root, overlay_layout{}), std::invalid_argument);
    EXPECT_THROW(tree.set_panel(root, canvas_layout{canvas_measure::absolute_children}),
                 std::invalid_argument);
    EXPECT_EQ(tree.snapshot().revision(), revision);
    EXPECT_EQ(tree.parent(source), root);
    tree.reparent(target, root);
    EXPECT_EQ(tree.boundaries(target), after(source));
    tree.reparent(target);
    EXPECT_EQ(tree.boundaries(target), layout_boundaries{});
    tree.remove(source);
    const auto recycled = tree.create(root);
    EXPECT_EQ(recycled.index, source.index);
    EXPECT_NE(recycled.generation, source.generation);
    tree.reparent(target, root);
    EXPECT_THROW(tree.set_boundaries(target, after(source)), std::invalid_argument);
    tree.set_boundaries(target, after(recycled));
    EXPECT_NO_THROW(tree.remove(root));
    EXPECT_EQ(tree.size(), 0U);
}
} // namespace atlas::ui::tests
