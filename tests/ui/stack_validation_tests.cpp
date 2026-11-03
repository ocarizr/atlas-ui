/** @file @brief Invalid weights/policies and rejected relationship changes preserve live state. */
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace atlas::ui::tests
{
/// @brief Finite nonnegative weights are the entire numeric slot domain.
class invalid_stack_weight_test : public ::testing::TestWithParam<float>
{
};

/// @brief Both weight fields reject without introducing sparse components or changing revision.
TEST_P(invalid_stack_weight_test, rejects_before_invalidating_geometry)
{
    jobs::job_system pool{1};
    element_tree tree;
    const auto root = tree.create();
    tree.set_panel(root, vertical_layout{});
    const auto child = tree.create(root);
    layout_engine engine{pool.dispatcher()};
    engine.update(tree, {0, 0, 100, 100});
    const auto revision = tree.snapshot().revision();
    EXPECT_THROW(tree.set_stack_slot(child, {0, GetParam(), 1}), std::invalid_argument);
    EXPECT_THROW(tree.set_stack_slot(child, {0, 1, GetParam()}), std::invalid_argument);
    EXPECT_EQ(tree.snapshot().revision(), revision);
    EXPECT_TRUE(tree.snapshot().stack_slots().empty());
    EXPECT_TRUE(tree.computed(child));
}

INSTANTIATE_TEST_SUITE_P(weights, invalid_stack_weight_test,
                         ::testing::Values(-1.f, std::numeric_limits<float>::infinity(),
                                           std::numeric_limits<float>::quiet_NaN()),
                         [](const ::testing::TestParamInfo<float>& info)
                         {
                             return std::string{info.index == 0   ? "negative"
                                                : info.index == 1 ? "infinite"
                                                                  : "nan"};
                         });

/// @brief Unknown enum values cannot select a different layout policy silently.
TEST(stack_validation, malformed_policies_reject_before_mutation)
{
    element_tree tree;
    const auto root = tree.create();
    const auto revision = tree.snapshot().revision();
    vertical_layout column;
    // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange) -- Deliberate input validation.
    column.distribution = static_cast<stack_distribution>(255);
    EXPECT_THROW(tree.set_panel(root, column), std::invalid_argument);
    column = {};
    column.fill = static_cast<stack_fill>(255);
    EXPECT_THROW(tree.set_panel(root, column), std::invalid_argument);
    column = {};
    column.overflow = static_cast<stack_overflow>(255);
    EXPECT_THROW(tree.set_panel(root, column), std::invalid_argument);
    // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
    EXPECT_EQ(tree.panel(root), panel_layout{overlay_layout{}});
    EXPECT_EQ(tree.snapshot().revision(), revision);
}

/// @brief Missing, foreign or incompatible parents cannot acquire stack-specific parameters.
TEST(stack_validation, relationship_and_noop_rules_are_explicit)
{
    element_tree tree;
    const auto root = tree.create();
    const auto child = tree.create(root);
    EXPECT_FALSE(tree.stack_slot_for(root));
    EXPECT_FALSE(tree.stack_slot_for(child));
    EXPECT_THROW(tree.set_stack_slot(root, {}), std::invalid_argument);
    EXPECT_THROW(tree.set_stack_slot(child, {}), std::invalid_argument);
    tree.set_panel(root, vertical_layout{});
    const auto revision = tree.snapshot().revision();
    tree.set_stack_slot(child, {});
    EXPECT_EQ(tree.snapshot().revision(), revision);
    tree.set_stack_slot(child, {3, 0, 1});
    const auto changed = tree.snapshot().revision();
    tree.set_stack_slot(child, {3, 0, 1});
    EXPECT_EQ(tree.snapshot().revision(), changed);
    element_tree other;
    const auto foreign = other.create();
    EXPECT_THROW(tree.set_stack_slot(foreign, {}), std::invalid_argument);
    tree.reset_stack_slot(child);
    EXPECT_TRUE(tree.snapshot().stack_slots().empty());
    tree.remove(child);
    EXPECT_THROW(tree.stack_slot_for(child), std::invalid_argument);
}
} // namespace atlas::ui::tests
