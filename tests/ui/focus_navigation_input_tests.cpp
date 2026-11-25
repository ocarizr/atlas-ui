/** @file @brief Ordered keyboard, group action mapping and consumable navigation packets. */
#include "focus_navigation_support.hpp"
#include "scroll_interaction_support.hpp"

namespace atlas::ui::tests
{
/// @brief Tab uses authored order then stable layout ties; Shift reverses without clicking.
TEST_F(focus_navigation_fixture, tab_order_and_shift_tab_share_logical_traversal)
{
    const auto a = button({0, 0});
    const auto b = button({20, 0});
    const auto c = button({40, 0});
    focus_navigation_options options;
    options.tab_order = -1;
    input.configure_navigation(c, options);
    input.configure_navigation();
    begin();
    input.process(std::array{key(input::key::tab)});
    EXPECT_EQ(input.focused(), c);
    input.process_more(std::array{key(input::key::tab)});
    EXPECT_EQ(input.focused(), a);
    input.process_more(std::array{key(input::key::shift), key(input::key::tab),
                                  key(input::key::shift, input_kind::key_up)});
    EXPECT_EQ(input.focused(), c);
    EXPECT_NE(input.focused(), b);
    finish();
}

/// @brief LB/RB-style semantic actions target their group while focus is in unrelated content.
TEST_F(focus_navigation_fixture, group_actions_reuse_last_tab_and_deliver_focus_events_only)
{
    const auto row = group({0, 0});
    const auto a = button({0, 0}, row);
    const auto b = button({20, 0}, row);
    const auto content = button({0, 50});
    input.configure_navigation_scope(row, {.actions = {.next = 71, .previous = 72}});
    begin();
    input.focus(a);
    input.focus(content);
    input.process(std::array{action(71)});
    EXPECT_EQ(input.focused(), b);
    EXPECT_GT(count(event_kind::focus_gained), 0U);
    input.process_more(std::array{action(72, input::action_phase::cancelled)});
    EXPECT_EQ(input.focused(), b);
    finish();
}

/// @brief User handlers may consume an event before the default policy moves focus.
TEST_F(focus_navigation_fixture, consumed_packets_do_not_move_focus)
{
    const auto a = button({0, 0});
    (void)button({20, 0});
    input.configure_navigation();
    handler->action = [](const routed_event& event, command_buffer&)
    {
        return event_response{.stop = event.kind == event_kind::key_down};
    };
    begin();
    input.focus(a);
    input.process(std::array{key(input::key::right)});
    EXPECT_EQ(input.focused(), a);
    finish();
}

/// @brief The default policy remains inactive until explicitly configured.
TEST_F(focus_navigation_fixture, ordinary_key_routes_keep_legacy_behavior_when_unconfigured)
{
    const auto a = button({0, 0});
    (void)button({20, 0});
    begin();
    input.focus(a);
    input.process(std::array{key(input::key::right)});
    EXPECT_EQ(input.focused(), a);
    finish();
}

/// @brief Spatial navigation shares existing scroll reveal for captured offscreen entries.
TEST_F(scroll_navigation_fixture, spatial_focus_reveals_offscreen_content)
{
    list();
    input.configure_navigation();
    open();
    input.focus(items.front());
    replay({{input_kind::key_down, {}, static_cast<unsigned>(input::key::down)},
            {input_kind::key_down, {}, static_cast<unsigned>(input::key::down)}});
    EXPECT_EQ(input.focused(), items[2]);
    EXPECT_EQ(position(), 40);
}

/// @brief Invalid/ambiguous configuration is rejected before replacing the working mappings.
TEST_F(focus_navigation_fixture, rejects_ambiguous_mapping_and_conflicting_destination)
{
    const auto a = button({0, 0});
    const auto b = button({20, 0});
    input.configure_navigation({.right = 10});
    EXPECT_THROW(input.configure_navigation({.right = 20, .next = 20}), std::invalid_argument);
    EXPECT_THROW(input.configure_navigation_scope(root, {.actions = {.next = 10}}),
                 std::invalid_argument);
    focus_navigation_options options;
    options.blocked[1] = true;
    options.destinations[1] = b;
    EXPECT_THROW(input.configure_navigation(a, options), std::invalid_argument);
    begin();
    input.focus(a);
    input.process(std::array{action(10)});
    EXPECT_EQ(input.focused(), b);
    finish();
}

/// @brief Large host-defined key IDs do not truncate into portable Tab identifiers.
TEST_F(focus_navigation_fixture, unknown_large_keys_do_not_trigger_navigation)
{
    const auto a = button({0, 0});
    (void)button({20, 0});
    input.configure_navigation();
    begin();
    input.focus(a);
    input_event event{input_kind::key_down};
    event.key = 65536 + static_cast<unsigned>(input::key::tab);
    input.process(std::array{event});
    EXPECT_EQ(input.focused(), a);
    finish();
}

/// @brief Losing activation releases Shift and focus before subsequent replay requests.
TEST_F(focus_navigation_fixture, window_loss_clears_modifier_state)
{
    const auto a = button({0, 0});
    (void)button({20, 0});
    input.configure_navigation();
    begin();
    input.process(std::array{key(input::key::shift), input_event{input_kind::window_blur},
                             key(input::key::tab)});
    EXPECT_EQ(input.focused(), a);
    finish();
}

/// @brief Directional wrap does not force ordered Tab traversal to wrap.
TEST_F(focus_navigation_fixture, tab_wrap_and_tab_stop_are_independent)
{
    const auto a = button({0, 0});
    const auto b = button({20, 0});
    const auto c = button({40, 0});
    focus_navigation_options skip;
    skip.tab_stop = false;
    input.configure_navigation(b, skip);
    input.configure_navigation_scope(root, {.wrap_tab = true});
    begin();
    input.focus(a);
    EXPECT_TRUE(input.navigate(focus_direction::next));
    EXPECT_EQ(input.focused(), c);
    EXPECT_TRUE(input.navigate(focus_direction::next));
    EXPECT_EQ(input.focused(), a);
    EXPECT_TRUE(input.navigate(focus_direction::right));
    EXPECT_EQ(input.focused(), b);
    input.process({});
    finish();
}

/// @brief Unbinding a handler clears its explicit relationships before a new registration reuses
/// it.
TEST_F(focus_navigation_fixture, rebinding_does_not_inherit_navigation_metadata)
{
    const auto a = button({0, 0});
    const auto b = button({20, 0});
    const auto c = button({40, 0});
    focus_navigation_options options;
    options.destinations[1] = c;
    input.configure_navigation(a, options);
    input.unbind(a);
    begin();
    input.process({});
    finish();
    input.bind(a, handler, {.focusable = true});
    begin(2);
    input.focus(a);
    EXPECT_TRUE(input.navigate(focus_direction::right));
    EXPECT_EQ(input.focused(), b);
    input.process({});
    finish();
}

/// @brief A focus-loss callback may remove the proposed target before it can receive focus.
TEST_F(focus_navigation_fixture, callback_removal_does_not_focus_a_dead_destination)
{
    const auto a = button({0, 0});
    const auto b = button({20, 0});
    handler->action = [&](const routed_event& event, command_buffer&)
    {
        if (event.kind == event_kind::focus_lost && event.current == a)
        {
            tree.remove(b);
        }

        return event_response{};
    };
    begin();
    input.focus(a);
    const auto before = count(event_kind::focus_gained);
    EXPECT_FALSE(input.navigate(focus_direction::right));
    EXPECT_EQ(input.focused(), element_handle{});
    EXPECT_EQ(count(event_kind::focus_gained), before);
    input.process({});
    finish();
}

/// @brief Removed group identities do not reserve semantic actions for later live configurations.
TEST_F(focus_navigation_fixture, removed_scopes_do_not_keep_action_ids_reserved)
{
    const auto row = group({0, 0});
    input.configure_navigation_scope(row, {.actions = {.next = 91}});
    tree.remove(row);
    EXPECT_NO_THROW(input.configure_navigation({.next = 91}, false));
}
} // namespace atlas::ui::tests
