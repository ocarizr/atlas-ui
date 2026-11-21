/** @file @brief Modeless popup input cannot redirect history operations to another screen. */
#include "flow_viewport_fixture.hpp"

namespace atlas::ui_content::tests
{
/// @brief Opening a new menu from below a modeless popup retires its presentation scope.
TEST_F(flow_viewport_test, LowerMenuNavigationDoesNotRetainPopupInReturnHistory)
{
    auto source = popup_graph(false, false);
    source.screens.push_back({"next", "main", asset(3)});
    source.transitions.push_back({"next", "home", "next"});
    source.transitions.push_back({"back", "next", {}, flow_operation::back});
    flow_manager flow{source, typed_loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ASSERT_TRUE(flow.request("open"));
    ASSERT_EQ(flow.advance(3), flow_status::committed);
    ASSERT_TRUE(flow.request("next"));
    ASSERT_EQ(flow.advance(4), flow_status::committed);
    EXPECT_EQ(flow.history().front().second, (std::vector<std::string>{"home", "next"}));
    ASSERT_TRUE(flow.request("back"));
    ASSERT_EQ(flow.advance(5), flow_status::committed);
    ASSERT_EQ(flow.presentation().size(), 1U);
    EXPECT_EQ(flow.presentation().front().screen, "home");
}

/// @brief A scoped menu exit uses the menu's history rather than treating its popup as the origin.
TEST_F(flow_viewport_test, LowerMenuExitRetiresModelessPopupsAndReturnsToPreviousMenu)
{
    auto source = popup_graph(false, false);
    source.screens.push_back({"settings", "main", asset(3)});
    source.transitions.front().from = "settings";
    source.transitions.push_back({"settings", "home", "settings"});
    flow_manager flow{source, typed_loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ASSERT_TRUE(flow.request("settings"));
    ASSERT_EQ(flow.advance(3), flow_status::committed);
    ASSERT_TRUE(flow.request("open"));
    ASSERT_EQ(flow.advance(4), flow_status::committed);
    ASSERT_TRUE(flow.exit_menu("settings"));
    ASSERT_EQ(flow.advance(5), flow_status::committed);
    ASSERT_EQ(flow.presentation().size(), 1U);
    EXPECT_EQ(flow.presentation().front().screen, "home");
}
} // namespace atlas::ui_content::tests
