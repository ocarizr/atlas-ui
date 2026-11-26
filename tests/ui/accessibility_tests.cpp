/** @file @brief Owner-phase accessibility shares input lifetime and activation semantics. */
#include "input_test_support.hpp"

namespace atlas::ui::tests
{
/// @brief Verify accessibility reads the presented bounds and does not activate on focus.
TEST_F(input_fixture, AccessibilityReadsThePresentedBoundsAndDoesNotActivateOnFocus)
{
    const auto target = tree.create();
    tree.set_layout(target, {.intrinsic = {30, 20}});
    input.bind(target, handler, {.focusable = true});
    input.configure_accessibility(
        target, {.role = accessibility::role::button, .name = "Resume", .invoke_action = 42});
    begin(1);
    auto nodes = input.accessibility_nodes({.x = -100, .y = 20, .scale = 2});
    ASSERT_EQ(nodes.size(), 1);
    EXPECT_EQ(nodes[0].name, "Resume");
    const auto id = nodes[0].id;
    const accessibility::request request{id, accessibility::action::focus};
    input.process_accessibility(std::span{&request, 1});
    EXPECT_EQ(input.focused(), target);
    EXPECT_EQ(count(event_kind::action), 0);
}

/// @brief Verify accessibility invoke uses the existing semantic action.
TEST_F(input_fixture, AccessibilityInvokeUsesTheExistingSemanticAction)
{
    const auto target = tree.create();
    input.bind(target, handler, {.focusable = true});
    input.configure_accessibility(
        target, {.role = accessibility::role::button, .name = "Resume", .invoke_action = 42});
    begin(1);
    const accessibility::request request{{target.index, target.generation, target.session},
                                         accessibility::action::invoke};
    input.process_accessibility(std::span{&request, 1});
    EXPECT_EQ(count(event_kind::action), 1);
    EXPECT_EQ(input.focused(), element_handle{});
}

/// @brief Verify accessibility rejects removed and disabled targets.
TEST_F(input_fixture, AccessibilityRejectsRemovedAndDisabledTargets)
{
    const auto target = tree.create();
    input.bind(target, handler, {.enabled = false, .focusable = true});
    input.configure_accessibility(
        target, {.role = accessibility::role::button, .name = "Resume", .invoke_action = 42});
    begin(1);
    const accessibility::request request{{target.index, target.generation, target.session},
                                         accessibility::action::invoke};
    EXPECT_FALSE(input.accessibility_nodes().front().enabled);
    input.process_accessibility(std::span{&request, 1});
    EXPECT_EQ(count(event_kind::action), 0);
    tree.remove(target);
    input.process_accessibility(std::span{&request, 1});
    EXPECT_TRUE(input.accessibility_nodes().empty());
    EXPECT_EQ(count(event_kind::action), 0);
}

/// @brief Verify accessibility localizes names and flattens decorative parents.
TEST_F(input_fixture, AccessibilityLocalizesNamesAndFlattensDecorativeParents)
{
    const auto outer = tree.create();
    const auto decoration = tree.create(outer);
    const auto child = tree.create(decoration);
    input.configure_accessibility(outer, {.name = "Settings"});
    input.configure_accessibility(child,
                                  {.role = accessibility::role::text, .name_key = "audio.title"});
    begin(1);
    EXPECT_THROW((void)input.accessibility_nodes(), std::invalid_argument);
    const auto nodes = input.accessibility_nodes({},
                                                 [](std::string_view key)
                                                 {
                                                     return key == "audio.title" ? "Audio" : "";
                                                 });
    ASSERT_EQ(nodes.size(), 2);
    EXPECT_EQ(nodes[1].parent, nodes[0].id);
    EXPECT_EQ(nodes[1].name, "Audio");
}

/// @brief Verify accessibility modal exposure also restricts queued actions.
TEST_F(input_fixture, AccessibilityModalExposureAlsoRestrictsQueuedActions)
{
    const auto background = tree.create();
    const auto modal = tree.create();
    input.bind(background, handler, {.focusable = true});
    input.bind(modal, handler, {.focusable = true});
    input.configure_accessibility(
        background, {.role = accessibility::role::button, .name = "Behind", .invoke_action = 42});
    input.configure_accessibility(modal, {.name = "Popup"});
    begin(1);
    const auto nodes = input.accessibility_nodes({.modal_root = modal});
    ASSERT_EQ(nodes.size(), 1);
    const accessibility::request request{
        {background.index, background.generation, background.session},
        accessibility::action::invoke};
    input.process_accessibility(std::span{&request, 1}, modal);
    EXPECT_EQ(count(event_kind::action), 0);
}

/// @brief Verify accessibility rejects ambiguous names and invalid screen scale.
TEST_F(input_fixture, AccessibilityRejectsAmbiguousNamesAndInvalidScreenScale)
{
    const auto target = tree.create();
    EXPECT_THROW(input.configure_accessibility(target, {.name = "Name", .name_key = "name"}),
                 std::invalid_argument);
    input.configure_accessibility(target, {.name = "Name"});
    begin(1);
    EXPECT_THROW((void)input.accessibility_nodes({.scale = 0}), std::invalid_argument);
}

/// @brief Semantic buttons can activate without an artificial physical input action identifier.
TEST_F(input_fixture, AccessibilityButtonWithoutAnInputMappingUsesSemanticActivation)
{
    const auto target = tree.create();
    input.bind(target, handler, {.focusable = true});
    input.configure_accessibility(target, {.role = accessibility::role::button, .name = "Resume"});
    begin(1);
    const accessibility::request request{{target.index, target.generation, target.session},
                                         accessibility::action::invoke};
    input.process_accessibility(std::span{&request, 1});
    EXPECT_EQ(count(event_kind::activate), 1);
    EXPECT_EQ(count(event_kind::pointer_up), 0);
}
} // namespace atlas::ui::tests
