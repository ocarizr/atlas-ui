/** @file @brief Hit testing, capture and command-producing owner callbacks. */
#include "input_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Unpainted interactive panels participate in stacking and inherited clipping.
TEST_F(input_fixture, HitTestingIncludesUnpaintedPanelsAndHonorsAncestorClips)
{
    const auto parent = tree.create();
    layout_style size;
    size.constraints.minimum = size.constraints.maximum = {40, 30};
    size.horizontal = size.vertical = layout_alignment::start;
    tree.set_layout(parent, size);
    paint_style mask;
    mask.clip = true;
    tree.set_paint(parent, mask);
    const auto child = tree.create(parent);
    size.constraints.minimum = size.constraints.maximum = {100, 80};
    tree.set_layout(child, size);
    input.bind(child, handler);
    begin();
    EXPECT_EQ(input.hit_test({20, 20}), child);
    EXPECT_EQ(input.hit_test({50, 20}), element_handle{});
    EXPECT_EQ(input.hit_test({40, 20}), element_handle{});
    input.process({});
    finish();
}

/// @brief Captured motion routes outside the original rectangle and release is explicit.
TEST_F(input_fixture, CaptureAndFocusRemainOnTheOwnerAndProduceDeferredCommands)
{
    const auto target = rectangle();
    input.bind(target, handler, {.focusable = true});
    handler->action = [target](const routed_event& event, command_buffer& commands)
    {
        if (event.kind == event_kind::pointer_down && event.phase == route_phase::target)
        {
            EXPECT_NO_THROW(jobs::require_coordinator_thread());
            commands.set(target, property::opacity, 0.5f, 42);
            return event_response{.capture_pointer = true, .focus = true};
        }

        return event_response{};
    };
    begin();
    const std::array events{input_event{input_kind::pointer_down, {10, 10}},
                            input_event{input_kind::pointer_move, {150, 150}}};
    input.process(events);
    EXPECT_EQ(input.captured(), target);
    EXPECT_EQ(input.focused(), target);
    EXPECT_EQ(tree.requested(target).opacity, 1);
    EXPECT_EQ(count(event_kind::pointer_move), 1);
    finish();
    EXPECT_EQ(tree.requested(target).opacity, 0.5f);
}

/// @brief Removal inside an earlier route callback suppresses stale target delivery.
TEST_F(input_fixture, RemovalDuringCapturePreventsTargetCallbacksAndCleansState)
{
    const auto root = tree.create();
    const auto child = rectangle(root);
    const auto parent_handler = std::make_shared<recording_handler>();
    input.bind(root, parent_handler, {.hit_test = false});
    input.bind(child, handler);
    parent_handler->action = [&](const routed_event& event, command_buffer&)
    {
        if (event.kind == event_kind::pointer_down && event.phase == route_phase::capture)
        {
            tree.remove(child);
        }

        return event_response{};
    };
    begin();
    const std::array events{input_event{input_kind::pointer_down, {10, 10}}};
    input.process(events);
    EXPECT_EQ(count(event_kind::pointer_down), 0);
    EXPECT_EQ(count(event_kind::disable), 1);
    EXPECT_EQ(count(event_kind::end), 1);
    EXPECT_EQ(input.captured(), element_handle{});
    finish();
}
} // namespace atlas::ui::tests
