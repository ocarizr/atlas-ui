/** @file @brief Scroll dispatch failure and command-capacity exhaustion preserve publication. */
#include "scroll_interaction_support.hpp"

namespace atlas::ui::tests
{
/// @brief A wrong target for a scroll scalar invalidates the complete batch before any scalar
/// changes.
TEST_F(scroll_navigation_fixture, ScrollCommandsValidateTheirTargetBeforePublishingOtherWrites)
{
    tree.commands().begin_frame(1);
    tree.commands().set(viewport, property::width, 99.f, 1);
    tree.commands().set(content, property::scroll_y, 20.f, 1);
    EXPECT_THROW(tree.apply(tree.commands().seal()), std::invalid_argument);
    EXPECT_FALSE(tree.requested(viewport).width_fixed);
    EXPECT_EQ(property_name(property::scroll_y), "scroll-y");
    EXPECT_FALSE(valid_scalar(property::scroll_x, std::numeric_limits<float>::infinity()));
}

/// @brief Throwing behavior discards coalesced offsets and releases built-in pointer capture.
TEST_F(scroll_navigation_fixture, CallbackFailureDiscardsOffsetsAndReleasesCapture)
{
    list();
    const auto bar = chrome();
    input.bind(bar, handler);
    handler->action = [](const routed_event& event, command_buffer&) -> event_response
    {
        if (event.kind == event_kind::pointer_move)
        {
            throw std::runtime_error("scroll callback failure");
        }

        return {};
    };
    open();
    const std::array packets{wheel(1), input_event{input_kind::pointer_down, {95, 20}},
                             input_event{input_kind::pointer_move, {95, 40}}};
    EXPECT_THROW(input.process(packets), std::runtime_error);
    EXPECT_TRUE(input.statistics().failed);
    EXPECT_EQ(input.captured(), element_handle{});
    EXPECT_EQ(tree.commands().pending(), 0U);
    EXPECT_EQ(position(), 0);
    handler->action = {};
    open();
    replay();
    EXPECT_EQ(position(), 0);
}

/// @brief All coalesced axis writes are preflighted before the first write is admitted.
TEST(scroll_input_failure_test, CommandBudgetRejectsTwoAxisFlushAtomically)
{
    jobs::job_system workers{1};
    element_tree tree{{.commands = {.max_commands = 1}}};
    const auto root = tree.create();
    tree.set_panel(root, scroll_layout{true, true});
    const auto child = tree.create(root);
    layout_style style;
    style.intrinsic = {300, 300};
    tree.set_layout(child, style);
    layout_engine layout{workers.dispatcher()};
    const auto geometry = layout.update(tree, {0, 0, 100, 80});
    input_session input{tree};
    input.begin_frame(1, tree.snapshot(), geometry);
    input.scroll_by(root, {40, 40});
    input.process({});
    input.tick();
    input.late_tick();
    EXPECT_THROW(input.end_frame(), std::overflow_error);
    EXPECT_EQ(tree.commands().pending(), 0U);
    EXPECT_TRUE(input.statistics().failed);
    EXPECT_EQ(std::get<scroll_layout>(tree.panel(root)).offset, layout_point{});
}
} // namespace atlas::ui::tests
