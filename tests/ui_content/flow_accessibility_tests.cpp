/** @file @brief Runtime Flow publication reuses active-layer ownership and owner localization. */
#include "flow_viewport_fixture.hpp"

namespace atlas::ui_content::tests
{
/// @brief A modal popup removes background domains from the window's semantic publication.
TEST_F(flow_viewport_test, AccessibilityPublishesOnlyModalForegroundViews)
{
    documents.emplace(
        1,
        parse_xml(
            {R"(<ui version="1"><element id="root" width="80" height="60"><accessibility name="Main"/></element></ui>)"}));
    documents.emplace(
        2,
        parse_xml(
            {R"(<ui version="1"><element id="root" width="20" height="20"><accessibility name="Popup"/></element></ui>)"}));
    flow_manager flow{popup_graph(), typed_loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    flow_viewport viewport{flow, layout, paint};
    accessibility::channel channel;
    ui::accessibility_frame first{channel};
    (void)viewport.frame(3, {0, 0, 80, 60}, {}, {.accessibility = &first});
    first.publish();
    ASSERT_EQ(channel.read()->nodes().size(), 1);
    EXPECT_EQ(channel.read()->nodes().front().name, "Main");
    ASSERT_TRUE(flow.request("open"));
    ASSERT_EQ(flow.advance(4), flow_status::committed);
    ui::accessibility_frame second{channel};
    (void)viewport.frame(5, {0, 0, 80, 60}, {}, {.accessibility = &second});
    second.publish();
    ASSERT_EQ(channel.read()->nodes().size(), 1);
    EXPECT_EQ(channel.read()->nodes().front().name, "Popup");
}

/// @brief Language changes replace resolved values without changing asset or live node identity.
TEST_F(flow_viewport_test, AccessibilityResolvesLanguageOnTheOwnerOnly)
{
    documents.emplace(
        1,
        parse_xml(
            {R"(<ui version="1"><element id="root" width="80" height="60"><accessibility name-key="title"/></element></ui>)"}));
    flow_manager flow{popup_graph(), typed_loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    flow_viewport viewport{flow, layout, paint};
    accessibility::channel channel;
    std::string language = "Settings";
    unsigned resolutions = 0;
    const auto resolve = [&](std::string_view key)
    {
        EXPECT_EQ(key, "title");
        ++resolutions;
        return language;
    };
    ui::accessibility_frame first{channel};
    (void)viewport.frame(3, {0, 0, 80, 60}, {},
                         {.accessibility = &first, .accessible_text = resolve});
    first.publish();
    const auto previous = channel.read();
    ASSERT_EQ(previous->nodes().size(), 1);
    language = "Configuracoes";
    ui::accessibility_frame second{channel};
    (void)viewport.frame(4, {0, 0, 80, 60}, {},
                         {.accessibility = &second, .accessible_text = resolve});
    second.publish();
    EXPECT_EQ(previous->nodes().front().id, channel.read()->nodes().front().id);
    EXPECT_EQ(previous->nodes().front().name, "Settings");
    EXPECT_EQ(channel.read()->nodes().front().name, language);
    EXPECT_EQ(resolutions, 2);
}
} // namespace atlas::ui_content::tests
