/** @file @brief Replayable hierarchy routes, stable stacking and pointer reset policies. */
#include "input_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Parent capture/target/bubble order is independent of layout order or job completion.
TEST_F(input_fixture, RoutesRootCaptureThenTargetThenRootBubble)
{
    const auto root = tree.create();
    const auto child = tree.create(root);
    input.bind(root, handler, {.hit_test = false});
    input.bind(child, handler);
    begin();
    const std::array packet{input_event{input_kind::pointer_down, {1, 1}}};
    input.process(packet);
    std::vector<route_phase> phases;
    for (const auto& event : handler->events)
    {
        if (event.kind == event_kind::pointer_down)
        {
            phases.push_back(event.phase);
        }
    }

    EXPECT_EQ(phases,
              (std::vector{route_phase::capture, route_phase::target, route_phase::bubble}));
    finish();
}

/// @brief Foreground roots isolate their descendants and a capture handler can stop the route.
TEST_F(input_fixture, StackingAndStopPropagationUseThePresentationOrder)
{
    const auto back = tree.create();
    const auto child = tree.create(back);
    const auto front = tree.create();
    auto style = tree.paint(back);
    style.layer = 10;
    tree.set_paint(back, style);
    style.layer = -100;
    tree.set_paint(child, style);
    const auto front_handler = std::make_shared<recording_handler>();
    input.bind(child, handler);
    input.bind(front, front_handler);
    begin();
    EXPECT_EQ(input.hit_test({1, 1}), front);
    input.process({});
    finish();
    input.unbind(front);
    input.bind(back, front_handler, {.hit_test = false});
    front_handler->action = [](const routed_event& event, command_buffer&)
    {
        return event_response{.stop = event.kind == event_kind::pointer_down};
    };
    begin(2);
    const std::array packet{input_event{input_kind::pointer_down, {1, 1}}};
    input.process(packet);
    EXPECT_EQ(count(event_kind::pointer_down), 0);
    finish();
}

/// @brief Release/cancel preserve focus, whereas activation loss clears both ownership states.
class pointer_reset_test : public input_fixture, public ::testing::WithParamInterface<input_kind>
{
};

/// @brief All terminal primary-pointer paths release capture without a stale follow-up delivery.
TEST_P(pointer_reset_test, ReleasesCaptureWithTheDocumentedFocusPolicy)
{
    const auto target = tree.create();
    input.bind(target, handler, {.focusable = true});
    handler->action = [](const routed_event& event, command_buffer&)
    {
        return event.kind == event_kind::pointer_down
                   ? event_response{.capture_pointer = true, .focus = true}
                   : event_response{};
    };
    begin();
    const std::array packets{input_event{input_kind::pointer_down, {1, 1}},
                             input_event{GetParam(), {2, 2}}};
    input.process(packets);
    EXPECT_EQ(input.captured(), element_handle{});
    EXPECT_EQ(input.focused(), GetParam() == input_kind::window_blur ? element_handle{} : target);
    EXPECT_EQ(count(event_kind::capture_lost), 1);
    finish();
}

INSTANTIATE_TEST_SUITE_P(primary, pointer_reset_test,
                         ::testing::Values(input_kind::pointer_up, input_kind::pointer_cancel,
                                           input_kind::window_blur));
} // namespace atlas::ui::tests
