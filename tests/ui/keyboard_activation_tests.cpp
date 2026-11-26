/** @file @brief Optional keyboard activation shares semantic routing and cancels safely. */
#include "input_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Navigation/focus alone never enables application activation conventions.
TEST_F(input_fixture, KeyboardActivationIsDisabledByDefault)
{
    const auto target = tree.create();
    input.bind(target, handler, {.focusable = true});
    begin();
    input.focus(target);
    const std::array events{input_event{.kind = input_kind::key_down, .key = 13},
                            input_event{.kind = input_kind::key_up, .key = 13}};
    input.process(events);
    EXPECT_EQ(count(event_kind::activate), 0);
}

/// @brief Repeated key-down packets produce one activation only after the matching release.
TEST_F(input_fixture, KeyboardActivationWaitsForReleaseAndIgnoresRepeats)
{
    const auto target = tree.create();
    input.bind(target, handler, {.focusable = true});
    input.configure_keyboard_activation();
    begin();
    input.focus(target);
    input.process({});
    std::size_t expected = 0;
    for (const auto key : {input::key::enter, input::key::space})
    {
        const input_event down{.kind = input_kind::key_down, .key = static_cast<unsigned>(key)};
        const input_event up{.kind = input_kind::key_up, .key = static_cast<unsigned>(key)};
        const std::array repeats{down, down};
        input.process_more(repeats);
        EXPECT_EQ(count(event_kind::activate), expected);
        input.process_more({&up, 1});
        EXPECT_EQ(count(event_kind::activate), ++expected);
        input.process_more({&up, 1});
        EXPECT_EQ(count(event_kind::activate), expected);
    }
}

/// @brief A consumed release and a focus round-trip both cancel a pending default activation.
TEST_F(input_fixture, KeyboardActivationRespectsConsumptionAndFocusChanges)
{
    const auto target = tree.create();
    input.bind(target, handler, {.focusable = true});
    input.configure_keyboard_activation();
    begin();
    input.focus(target);
    const input_event down{.kind = input_kind::key_down, .key = 13};
    const input_event up{.kind = input_kind::key_up, .key = 13};
    handler->action = [](const routed_event& event, command_buffer&)
    {
        return event_response{.stop = event.kind == event_kind::key_up};
    };
    const std::array click{down, up};
    input.process(click);
    EXPECT_EQ(count(event_kind::activate), 0);
    handler->action = {};
    input.process_more({&down, 1});
    input.focus({});
    input.focus(target);
    input.process_more({&down, 1});
    input.process_more({&up, 1});
    EXPECT_EQ(count(event_kind::activate), 0);
    input.process_more({&down, 1});
    const input_event blur{input_kind::window_blur};
    input.process_more({&blur, 1});
    input.process_more({&up, 1});
    EXPECT_EQ(count(event_kind::activate), 0);
}
} // namespace atlas::ui::tests
