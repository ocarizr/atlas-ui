/** @file @brief Accessible editor controls use the real owner routes and persisted semantics. */
#include "panel_scene.hpp"
#include <algorithm>
#include <gtest/gtest.h>

namespace atlas::editor::tests
{
/// @brief Native editor controls expose meaningful names and semantic activation.
TEST(editor_accessibility, ChromeExposesNamedSemanticControls)
{
    jobs::job_system workers{1};
    ui::layout_engine layout{workers.dispatcher()};
    ui::paint_engine paint{workers.dispatcher()};
    ui::element_tree preview;
    (void)preview.create();
    panel_plan plan;
    plan.preview = {0, 40, 200, 100};
    plan.controls.push_back({{0, 0, 80, 30}, {1, 1, 1, 1}, "Save", {control_kind::save}});
    panel_scene chrome{plan, preview, {0, 0, 200, 140}, 1, layout, paint, {}};
    (void)chrome.begin_input({});
    const auto nodes = chrome.input().accessibility_nodes();
    ASSERT_EQ(nodes.size(), 1);
    EXPECT_EQ(nodes.front().name, "Save");
    EXPECT_TRUE(nodes.front().invoke);
    const accessibility::request request{nodes.front().id, accessibility::action::invoke};
    chrome.input().process_accessibility({&request, 1});
    const auto commands = chrome.finish_input();
    ASSERT_EQ(commands.size(), 1);
    EXPECT_EQ(commands.front().kind, control_kind::save);
}

/// @brief Exercise the same window-free view API used by the native composition root.
class editor_accessibility_test : public testing::Test
{
  protected:
    jobs::job_system workers{1}; ///< Joined after UI and storage release their borrowed services.
    /// @brief Asset-free typed mount, independent from application disk content.
    streaming::catalog_streamer storage{
        workers,
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")}),
        {}};
    document_session session{new_document(), {storage}};  ///< Per-test source and preview.
    editor_view view{session, workers, "unused.xml", {}}; ///< Uses the real snapshot/input path.
    /// @brief The native side receives only immutable values and bounded request admission.
    std::shared_ptr<accessibility::channel> channel = std::make_shared<accessibility::channel>();
    window::window_state state{960, 540,
                               1}; ///< Logical and physical extents agree in this fixture.

    void SetUp() override
    {
        view.bind_accessibility(channel);
        view.accessibility_coordinates({.x = -100, .y = 20, .scale = 2});
        (void)view.snapshot(state, {});
    } ///< Attach before the first frame exactly as the executable does.

    accessibility::node named(std::string_view name)
    {
        const auto snapshot = channel->read();
        const auto found = std::ranges::find(snapshot->nodes(), name, &accessibility::node::name);
        if (found == snapshot->nodes().end())
        {
            throw std::runtime_error("Missing accessible editor control");
        }

        return *found;
    } ///< Copy values without exposing a mutable editor object to native clients.
};

/// @brief Queued activation executes even when no physical input arrived in that frame.
TEST_F(editor_accessibility_test, NativeRequestUsesNormalCommandsWithoutPointerInput)
{
    const auto target = named("Add child");
    ASSERT_TRUE(channel->request({target.id, accessibility::action::invoke}));
    (void)view.snapshot(state, {});
    EXPECT_EQ(session.document().elements.size(), 3);
    EXPECT_FALSE(channel->request({target.id, accessibility::action::invoke}));
    EXPECT_TRUE(session.undo());
    EXPECT_EQ(session.document().elements.size(), 2);
}

/// @brief Logical focus survives chrome regeneration without aliasing its retired identities.
TEST_F(editor_accessibility_test, FocusSurvivesAnEditingRebuild)
{
    auto target = named("+ Width");
    ASSERT_TRUE(channel->request({target.id, accessibility::action::focus}));
    (void)view.snapshot(state, {});
    target = named("+ Width");
    EXPECT_TRUE(target.focused);
    ASSERT_TRUE(channel->request({target.id, accessibility::action::invoke}));
    (void)view.snapshot(state, {});
    const auto rebuilt = named("+ Width");
    EXPECT_NE(target.id, rebuilt.id);
    EXPECT_TRUE(rebuilt.focused);
    const auto stable = channel->read();
    (void)view.snapshot(state, {});
    EXPECT_EQ(stable, channel->read());
}
} // namespace atlas::editor::tests
