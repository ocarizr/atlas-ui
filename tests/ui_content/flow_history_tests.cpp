/** @file @brief Scoped parent exits, remembered tabs and failed host publication. */
#include "flow_runtime_fixture.hpp"

namespace atlas::ui_content::tests
{
/// @brief A menu return skips its tab stack and never interprets it as parent history.
TEST_F(flow_runtime_test, ScopedMenuExitSkipsSubmenuHistory)
{
    auto authored = graph();
    authored.transitions.at(3).operation = flow_operation::push;
    flow_manager flow{authored, loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ASSERT_TRUE(flow.request("open"));
    ASSERT_EQ(flow.advance(3), flow_status::committed);
    ASSERT_TRUE(flow.request("tab"));
    ASSERT_EQ(flow.advance(4), flow_status::committed);
    ASSERT_TRUE(flow.exit_menu("second"));
    ASSERT_EQ(flow.advance(5), flow_status::committed);
    ASSERT_EQ(flow.presentation().size(), 1U);
    EXPECT_EQ(flow.presentation().front().screen, "home");
    ASSERT_TRUE(flow.exit_menu("home"));
    EXPECT_EQ(flow.advance(6), flow_status::rejected);
    EXPECT_EQ(flow.presentation().front().screen, "home");
}

/// @brief Returning to an existing menu restores remembered tabs with newly composed views.
TEST_F(flow_runtime_test, RestoresRememberedSubmenuOnParentHistoryReturn)
{
    auto authored = graph();
    authored.regions.at(1).remember = true;
    authored.transitions.push_back({.id = "deeper", .from = "settings", .to = "modal"});
    flow_manager flow{authored, loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ASSERT_TRUE(flow.request("open"));
    ASSERT_EQ(flow.advance(3), flow_status::committed);
    ASSERT_TRUE(flow.request("tab"));
    ASSERT_EQ(flow.advance(4), flow_status::committed);
    const auto old_callback = callbacks.back();
    ASSERT_TRUE(flow.request("deeper"));
    ASSERT_EQ(flow.advance(5), flow_status::committed);
    ASSERT_TRUE(flow.request("close"));
    ASSERT_EQ(flow.advance(6), flow_status::committed);
    ASSERT_EQ(flow.presentation().size(), 2U);
    EXPECT_EQ(flow.presentation().back().screen, "second");
    EXPECT_FALSE(old_callback("back"));
}

/// @brief Missing submenu hosts reject the entire private candidate rather than losing the menu.
TEST_F(flow_runtime_test, RejectsMissingHostWithoutReplacingPublishedState)
{
    auto authored = graph();
    authored.regions.at(1).host = "missing";
    flow_manager flow{authored, loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ASSERT_TRUE(flow.request("open"));
    EXPECT_EQ(flow.advance(3), flow_status::failed);
    ASSERT_EQ(flow.presentation().size(), 1U);
    EXPECT_EQ(flow.presentation().front().screen, "home");
    EXPECT_TRUE(flow.failure());
}
} // namespace atlas::ui_content::tests
