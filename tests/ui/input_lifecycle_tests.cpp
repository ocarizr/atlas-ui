/** @file @brief Lifecycle ordering, explicit stage boundaries and focus/capture invalidation. */
#include "input_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Initially disabled handlers start at first enable, not once per subsequent frame.
TEST_F(input_fixture, StartEnableAndSystemStagesHaveStableOrder)
{
    const auto target = tree.create();
    input.bind(target, handler, {.enabled = false});
    begin();
    input.process({});
    finish();
    EXPECT_TRUE(handler->events.empty());
    input.configure(target, {});
    begin(2);
    input.process({});
    finish();
    std::vector<event_kind> kinds;
    for (const auto& event : handler->events)
    {
        kinds.push_back(event.kind);
    }

    EXPECT_EQ(kinds, (std::vector{event_kind::start, event_kind::enable, event_kind::process_input,
                                  event_kind::tick, event_kind::late_tick, event_kind::end_frame}));
    input.configure(target, {.enabled = false});
    begin(3);
    input.process({});
    finish();
    EXPECT_EQ(count(event_kind::disable), 1);
    input.configure(target, {});
    begin(4);
    input.process({});
    finish();
    EXPECT_EQ(count(event_kind::start), 1);
    EXPECT_EQ(count(event_kind::enable), 2);
    input.shutdown(5);
    tree.apply(tree.commands().seal());
    EXPECT_EQ(count(event_kind::disable), 2);
    EXPECT_EQ(count(event_kind::end), 1);
    input.shutdown(6);
    EXPECT_EQ(count(event_kind::end), 1);
}

/// @brief Disabling an ancestor releases descendant ownership before input processing.
TEST_F(input_fixture, DisabledAncestorCancelsDescendantFocusAndCapture)
{
    const auto root = tree.create();
    const auto child = rectangle(root);
    input.bind(root, std::make_shared<recording_handler>(), {.hit_test = false});
    input.bind(child, handler, {.focusable = true});
    handler->action = [](const routed_event& event, command_buffer&)
    {
        return event.kind == event_kind::pointer_down
                   ? event_response{.capture_pointer = true, .focus = true}
                   : event_response{};
    };
    begin();
    const std::array down{input_event{input_kind::pointer_down, {2, 2}}};
    input.process(down);
    finish();
    input.configure(root, {.enabled = false});
    begin(2);
    EXPECT_EQ(input.focused(), element_handle{});
    EXPECT_EQ(input.captured(), element_handle{});
    EXPECT_EQ(count(event_kind::focus_lost), 1);
    EXPECT_EQ(count(event_kind::capture_lost), 1);
    EXPECT_EQ(count(event_kind::disable), 1);
    input.process(down);
    finish();
    EXPECT_EQ(count(event_kind::pointer_down), 1);
}

/// @brief Focus reveal is an owner command request even when a target is outside its clip.
TEST_F(input_fixture, FocusRevealCarriesFrozenTargetGeometryToAncestors)
{
    const auto root = tree.create();
    const auto child = tree.create(root);
    paint_style mask;
    mask.clip = true;
    tree.set_paint(root, mask);
    paint_style moved;
    moved.translation = {200, 0};
    tree.set_paint(child, moved);
    const auto parent_handler = std::make_shared<recording_handler>();
    input.bind(root, parent_handler, {.hit_test = false});
    input.bind(child, handler, {.focusable = true});
    begin();
    input.focus(child);
    EXPECT_EQ(input.focused(), child);
    const auto it = std::find_if(parent_handler->events.begin(), parent_handler->events.end(),
                                 [](const auto& event)
                                 {
                                     return event.kind == event_kind::ensure_visible;
                                 });
    ASSERT_NE(it, parent_handler->events.end());
    EXPECT_EQ(it->target, child);
    EXPECT_EQ(it->target_bounds.x, 200);
    EXPECT_EQ(it->clip, (layout_rect{0, 0, 100, 80}));
    input.process({});
    finish();
}

/// @brief Unbind cleanup preserves the old generation when a slot is immediately reused.
TEST_F(input_fixture, ReusedSlotCannotReceiveAnOldRegistrationOrCapture)
{
    const auto old = tree.create();
    input.bind(old, handler);
    begin();
    input.process({});
    finish();
    tree.remove(old);
    const auto replacement = tree.create();
    ASSERT_EQ(old.index, replacement.index);
    const auto next_handler = std::make_shared<recording_handler>();
    input.bind(replacement, next_handler);
    begin(2);
    input.process({});
    finish();
    EXPECT_EQ(count(event_kind::end), 1);
    EXPECT_FALSE(handler->events.back().target_alive);
    EXPECT_EQ(input.hit_test({1, 1}), replacement);
    EXPECT_EQ(next_handler->events.front().kind, event_kind::start);
}
} // namespace atlas::ui::tests
