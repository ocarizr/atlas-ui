/** @file @brief Bounded wheel requests, nested consumption and invalid packet preflight. */
#include "scroll_interaction_support.hpp"
#include <limits>

namespace atlas::ui::tests
{
/// @brief A sealed programmatic offset at an earlier sync point is not an unrequested input change.
TEST_F(scroll_navigation_fixture, EarlierProgrammaticScrollPublicationIsNotOverwritten)
{
    list();
    for (const float initial : {0.f, 500.f})
    {
        offset({0, initial});
        open();
        input.process({});
        tree.commands().set(viewport, property::scroll_y, 70.f, 7);
        tree.apply(tree.commands().seal());
        EXPECT_EQ(position(), 70);
        finish();
        EXPECT_EQ(position(), 70);
    }
}

/// @brief Repeated packets coalesce without generating repeated-property conflict warnings.
TEST_F(scroll_navigation_fixture, WheelCoalescesAndPublishesOnlyAfterEndFrame)
{
    list();
    open();
    const std::array packets{wheel(1), wheel(1)};
    input.process(packets);
    EXPECT_EQ(position(), 0);
    input.tick();
    input.late_tick();
    input.end_frame();
    const auto commands = tree.commands().seal();
    EXPECT_TRUE(commands.conflicts().empty());
    EXPECT_EQ(commands.writes().size(), 1U);
    tree.apply(commands);
    EXPECT_EQ(position(), 80);
    open();
    replay({wheel(1000)});
    EXPECT_EQ(position(), 120);
    open();
    replay({wheel(-1000)});
    EXPECT_EQ(position(), 0);
}

/// @brief A horizontal-only viewport interprets the conventional vertical wheel on its axis.
TEST_F(scroll_navigation_fixture, HorizontalOnlyWheelUsesTheEnabledAxis)
{
    tree.set_panel(viewport, scroll_layout{true, false});
    resize_content({300, 80});
    open();
    replay({wheel(1)});
    EXPECT_EQ(std::get<scroll_layout>(tree.panel(viewport)).offset, (layout_point{40, 0}));
}

/// @brief A wheel packet at the inner limit bubbles to the outer scroll viewport.
TEST_F(scroll_navigation_fixture, NestedWheelAtLimitPropagatesToParent)
{
    tree.set_panel(content, scroll_layout{false, true, {0, 120}});
    resize_content({100, 160});
    const auto inner = rectangle(content);
    auto style = tree.layout(inner);
    style.intrinsic = {100, 280};
    tree.set_layout(inner, style);
    open();
    replay({wheel(1)});
    EXPECT_EQ(position(), 40);
    EXPECT_EQ(std::get<scroll_layout>(tree.panel(content)).offset.y, 120);
}

/// @brief Invalid suffixes cannot admit a wheel prefix or leak queued offsets.
TEST_F(scroll_navigation_fixture, InvalidWheelBatchDoesNotAdmitAValidPrefix)
{
    list();
    auto invalid = wheel(1);
    invalid.scroll.y = std::numeric_limits<float>::infinity();
    const std::array packets{wheel(1), invalid};
    open();
    EXPECT_THROW(input.process(packets), std::invalid_argument);
    replay();
    EXPECT_EQ(position(), 0);
}

/// @brief Invisible and disabled viewports never act as built-in scroll targets.
TEST_F(scroll_navigation_fixture, DisabledViewportDoesNotConsumeWheel)
{
    list();
    input.bind(viewport, handler, {.enabled = false});
    open();
    replay({wheel(1)});
    EXPECT_EQ(position(), 0);
}
} // namespace atlas::ui::tests
