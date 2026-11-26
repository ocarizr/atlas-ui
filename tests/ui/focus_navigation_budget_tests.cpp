/** @file @brief Scan admission fails before an unintended focus recipient can be notified. */
#include "focus_navigation_support.hpp"

namespace atlas::ui::tests
{
/// @brief A bounded scan failure follows existing frame cleanup and permits a later recovery.
TEST_F(focus_navigation_fixture, candidate_budget_failure_does_not_deliver_an_unintended_focus)
{
    const auto a = button({0, 0});
    const auto b = button({20, 0});
    input_session limited{tree, {.max_navigation_candidates = 1}};
    limited.bind(a, handler, {.focusable = true});
    limited.bind(b, handler, {.focusable = true});
    tree.set_viewport({0, 0, 100, 80});
    const auto source = tree.snapshot();
    const auto geometry = layout.calculate(source);
    limited.begin_frame(1, source, geometry);
    limited.focus(a);
    const auto before = count(event_kind::focus_gained);
    EXPECT_THROW((void)limited.navigate(focus_direction::right), std::overflow_error);
    EXPECT_TRUE(limited.statistics().failed);
    EXPECT_EQ(count(event_kind::focus_gained), before);
    limited.begin_frame(2, source, geometry);
    limited.focus(b);
    limited.process({});
    limited.tick();
    limited.late_tick();
    limited.end_frame();
    tree.apply(tree.commands().seal());
}
} // namespace atlas::ui::tests
