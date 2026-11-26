/** @file @brief Spatial/explicit focus movement never synthesizes activation. */
#include "focus_navigation_support.hpp"

namespace atlas::ui::tests
{
/// @brief Four geometry directions share the same focus transitions as explicit programmatic focus.
TEST_F(focus_navigation_fixture, spatial_directions_follow_geometry_without_activation)
{
    const auto center = button({40, 30});
    const auto left = button({10, 30});
    const auto right = button({70, 30});
    const auto up = button({40, 0});
    const auto down = button({40, 60});
    begin();
    for (const auto& [direction, target] : std::array{
             std::pair{focus_direction::left, left}, std::pair{focus_direction::right, right},
             std::pair{focus_direction::up, up}, std::pair{focus_direction::down, down}})
    {
        input.focus(center);
        EXPECT_TRUE(input.navigate(direction));
        EXPECT_EQ(input.focused(), target);
    }

    EXPECT_EQ(count(event_kind::action), 0U);
    EXPECT_EQ(count(event_kind::key_down), 0U);
    input.process({});
    finish();
}

/// @brief Aligned candidates precede diagonal ones; equal geometry uses stable layout ties.
TEST_F(focus_navigation_fixture, beam_and_stable_ties_are_deterministic)
{
    const auto origin = button({0, 0});
    const auto diagonal = button({12, 12});
    const auto aligned = button({70, 0});
    (void)diagonal;
    const auto tie = button({70, 0});
    (void)tie;
    begin();
    input.focus(origin);
    EXPECT_TRUE(input.navigate(focus_direction::right));
    EXPECT_EQ(input.focused(), aligned);
    input.process({});
    finish();
}

/// @brief Explicit destinations override geometry; a deliberate block prevents automatic fallback.
TEST_F(focus_navigation_fixture, explicit_destinations_and_blocks_override_search)
{
    const auto a = button({0, 0});
    const auto b = button({20, 0});
    const auto c = button({40, 0});
    focus_navigation_options options;
    options.destinations[1] = c;
    options.blocked[0] = true;
    input.configure_navigation(a, options);
    begin();
    input.focus(a);
    EXPECT_FALSE(input.navigate(focus_direction::left));
    EXPECT_EQ(input.focused(), a);
    EXPECT_TRUE(input.navigate(focus_direction::right));
    EXPECT_EQ(input.focused(), c);
    EXPECT_NE(input.focused(), b);
    input.process({});
    finish();
}

/// @brief A hidden/disabled/recycled explicit destination never redirects to a nearby widget.
TEST_F(focus_navigation_fixture, unavailable_override_stays_put_and_does_not_fall_back)
{
    const auto a = button({0, 0});
    const auto b = button({20, 0});
    const auto c = button({40, 0});
    focus_navigation_options options;
    options.destinations[1] = b;
    input.configure_navigation(a, options);
    input.configure(b, {.enabled = false, .focusable = true});
    begin();
    input.focus(a);
    EXPECT_FALSE(input.navigate(focus_direction::right));
    EXPECT_EQ(input.focused(), a);
    input.process({});
    finish();
    tree.remove(b);
    const auto replacement = button({20, 0});
    begin(2);
    input.focus(a);
    EXPECT_FALSE(input.navigate(focus_direction::right));
    EXPECT_NE(input.focused(), replacement);
    input.process({});
    finish();
    (void)c;
}

/// @brief Scopes isolate automatic search and wrapping is off until explicitly selected.
TEST_F(focus_navigation_fixture, independent_scope_wrap_is_optional)
{
    const auto row = group({0, 0});
    const auto a = button({0, 0}, row);
    const auto b = button({20, 0}, row);
    const auto outside = button({80, 0});
    input.configure_navigation_scope(row);
    begin();
    input.focus(b);
    EXPECT_FALSE(input.navigate(focus_direction::right));
    EXPECT_NE(input.focused(), outside);
    input.process({});
    finish();
    input.configure_navigation_scope(row, {.wrap_horizontal = true});
    begin(2);
    input.focus(b);
    EXPECT_TRUE(input.navigate(focus_direction::right));
    EXPECT_EQ(input.focused(), a);
    input.process({});
    finish();
}

/// @brief An explicit relationship may intentionally bridge groups inside the same view.
TEST_F(focus_navigation_fixture, explicit_connections_can_bridge_groups)
{
    const auto row = group({0, 0});
    const auto a = button({0, 0}, row);
    const auto outside = button({80, 0});
    input.configure_navigation_scope(row);
    focus_navigation_options options;
    options.destinations[1] = outside;
    input.configure_navigation(a, options);
    begin();
    input.focus(a);
    EXPECT_TRUE(input.navigate(focus_direction::right));
    EXPECT_EQ(input.focused(), outside);
    input.process({});
    finish();
}

/// @brief Changed geometry is read only at coherent frame boundaries, preserving input snapshots.
TEST_F(focus_navigation_fixture, resize_and_position_changes_use_the_next_frame)
{
    const auto a = button({0, 0});
    const auto b = button({20, 0});
    const auto c = button({40, 0});
    begin();
    input.focus(a);
    EXPECT_TRUE(input.navigate(focus_direction::right));
    EXPECT_EQ(input.focused(), b);
    input.process({});
    finish();
    auto slot = *tree.canvas_slot_for(b);
    slot.horizontal.offset_min = 60;
    tree.set_canvas_slot(b, slot);
    begin(2);
    input.focus(a);
    EXPECT_TRUE(input.navigate(focus_direction::right));
    EXPECT_EQ(input.focused(), c);
    input.process({});
    finish();
}
} // namespace atlas::ui::tests
