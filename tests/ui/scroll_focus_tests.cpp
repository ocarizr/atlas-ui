/** @file @brief Minimal focus reveal, deterministic endpoints and retained-focus manual scrolling.
 */
#include "scroll_interaction_support.hpp"
#include <atlas/input/key.hpp>

namespace atlas::ui::tests
{
/// @brief Keyboard and semantic gamepad actions move focus and queue reveal in the same runtime.
TEST_F(scroll_navigation_fixture, KeyboardAndGamepadNavigationRevealTheNewFocus)
{
    list();
    input.configure_scroll_navigation({.next = 71, .last = 72});
    open();
    input.focus(items.front());
    replay({{input_kind::key_down, {}, static_cast<unsigned>(input::key::end)}});
    EXPECT_EQ(input.focused(), items.back());
    EXPECT_EQ(position(), 120);
    open();
    replay({{input_kind::key_down, {}, static_cast<unsigned>(input::key::home)}});
    EXPECT_EQ(input.focused(), items.front());
    EXPECT_EQ(position(), 0);
    input_event next{input_kind::action};
    next.action = input::action_event{
        71, input::value_type::button, input::action_phase::performed, {1, 0}, 3};
    open();
    replay({next, next});
    EXPECT_EQ(input.focused(), items[2]);
    EXPECT_EQ(position(), 40);
}

/// @brief First, intermediate and last focus transitions use the required list boundary policy.
TEST_F(scroll_navigation_fixture, FocusEndpointsAndMiddleUseMinimumReveal)
{
    list();
    open();
    input.focus(items[2]);
    replay();
    EXPECT_EQ(position(), 40);
    open();
    input.focus(items[1]);
    replay();
    EXPECT_EQ(position(), 40);
    open();
    input.focus(items.back());
    replay();
    EXPECT_EQ(position(), 120);
    open();
    input.focus(items.front());
    replay();
    EXPECT_EQ(position(), 0);
}

/// @brief A retained focus is not an instruction to override manual pointer scrolling each frame.
TEST_F(scroll_navigation_fixture, ManualScrollDoesNotSnapBackToRetainedFocus)
{
    list();
    open();
    input.focus(items.back());
    replay();
    open();
    replay({wheel(-1)});
    EXPECT_EQ(position(), 80);
    open();
    replay();
    EXPECT_EQ(position(), 80);
    EXPECT_EQ(input.focused(), items.back());
    open();
    input.ensure_visible(items.back());
    replay();
    EXPECT_EQ(position(), 120);
}

/// @brief Oversized content has a stable leading policy, including a single first/last item.
TEST_F(scroll_navigation_fixture, OversizedSingleItemDoesNotOscillateBetweenEdges)
{
    list({.count = 1, .height = 200});
    offset({0, 70});
    open();
    input.focus(items.front());
    replay();
    EXPECT_EQ(position(), 0);
    open();
    input.ensure_visible(items.front());
    replay();
    EXPECT_EQ(position(), 0);
}

/// @brief Existing focusable geometry outside the clip receives no pointer press.
TEST_F(scroll_navigation_fixture, ClippedItemsCannotReceivePointerEvents)
{
    list();
    open();
    replay({{input_kind::pointer_down, {20, 150}}});
    EXPECT_EQ(count(event_kind::pointer_down), 0U);
    EXPECT_EQ(input.focused(), element_handle{});
}

/// @brief Reordering and insertion preserve the focused origin when there is remaining range.
TEST_F(scroll_navigation_fixture, InsertionReorderAndRemovalPreserveOrRetireFocusAnchor)
{
    list();
    open();
    input.focus(items[2]);
    replay();
    const auto extra = rectangle(content);
    auto style = tree.layout(extra);
    style.intrinsic = {100, 40};
    tree.set_layout(extra, style);
    tree.set_stack_slot(extra, {-1});
    open();
    replay();
    EXPECT_EQ(position(), 80);
    tree.set_stack_slot(items[2], {-2});
    open();
    replay();
    EXPECT_EQ(position(), 0);
    tree.remove(items[2]);
    open();
    replay();
    EXPECT_EQ(input.focused(), element_handle{});
    EXPECT_EQ(position(), 0);
}

/// @brief Both axes support semantic logical deltas and explicit reveal without a Lua VM.
TEST_F(scroll_navigation_fixture, BothAxesClampSemanticRequestsAndRevealUnboundElements)
{
    tree.set_panel(viewport, scroll_layout{true, true});
    resize_content({300, 300});
    open();
    input.scroll_by(viewport, {500, 500});
    replay();
    EXPECT_EQ(std::get<scroll_layout>(tree.panel(viewport)).offset, (layout_point{200, 220}));
    open();
    input.ensure_visible(content);
    replay();
    EXPECT_EQ(std::get<scroll_layout>(tree.panel(viewport)).offset, (layout_point{}));
}

/// @brief Inner reveal is included before deciding how much an outer viewport must move.
TEST_F(scroll_navigation_fixture, NestedFocusRevealReachesBothViewportBoundaries)
{
    tree.set_panel(content, scroll_layout{});
    resize_content({100, 160});
    const auto stack = tree.create(content);
    tree.set_panel(stack, vertical_layout{});
    element_handle last;
    for (unsigned index = 0; index < 4; ++index)
    {
        last = rectangle(stack);
        auto style = tree.layout(last);
        style.intrinsic = {100, 60};
        tree.set_layout(last, style);
        input.bind(last, handler, {.focusable = true});
    }

    open();
    input.focus(last);
    replay();
    EXPECT_EQ(std::get<scroll_layout>(tree.panel(content)).offset.y, 80);
    EXPECT_EQ(position(), 80);
}
} // namespace atlas::ui::tests
