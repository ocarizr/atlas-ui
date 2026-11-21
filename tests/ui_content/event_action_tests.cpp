/** @file @brief Semantic registry/Flow bindings survive XML/prepared content and real input. */
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui_content/prepared.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_content::tests
{
/// @brief Prepared v4 retains event/action IDs and every exact scalar argument type.
TEST(event_action_contract, PreservesExactScalarsAndRoutesInPreparedV4)
{
    const auto source = parse_xml({R"(<ui version="1"><element id="root">
        <interaction focusable="true"/><event-action event="click" action="project.buy" context="state" screen="home">
        <arg type="integer" value="2"/><arg type="number" value="1.5"/><arg type="boolean" value="true"/><arg type="text" value="hello"/>
        </event-action></element></ui>)"});
    const assets::asset_ref ref{{assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23"), 1},
                                assets::asset_type::ui};
    const prepared_view prepared{ref, source, {{ref, 1}}, {}, {}};
    const auto bytes = encode_prepared(prepared);
    EXPECT_EQ(std::to_integer<unsigned>(bytes.at(4)), 4U);
    const auto restored = decode_prepared(bytes).view;
    EXPECT_EQ(write_xml(restored), write_xml(source));
    ASSERT_EQ(restored.elements.front().events.size(), 1U);
    EXPECT_EQ(restored.elements.front().events.front().arguments[0], mvvm::value{std::int64_t{2}});
    EXPECT_EQ(restored.elements.front().events.front().arguments[1], mvvm::value{1.5});
}

/// @brief Unknown events, attributes and numeric coercion are rejected at the source boundary.
TEST(event_action_contract, RejectsMalformedEventWithoutDroppingAuthoredValues)
{
    EXPECT_THROW(
        (void)parse_xml(
            {R"(<ui version="1"><element id="root"><event-action event="hover" action="x"/></element></ui>)"}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)parse_xml(
            {R"(<ui version="1"><element id="root"><event-action event="click" action="x"><arg type="integer" value="1.5"/></event-action></element></ui>)"}),
        std::invalid_argument);
    EXPECT_THROW(
        (void)parse_xml(
            {R"(<ui version="1"><element id="root"><event-action event="click" action="x" extra="bad"/></element></ui>)"}),
        std::invalid_argument);
}

/// @brief Unpaired/outside/cancelled release cannot activate a semantic click binding.
TEST(event_action_contract, ClickRequiresPressAndInsideReleaseOrSemanticActivation)
{
    jobs::job_system jobs{1};
    const auto catalog =
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")});
    streaming::catalog_streamer storage{jobs, catalog, {}};
    auto source = parse_xml({R"(<ui version="1"><element id="root" width="100" height="100">
        <interaction focusable="true"/><event-action event="click" action="atlas.navigate_to" screen="home" route="open" input-action="7"/>
        </element></ui>)"});
    std::size_t calls = 0;
    construction options{storage};
    options.dispatch_event = [&](const auto& event)
    {
        EXPECT_EQ(event.route, "open");
        ++calls;
    };
    auto view = instance::build(source, options);
    ui::layout_engine layout{jobs.dispatcher()};
    const auto geometry = layout.update(view->tree(), {0, 0, 100, 100});
    auto& input = view->input();
    input.begin_frame(2, view->tree().snapshot(), geometry);
    const std::array events{
        ui::input_event{ui::input_kind::pointer_up, {10, 10}},
        ui::input_event{ui::input_kind::pointer_down, {10, 10}},
        ui::input_event{ui::input_kind::pointer_up, {120, 120}},
        ui::input_event{ui::input_kind::pointer_down, {10, 10}},
        ui::input_event{ui::input_kind::pointer_cancel},
        ui::input_event{ui::input_kind::pointer_up, {10, 10}},
        ui::input_event{ui::input_kind::pointer_down, {10, 10}},
        ui::input_event{ui::input_kind::pointer_up, {10, 10}},
        ui::input_event{
            .kind = ui::input_kind::action,
            .action = input::action_event{
                7, input::value_type::button, input::action_phase::performed, {1, 0}, 2}}};
    input.process(events);
    input.tick();
    input.late_tick();
    input.end_frame();
    EXPECT_EQ(calls, 2U);
}
} // namespace atlas::ui_content::tests
