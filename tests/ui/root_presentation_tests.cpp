/** @file @brief Snapshot-checked root coordinates agree with paint and inverse input mapping. */
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui/root_presentation.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace atlas::ui::tests
{
/// @brief One owner and independent tree per fact, with the worker pool released last.
class root_presentation_test : public testing::Test
{
  protected:
    jobs::job_system jobs{1}; ///< Shared infrastructure outlives the immutable snapshots.
    element_tree tree;        ///< No native SDK or renderer.
    layout_engine layout{jobs.dispatcher()}; ///< Reuse the normal layout contract.
};

/// @brief Root transforms reuse the rendering implementation, including an exact exchanged axis.
TEST_F(root_presentation_test, MapsTranslatedScaledRotatedAndCollapsedBorders)
{
    const auto root = tree.create();
    paint_style style;
    style.translation = {30, 0};
    style.scale = {2, 1};
    style.rotation = 90;
    tree.set_paint(root, style);
    const auto geometry = layout.update(tree, {0, 0, 20, 10});
    const auto source = tree.snapshot();
    EXPECT_EQ(root_visual_bounds(source, geometry, root), (layout_rect{35, -15, 10, 40}));
    EXPECT_TRUE(root_visual_contains(source, geometry, root, {40, 5}));
    EXPECT_FALSE(root_visual_contains(source, geometry, root, {5, 5}));
    style.scale = {0, 0};
    tree.set_paint(root, style);
    EXPECT_FALSE(root_visual_contains(tree.snapshot(), geometry, root, {40, 5}));
}

/// @brief Foreign geometry, nonroot elements and invalid pointers fail before coordinate use.
TEST_F(root_presentation_test, RejectsMismatchedSnapshotsAndNonrootQueries)
{
    const auto root = tree.create();
    const auto child = tree.create(root);
    const auto geometry = layout.update(tree, {0, 0, 20, 10});
    const auto source = tree.snapshot();
    EXPECT_THROW((void)root_visual_bounds(source, geometry, child), std::invalid_argument);
    element_tree other;
    (void)other.create();
    EXPECT_THROW((void)root_visual_bounds(other.snapshot(), geometry, root), std::invalid_argument);
    EXPECT_THROW((void)root_visual_contains(source, geometry, root,
                                            {std::numeric_limits<float>::quiet_NaN(), 0}),
                 std::invalid_argument);
}
} // namespace atlas::ui::tests
