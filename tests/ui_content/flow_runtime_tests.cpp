/** @file @brief Controlled asynchronous Flow transitions, typed streaming and focus. */
#include "flow_runtime_fixture.hpp"
#include <atlas/ui_content/flow.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::ui_content::tests
{

/// @brief Execute a generated path through a real pointer binding, tabs and modal history.
TEST_F(flow_runtime_test, PublishesNestedTabsOverlayAndBackWithRestoredFocus)
{
    auto authored = graph();
    authored.screens.front().focus.clear();
    const auto path = plan_flow_path(authored, "home", "second");
    ASSERT_EQ(path, (std::vector<std::string>{"open", "tab"}));
    flow_manager flow{authored, loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    auto items = flow.presentation();
    ASSERT_EQ(items.size(), 1U);
    const std::array click{ui::input_event{ui::input_kind::pointer_down, {2, 2}},
                           ui::input_event{ui::input_kind::pointer_up, {2, 2}}};
    render_frame(flow, *items.front().view, 2, click, true);
    ASSERT_EQ(flow.advance(3), flow_status::committed);
    EXPECT_FALSE(callbacks.front()("open"));
    items = flow.presentation();
    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(items.back().screen, "first");
    ASSERT_TRUE(flow.request(path.back()));
    ASSERT_EQ(flow.advance(4), flow_status::committed);
    EXPECT_EQ(flow.presentation().back().screen, "second");

    ASSERT_TRUE(flow.request("show"));
    ASSERT_EQ(flow.advance(5), flow_status::committed);
    items = flow.presentation();
    ASSERT_EQ(items.size(), 3U);
    EXPECT_FALSE(items.front().input);
    EXPECT_FALSE(items.at(1).input);
    EXPECT_TRUE(items.back().input);
    ASSERT_TRUE(flow.request("close"));
    ASSERT_EQ(flow.advance(6), flow_status::committed);
    EXPECT_EQ(flow.presentation().back().screen, "second");

    ASSERT_TRUE(flow.request("back"));
    ASSERT_EQ(flow.advance(7), flow_status::committed);
    items = flow.presentation();
    ASSERT_EQ(items.size(), 1U);
    EXPECT_EQ(items.front().screen, "home");
    render_frame(flow, *items.front().view, 7, {});
    EXPECT_NO_THROW(flow.shutdown(8));
    EXPECT_FALSE(callbacks.back()("open"));
}

/// @brief A focused control queues only the authored semantic action and its performed phase.
TEST_F(flow_runtime_test, RoutesPerformedSemanticActionWithoutPointerInput)
{
    auto authored = graph();
    authored.transitions.front().action = 42;
    flow_manager flow{authored, loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    const auto event = [](input::action_phase phase, std::uint64_t frame)
    {
        ui::input_event result;
        result.kind = ui::input_kind::action;
        result.action = input::action_event{.action = 42, .phase = phase, .frame = frame};
        return result;
    };
    const std::array started{event(input::action_phase::started, 2)};
    render_frame(flow, *flow.presentation().front().view, 2, started);
    EXPECT_EQ(flow.advance(3), flow_status::idle);
    const std::array performed{event(input::action_phase::performed, 3)};
    render_frame(flow, *flow.presentation().front().view, 3, performed);
    ASSERT_EQ(flow.advance(4), flow_status::committed);
    EXPECT_EQ(flow.presentation().front().screen, "settings");
    flow.shutdown(5);
}

/// @brief Failure, guard changes and abandoned generations preserve the old screen.
TEST_F(flow_runtime_test, FailedCancelledAndStaleLoadsLeaveCurrentScreen)
{
    flow_manager flow{graph(), loader()};
    flow.start();
    ASSERT_EQ(flow.advance(2), flow_status::committed);
    const auto prior = flow.presentation().front().view->find("button");
    flow.set_parameter("allowed", false);
    ASSERT_TRUE(flow.request("open"));
    EXPECT_EQ(flow.advance(3), flow_status::rejected);
    ASSERT_TRUE(flow.request("disabled"));
    EXPECT_EQ(flow.advance(4), flow_status::rejected);
    EXPECT_THROW(flow.set_parameter("allowed", std::int64_t{1}), std::invalid_argument);
    flow.set_parameter("allowed", true);
    gate = load_gate::waiting;
    ASSERT_TRUE(flow.request("open"));
    EXPECT_EQ(flow.advance(5), flow_status::loading);
    flow.set_parameter("allowed", false);
    EXPECT_EQ(flow.advance(6), flow_status::rejected);
    EXPECT_FALSE(callbacks.back()("tab"));
    flow.set_parameter("allowed", true);
    ASSERT_TRUE(flow.request("open"));
    EXPECT_EQ(flow.advance(7), flow_status::loading);
    flow.cancel();
    EXPECT_FALSE(callbacks.back()("tab"));
    EXPECT_EQ(flow.advance(8), flow_status::idle);
    gate = load_gate::failed;
    ASSERT_TRUE(flow.request("open"));
    EXPECT_EQ(flow.advance(9), flow_status::failed);
    EXPECT_TRUE(flow.failure());
    EXPECT_EQ(flow.presentation().front().view->find("button"), prior);
    gate = load_gate::ready;
    ASSERT_TRUE(flow.request("reload"));
    EXPECT_EQ(flow.advance(10), flow_status::committed);
    EXPECT_NE(flow.presentation().front().view->find("button"), prior);
    EXPECT_FALSE(callbacks.front()("open"));
    flow.shutdown(11);
}

/// @brief Use actual nonblocking typed reads for document and ViewModel bootstrap.
TEST(menu_flow, StreamerPreparesViewAndModelBeforeOwnerPublication)
{
    const auto screen = asset(1);
    const auto model = asset(2, assets::asset_type::view_model);
    const auto screen_source = "<ui version='1'><model name='hud' asset='" +
                               assets::format_id(model.id) + "'/><element id='root'/></ui>";
    const std::string model_source =
        "<ViewModel version='1' name='hud'><Fields>"
        "<Field name='xp' type='integer' value='7'/></Fields></ViewModel>";
    const auto catalog =
        assets::catalog::compile({screen.id.space,
                                  3,
                                  {{screen, {"fixture", "screen.xml"}, 1, screen_source.size()},
                                   {model, {"fixture", "model.xml"}, 1, model_source.size()}}});
    streaming::storage_mount backing;
    backing.sources.emplace("screen",
                            streaming::memory_asset{std::make_shared<const streaming::asset_bytes>(
                                screen_source.begin(), screen_source.end())});
    backing.sources.emplace("model",
                            streaming::memory_asset{std::make_shared<const streaming::asset_bytes>(
                                model_source.begin(), model_source.end())});
    backing.routes = {{screen, 1, "screen", 0}, {model, 1, "model", 0}};
    jobs::job_system jobs{2};
    streaming::catalog_streamer storage{jobs, catalog, std::move(backing)};
    flow_manager flow{{{{"main", "home", ""}}, {{"home", "main", screen}}, {}, {}},
                      make_flow_loader({storage})};
    flow.start();
    std::uint64_t frame = 2;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    auto status = flow.advance(frame++);
    EXPECT_EQ(status, flow_status::loading);
    while (status == flow_status::loading && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
        status = flow.advance(frame++);
    }

    ASSERT_EQ(status, flow_status::committed);
    const auto visible = flow.presentation();
    ASSERT_EQ(visible.size(), 1U);
    auto& view_model = visible.front().view->model("hud");
    EXPECT_EQ(view_model.integer(view_model.description().field("xp")), 7);
    flow.shutdown(frame);
}
} // namespace atlas::ui_content::tests
