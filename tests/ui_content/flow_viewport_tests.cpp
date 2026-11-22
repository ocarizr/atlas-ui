/** @file @brief Real Atlas routing across clipped regions and popup presentation layers. */
#include "flow_viewport_fixture.hpp"

namespace atlas::ui_content::tests
{
/// @brief Wheel follows the pointer's modeless layer even while another layer owns pointer capture.
TEST_F(flow_viewport_test, WheelRoutesUnderPointerIndependentlyFromPopupCapture)
{
    documents.emplace(1, parse_xml({R"(<ui version="1">
      <element id="viewport" panel="scroll"/>
      <element id="content" parent="viewport" height="200"/>
    </ui>)"}));
    documents.emplace(2, parse_xml({R"(<ui version="1">
      <element id="root" width="20" height="20"/>
      <element id="viewport" parent="root" panel="scroll" width="10" height="20"/>
      <element id="content" parent="viewport" height="100"/>
      <element id="bar" parent="root" width="10" height="20">
        <layout horizontal="end"/>
        <scrollbar viewport="viewport" min-thumb="5"/>
      </element>
    </ui>)"}));
    flow_manager flow{popup_graph(false, false), typed_loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ASSERT_TRUE(flow.request("open"));
    ASSERT_EQ(flow.advance(3), flow_status::committed);
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    flow_viewport viewport{flow, layout, paint};
    const ui::input_event press{ui::input_kind::pointer_down, {15, 2}};
    (void)viewport.frame(4, {0, 0, 80, 60}, {&press, 1});
    const auto layers = flow.presentation();
    ASSERT_EQ(layers.size(), 2U);
    EXPECT_EQ(layers.back().view->input().captured(), layers.back().view->find("bar"));
    ui::input_event wheel{ui::input_kind::pointer_scroll, {60, 50}};
    wheel.scroll.y = 1;
    (void)viewport.frame(5, {0, 0, 80, 60}, {&wheel, 1});
    EXPECT_EQ(std::get<ui::scroll_layout>(
                  layers.front().view->tree().panel(layers.front().view->find("viewport")))
                  .offset.y,
              40);
}

/// @brief Replayed input produces a real navigation request only after closed UI phases.
TEST_F(flow_viewport_test, FramesCloseBeforeNavigationAndReplayUsesRealInput)
{
    flow_manager flow{graph(), loader()};
    flow.start();
    EXPECT_EQ(flow.advance(2), flow_status::committed);
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    flow_viewport viewport{flow, layout, paint};
    const std::array click{ui::input_event{ui::input_kind::pointer_down, {10, 10}},
                           ui::input_event{ui::input_kind::pointer_up, {10, 10}}};
    (void)viewport.frame(3, {0, 0, 80, 60}, click);
    EXPECT_TRUE(flow.presentation().front().view->input().statistics().closed);
    EXPECT_EQ(flow.advance(4), flow_status::committed);
    EXPECT_EQ(flow.presentation().front().screen, "settings");
}

/// @brief Modeless overlays preserve lower route eligibility without duplicating input.
TEST_F(flow_viewport_test, ModelessPresentationKeepsLowerNavigationEligible)
{
    auto source = popup_graph(false, false);
    flow_manager flow{source, typed_loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ASSERT_TRUE(flow.request("open"));
    ASSERT_EQ(flow.advance(3), flow_status::committed);
    EXPECT_TRUE(flow.presentation().front().input);
    const auto before = flow.presentation();
    ASSERT_TRUE(flow.request("reload"));
    EXPECT_EQ(flow.advance(4), flow_status::committed);
    const auto after = flow.presentation();
    ASSERT_EQ(after.size(), 2U);
    EXPECT_NE(after.front().view, before.front().view);
    EXPECT_EQ(after.back().view, before.back().view);
}

/// @brief Dismissal consumes its release even after the popup has been retired.
TEST_F(flow_viewport_test, OutsideDismissConsumesPressAndReleaseAcrossPublication)
{
    auto source = popup_graph();
    source.transitions.back().control = "button";
    auto popup = parse_xml({screen_xml});
    popup.elements.front().layout.constraints.minimum = {20, 20};
    popup.elements.front().layout.constraints.maximum = {20, 20};
    documents.emplace(2, popup);
    flow_manager flow{source, typed_loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ASSERT_TRUE(flow.request("open"));
    ASSERT_EQ(flow.advance(3), flow_status::committed);
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    flow_viewport viewport{flow, layout, paint};
    const ui::input_event down{ui::input_kind::pointer_down, {60, 50}};
    (void)viewport.frame(4, {0, 0, 80, 60}, {&down, 1});
    EXPECT_EQ(flow.advance(5), flow_status::committed);
    ASSERT_EQ(flow.presentation().size(), 1U);
    const ui::input_event up{ui::input_kind::pointer_up, {60, 50}};
    (void)viewport.frame(6, {0, 0, 80, 60}, {&up, 1});
    EXPECT_EQ(flow.advance(7), flow_status::idle);
}

/// @brief Pointer selection transfers keyboard ownership to exactly one modeless layer.
TEST_F(flow_viewport_test, ModelessOutsidePointerNavigatesLowerViewWithExclusiveKeyboardFocus)
{
    auto source = popup_graph(false, false);
    source.screens.front().focus = "button";
    source.screens.back().focus = "button";
    source.transitions.back().control = "button";
    auto popup = parse_xml({screen_xml});
    popup.elements.front().layout.constraints.minimum = {20, 20};
    popup.elements.front().layout.constraints.maximum = {20, 20};
    documents.emplace(2, popup);
    flow_manager flow{source, typed_loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ASSERT_TRUE(flow.request("open"));
    ASSERT_EQ(flow.advance(3), flow_status::committed);
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    flow_viewport viewport{flow, layout, paint};
    (void)viewport.frame(4, {0, 0, 80, 60});
    auto layers = flow.presentation();
    EXPECT_EQ(layers.front().view->input().focused(), ui::element_handle{});
    EXPECT_EQ(layers.back().view->input().focused(), layers.back().view->find("button"));
    const std::array click{ui::input_event{ui::input_kind::pointer_down, {60, 50}},
                           ui::input_event{ui::input_kind::pointer_up, {60, 50}}};
    (void)viewport.frame(5, {0, 0, 80, 60}, click);
    EXPECT_EQ(layers.back().view->input().focused(), ui::element_handle{});
    EXPECT_EQ(flow.advance(6), flow_status::committed);
}

/// @brief A child region inherits the host's mapped rectangle and cannot paint outside it.
TEST_F(flow_viewport_test, NestedRegionUsesParentHostBoundsAndClipsPaint)
{
    flow_document source;
    source.regions = {{"main", "home", ""}, {"tabs", "child", "home", "host"}};
    source.screens = {{"home", "main", asset(1, assets::asset_type::menu)},
                      {"child", "tabs", asset(2, assets::asset_type::submenu)}};
    auto parent = parse_xml({R"(<ui version="1"><element id="root" panel="canvas"/>
        <element id="host" parent="root"><canvas-slot x-offset-min="10" y-offset-min="5"/></element></ui>)"});
    parent.elements.back().layout.constraints.minimum = {40, 30};
    parent.elements.back().layout.constraints.maximum = {40, 30};
    parent.elements.front().paint.translation = {3, 2};
    parent.elements.back().paint.translation = {7, 3};
    parent.elements.front().requested.opacity = 0.5F;
    documents.emplace(1, parent);
    documents.emplace(
        2,
        parse_xml({R"(<ui version="1"><element id="root"><paint fill="true"/></element></ui>)"}));
    flow_manager flow{source, typed_loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    flow_viewport viewport{flow, layout, paint};
    const auto quads = viewport.frame(3, {0, 0, 100, 80});
    ASSERT_EQ(quads.size(), 1U);
    EXPECT_EQ(quads.front().clip, (ui::layout_rect{20, 10, 40, 30}));
    EXPECT_EQ(quads.front().bounds, (ui::layout_rect{20, 10, 40, 30}));
    EXPECT_FLOAT_EQ(quads.front().vertices.front().color[3], 0.5F);
}

/// @brief Closing a modeless popup restores the lower screen's saved focus without a new click.
TEST_F(flow_viewport_test, BackClosesPopupAndRestoresFocusWithoutLeakingKey)
{
    auto source = popup_graph(false, false);
    source.screens.front().focus = "button";
    source.screens.back().focus = "button";
    flow_manager flow{source, typed_loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    flow_viewport viewport{flow, layout, paint};
    (void)viewport.frame(3, {0, 0, 80, 60});
    ASSERT_TRUE(flow.request("open"));
    ASSERT_EQ(flow.advance(4), flow_status::committed);
    const ui::input_event back{ui::input_kind::key_down, {}, 27};
    (void)viewport.frame(5, {0, 0, 80, 60}, {&back, 1});
    ASSERT_EQ(flow.advance(6), flow_status::committed);
    (void)viewport.frame(7, {0, 0, 80, 60});
    auto* view = flow.presentation().front().view;
    EXPECT_EQ(view->input().focused(), view->find("button"));
    EXPECT_EQ(flow.advance(8), flow_status::idle);
}

/// @brief Invalid replay cannot queue navigation before the malformed suffix is discovered.
TEST_F(flow_viewport_test, InvalidReplayPreflightPreventsPartialNavigation)
{
    flow_manager flow{graph(), loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    flow_viewport viewport{flow, layout, paint};
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- Deliberate corrupt replay.
    const auto invalid_kind = static_cast<ui::input_kind>(255);
    const std::array events{ui::input_event{ui::input_kind::pointer_down, {5, 5}},
                            ui::input_event{ui::input_kind::pointer_up, {5, 5}},
                            ui::input_event{invalid_kind}};
    EXPECT_THROW((void)viewport.frame(3, {0, 0, 80, 60}, events), std::invalid_argument);
    EXPECT_EQ(flow.advance(5), flow_status::idle);
    EXPECT_EQ(flow.presentation().front().screen, "home");
}

/// @brief Clipping a host out of presentation also removes its child's keyboard ownership.
TEST_F(flow_viewport_test, HiddenHostCannotReceiveSemanticActivationThroughChildFocus)
{
    flow_document source;
    source.regions = {{"main", "home", ""}, {"tabs", "child", "home", "button"}};
    source.screens = {{"home", "main", asset(1, assets::asset_type::menu)},
                      {"child", "tabs", asset(2, assets::asset_type::submenu), "button"}};
    source.transitions = {{"reload", "child", {}, flow_operation::reload, {}, true, "button", 7}};
    auto parent = parse_xml({screen_xml});
    parent.elements.front().requested.visible = false;
    documents.emplace(1, parent);
    flow_manager flow{source, typed_loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    flow_viewport viewport{flow, layout, paint};
    const ui::input_event action{
        .kind = ui::input_kind::action,
        .action =
            input::action_event{.action = 7, .phase = input::action_phase::performed, .frame = 3}};
    (void)viewport.frame(3, {0, 0, 80, 60}, {&action, 1});
    EXPECT_EQ(flow.advance(4), flow_status::idle);
    EXPECT_EQ(flow.presentation().back().view->input().focused(), ui::element_handle{});
}

/// @brief Dismissal uses the same visual transform as paint and the popup's own hit testing.
TEST_F(flow_viewport_test, TranslatedPopupDoesNotTreatItsVisibleBodyAsAnOutsideClick)
{
    auto source = popup_graph();
    auto popup = parse_xml({screen_xml});
    popup.elements.front().layout.constraints.minimum = {20, 20};
    popup.elements.front().layout.constraints.maximum = {20, 20};
    popup.elements.front().paint.translation = {30, 0};
    documents.emplace(2, popup);
    flow_manager flow{source, typed_loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    ASSERT_TRUE(flow.request("open"));
    ASSERT_EQ(flow.advance(3), flow_status::committed);
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    flow_viewport viewport{flow, layout, paint};
    const std::array click{ui::input_event{ui::input_kind::pointer_down, {40, 5}},
                           ui::input_event{ui::input_kind::pointer_up, {40, 5}}};
    (void)viewport.frame(4, {0, 0, 80, 60}, click);
    EXPECT_EQ(flow.advance(5), flow_status::idle);
    ASSERT_EQ(flow.presentation().size(), 2U);
    const ui::input_event outside{ui::input_kind::pointer_down, {5, 5}};
    (void)viewport.frame(6, {0, 0, 80, 60}, {&outside, 1});
    EXPECT_EQ(flow.advance(7), flow_status::committed);
    EXPECT_EQ(flow.presentation().size(), 1U);
}
} // namespace atlas::ui_content::tests
