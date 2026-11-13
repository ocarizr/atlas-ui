/** @file @brief Verify that animated presentation and deliberate reflow share the owner pipeline.
 */
#include "composition_fixture.hpp"
#include <atlas/ui/paint_engine.hpp>

namespace atlas::ui_content::tests
{
namespace
{
using namespace std::chrono_literals;

/// @brief A fixed mask around a nested moving drawer with an interactive painted leaf.
document drawer()
{
    return parse_xml({R"xml(<ui version="1">
      <element id="mask" panel="canvas"><paint clip="true"/></element>
      <element id="drawer" parent="mask" panel="canvas" width="80" height="40"/>
      <element id="item" parent="drawer" panel="leaf" width="40" height="20">
        <paint fill="true"/>
        <interaction focusable="true"/>
      </element>
    </ui>)xml"});
}

/// @brief Publish the pose before presentation capture, in its own owner command frame.
void apply_pose(instance& view, std::uint64_t frame)
{
    auto& tree = view.tree();
    tree.commands().begin_frame(frame);
    view.publish_animation();
    tree.apply(tree.commands().seal());
}

/// @brief Share a toggle intent between pointer input and a device-independent action.
class drawer_toggle final : public ui::input_handler
{
  public:
    /// @brief Stash a demand for the owner to apply at the next synchronization point.
    ui::event_response on_event(const ui::routed_event& event, ui::command_buffer&) override
    {
        if (event.kind == ui::event_kind::pointer_up || event.kind == ui::event_kind::action)
        {
            open = !open;
        }

        return {};
    }

    bool open = false; ///< Current requested endpoint.
};
} // namespace

/// @brief The same clip travels in both directions and changes neither geometry nor the mask.
TEST_F(composition_fixture, DrawerTranslationMovesPaintAndInputWithoutLayout)
{
    auto view = instance::build(drawer(), {*storage});
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    const ui::layout_rect viewport{0, 0, 120, 80};
    const auto item = view->find("item");
    const auto original = layout.update(view->tree(), viewport);
    view->play_animation({100ms,
                          {{"drawer", ui::property::translate_x, {{0ms, -40}, {100ms, 0}}},
                           {"drawer", ui::property::translate_y, {{0ms, 10}, {100ms, 0}}}}});
    const std::array positions{50ms, 100ms, 50ms};
    for (std::size_t index = 0; index < positions.size(); ++index)
    {
        view->seek_animation(positions[index]);
        apply_pose(*view, 2 + 2 * index);
        const auto source = view->tree().snapshot();
        const auto geometry = layout.calculate(source);
        EXPECT_EQ(geometry.revision(), original.revision());
        EXPECT_EQ(layout.statistics().measured_nodes, 0U);
        EXPECT_EQ(layout.statistics().arranged_nodes, 0U);
        const auto packet = paint.calculate(source, geometry);
        ASSERT_EQ(packet.quads().size(), 1U);
        EXPECT_EQ(packet.quads().front().clip, viewport);
        EXPECT_EQ(packet.quads().front().bounds.x, positions[index] == 50ms ? -20.f : 0.f);
        auto& input = view->input();
        input.begin_frame(3 + 2 * index, source, geometry);
        EXPECT_EQ(input.hit_test({5, 10}), item);
        EXPECT_EQ(input.hit_test({30, 10}), positions[index] == 50ms ? ui::element_handle{} : item);
        EXPECT_EQ(input.hit_test({-5, 10}), ui::element_handle{});
        input.process({});
        input.tick();
        input.late_tick();
        input.end_frame();
    }
}

/// @brief A rotated, resized border is hit as a quadrilateral inside the fixed mask.
TEST_F(composition_fixture, VisualScaleAndRotationPreserveLayoutAndUseAccurateHits)
{
    auto view = instance::build(drawer(), {*storage});
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    const auto geometry = layout.update(view->tree(), {0, 0, 120, 80});
    view->play_animation({100ms,
                          {{"item", ui::property::scale_x, {{0ms, 2}}},
                           {"item", ui::property::rotate, {{0ms, 45}}}}});
    apply_pose(*view, 2);
    const auto source = view->tree().snapshot();
    const auto packet = paint.calculate(source, geometry);
    ASSERT_EQ(packet.quads().size(), 1U);
    const auto& quad = packet.quads().front();
    EXPECT_NE(quad.vertices[0].position.y, quad.vertices[1].position.y);
    auto& input = view->input();
    input.begin_frame(3, source, geometry);
    EXPECT_EQ(input.hit_test({20, 10}), view->find("item"));
    EXPECT_EQ(input.hit_test({3, 35}), ui::element_handle{});
    input.process({});
    input.tick();
    input.late_tick();
    input.end_frame();
}

/// @brief Explicit spacing and insets reflow following children even during playback.
TEST_F(composition_fixture, LayoutTracksReflowDependentSiblings)
{
    const auto document = parse_xml({R"xml(<ui version="1">
      <element id="stack" panel="horizontal"/>
      <element id="first" parent="stack" panel="leaf" width="20"/>
      <element id="next" parent="stack" panel="leaf" width="20"/>
    </ui>)xml"});
    auto view = instance::build(document, {*storage});
    const ui::layout_rect viewport{0, 0, 120, 80};
    ui::layout_engine layout{jobs.dispatcher()};
    const auto before = layout.update(view->tree(), viewport);
    view->play_animation({100ms,
                          {{"stack", ui::property::spacing, {{0ms, 5}}},
                           {"stack", ui::property::padding_left, {{0ms, 3}}},
                           {"first", ui::property::margin_right, {{0ms, 2}}}}});
    apply_pose(*view, 2);
    const auto after = layout.update(view->tree(), viewport);
    EXPECT_NE(after.revision(), before.revision());
    EXPECT_GT(layout.statistics().arranged_nodes, 0U);
    const auto next = after.find(view->find("next"));
    ASSERT_TRUE(next.has_value());
    EXPECT_FLOAT_EQ(next.value_or(ui::layout_geometry{}).bounds.x, 30.f);
}

/// @brief Pointer and semantic activation share one clip, including after the viewport resizes.
TEST_F(composition_fixture, DrawerUsesOneClipAcrossInputAndViewportChanges)
{
    auto document = drawer();
    document.elements.back().interaction.reset();
    auto view = instance::build(document, {*storage});
    ui::layout_engine layout{jobs.dispatcher()};
    const auto button = view->find("item");
    auto handler = std::make_shared<drawer_toggle>();
    auto& input = view->input();
    input.bind(button, handler, {.focusable = true});
    view->play_animation({100ms, {{"drawer", ui::property::translate_x, {{0ms, 0}, {100ms, 40}}}}});
    const std::array events{
        ui::input_event{.kind = ui::input_kind::pointer_up, .position = {10, 10}},
        ui::input_event{.kind = ui::input_kind::action,
                        .action = atlas::input::action_event{42,
                                                             atlas::input::value_type::button,
                                                             atlas::input::action_phase::performed,
                                                             {1, 0},
                                                             1}}};
    for (std::size_t index = 0; index < events.size(); ++index)
    {
        const auto geometry =
            layout.update(view->tree(), {0, 0, 120 + 10.f * static_cast<float>(index), 80});
        input.begin_frame(2 + index, view->tree().snapshot(), geometry);
        input.focus(button);
        input.process(std::span{&events[index], 1});
        input.tick();
        input.late_tick();
        input.end_frame();
        view->seek_animation(handler->open ? 100ms : 0ms);
        view->publish_animation();
        auto& tree = view->tree();
        tree.apply(tree.commands().seal());
        EXPECT_FLOAT_EQ(tree.paint(view->find("drawer")).translation.x, index == 0 ? 40.f : 0.f);
    }
}

/// @brief Validate paired anchors after composition and keep all state on a failed batch.
TEST_F(composition_fixture, CanvasAnimationValidatesTheFinalAnchorPairAtomically)
{
    auto view = instance::build(drawer(), {*storage});
    const auto target = view->find("drawer");
    view->play_animation({100ms,
                          {{"drawer", ui::property::canvas_x_anchor_min, {{0ms, 0.5f}}},
                           {"drawer", ui::property::canvas_x_anchor_max, {{0ms, 0.5f}}},
                           {"drawer", ui::property::canvas_x_offset_min, {{0ms, -10}}}}});
    apply_pose(*view, 2);
    const auto original = view->tree().canvas_slot_for(target);
    ASSERT_TRUE(original.has_value());
    const auto horizontal = original.value_or(ui::canvas_slot{}).horizontal;
    EXPECT_FLOAT_EQ(horizontal.anchor_min, 0.5f);
    EXPECT_FLOAT_EQ(horizontal.anchor_max, 0.5f);
    EXPECT_FLOAT_EQ(horizontal.offset_min, -10.f);
    const auto revision = view->tree().snapshot().revision();
    auto& commands = view->tree().commands();
    commands.begin_frame(3);
    commands.set(target, ui::property::translate_x, 15.f, 1);
    commands.set(target, ui::property::canvas_x_anchor_min, 0.75f, 1);
    EXPECT_THROW(view->tree().apply(commands.seal()), std::invalid_argument);
    EXPECT_EQ(view->tree().canvas_slot_for(target), original);
    EXPECT_EQ(view->tree().snapshot().revision(), revision);
    EXPECT_FLOAT_EQ(view->tree().paint(target).translation.x, 0.f);
}

/// @brief The scissor contract rejects a rotated mask and treats collapsed elements as empty.
TEST_F(composition_fixture, NonRectangularMasksFailAndSingularTransformsHaveNoHit)
{
    auto view = instance::build(drawer(), {*storage});
    ui::layout_engine layout{jobs.dispatcher()};
    ui::paint_engine paint{jobs.dispatcher()};
    const auto geometry = layout.update(view->tree(), {0, 0, 120, 80});
    const auto mask = view->find("mask");
    auto style = view->tree().paint(mask);
    style.rotation = 45;
    view->tree().set_paint(mask, style);
    EXPECT_THROW((void)paint.calculate(view->tree().snapshot(), geometry), std::invalid_argument);
    EXPECT_THROW(view->input().begin_frame(2, view->tree().snapshot(), geometry),
                 std::invalid_argument);
    style.scale.x = 0;
    view->tree().set_paint(mask, style);
    const auto source = view->tree().snapshot();
    EXPECT_TRUE(paint.calculate(source, geometry).quads().empty());
    view->input().begin_frame(2, source, geometry);
    EXPECT_EQ(view->input().hit_test({5, 5}), ui::element_handle{});
}
} // namespace atlas::ui_content::tests
