/** @file @brief Shared controlled Flow loading and owner-frame test fixture. */
#pragma once
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui_content/flow.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::ui_content::tests
{
/// @brief Build stable references independent of source filenames.
inline assets::asset_ref asset(std::uint64_t ordinal,
                               assets::asset_type type = assets::asset_type::ui)
{
    return {{assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23"), ordinal}, type};
}

/// @brief Ordinary UI for both streamer and controlled loader exercises.
constexpr std::string_view screen_xml = R"(<ui version="1"><element id="button">
  <interaction enabled="true" hit-test="true" focusable="true"/>
</element></ui>)";

/// @brief A controllable load consumes the same composition route configuration as production.
enum class load_gate : std::uint8_t
{
    waiting, ///< Stay pending.
    ready,   ///< Publish the private composition.
    failed   ///< Simulate an I/O or composition failure.
};

/// @brief A controllable load consumes the same composition route configuration as production.
class controlled_load final : public flow_load
{
  public:
    controlled_load(const construction& options, flow_load_request request, const load_gate& gate)
        : options_{options}, request_{std::move(request)}, gate_{gate}
    {
    } ///< Borrow owner-thread fixture gates.

    /// @brief Publish only when the fixture releases the pending request.
    std::unique_ptr<instance> poll() override
    {
        if (gate_ == load_gate::waiting)
        {
            return {};
        }

        if (gate_ == load_gate::failed)
        {
            throw std::runtime_error("fixture rejected this load");
        }

        options_.navigate = request_.dispatch;
        options_.navigation = request_.controls;
        return instance::build(parse_xml({screen_xml}), options_);
    }

  private:
    construction options_;      ///< Shared typed fixture mount.
    flow_load_request request_; ///< Exact candidate and routing identity.
    const load_gate& gate_;     ///< Owner-controlled result, with no worker sleeps.
};

/// @brief A nested tab scope, modal overlay and back history in one tiny graph.
inline flow_document graph()
{
    flow_document result;
    result.regions = {{"main", "home", ""}, {"tabs", "first", "settings"}};
    result.screens = {{"home", "main", asset(1), "button"},
                      {"settings", "main", asset(2)},
                      {"modal", "main", asset(3)},
                      {"first", "tabs", asset(4)},
                      {"second", "tabs", asset(5)}};
    result.parameters = {{"allowed", true}};
    result.transitions = {
        {.id = "open",
         .from = "home",
         .to = "settings",
         .guard = flow_guard{"allowed", true},
         .control = "button"},
        {.id = "show", .from = "settings", .to = "modal", .operation = flow_operation::overlay},
        {.id = "close", .from = "modal", .operation = flow_operation::pop},
        {.id = "tab", .from = "first", .to = "second", .operation = flow_operation::replace},
        {.id = "back", .from = "second", .operation = flow_operation::back},
        {.id = "reload", .from = "home", .operation = flow_operation::reload},
        {.id = "disabled", .from = "home", .to = "modal", .enabled = false}};
    return result;
}

/// @brief Control readiness while retaining callbacks from old generations to try again later.
class flow_runtime_test : public testing::Test
{
  protected:
    /// @brief Generate independent candidate controls through the public loader seam.
    flow_loader loader()
    {
        return [this](flow_load_request request)
        {
            callbacks.push_back(request.dispatch);
            return std::make_unique<controlled_load>(construction{storage}, std::move(request),
                                                     gate);
        };
    }

    /// @brief Exercise focus and pointer routing through the existing input lifecycle.
    void render_frame(flow_manager& flow, instance& view, std::uint64_t frame,
                      std::span<const ui::input_event> input, bool select_focus = false)
    {
        ui::layout_engine layout{jobs.dispatcher()};
        const auto geometry = layout.update(view.tree(), {0, 0, 80, 60});
        view.begin_models(frame);
        view.input().begin_frame(frame, view.tree().snapshot(), geometry);
        flow.restore_focus(view);
        if (select_focus)
        {
            EXPECT_EQ(view.input().focused(), ui::element_handle{});
            view.input().focus(view.find("button"));
        }

        EXPECT_EQ(view.input().focused(), view.find("button"));
        view.input().process(input);
        view.input().tick();
        view.synchronize();
        view.input().late_tick();
        view.input().end_frame();
        view.tree().apply(view.tree().commands().seal());
    }

    jobs::job_system jobs{2}; ///< Keep shared workers alive through mount shutdown.
    streaming::catalog_streamer storage{
        jobs, assets::catalog::compile({asset(1).id.space}), {}}; ///< Empty mount for inline UI.
    load_gate gate = load_gate::ready;                            ///< Owner-controlled readiness.
    std::vector<flow_dispatch> callbacks; ///< Saved candidate tokens, even after retirement.
};
} // namespace atlas::ui_content::tests
