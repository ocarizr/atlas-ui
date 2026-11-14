/** @file @brief Exercise interrupted and concurrent playback at the tree command boundary. */
#include "composition_fixture.hpp"
#include <atlas/ui/layout_engine.hpp>

namespace atlas::ui_content::tests
{
namespace
{
using namespace std::chrono_literals;

/// @brief Run the owner publication phase without input or layout.
void publish(instance& view, std::uint64_t frame)
{
    auto& tree = view.tree();
    tree.commands().begin_frame(frame);
    view.publish_animation();
    tree.apply(tree.commands().seal());
}
} // namespace

/// @brief Direction changes preserve the exact drawer pose while a delayed fade runs separately.
TEST_F(composition_fixture, DrawerReversesWhileIndependentClipContinues)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    const auto target = view->find("sibling");
    const auto drawer = view->start_animation(
        {100ms, {{"sibling", ui::property::translate_x, {{0ms, -80.f}, {100ms, 0.f}}}}});
    const auto focus = view->start_animation(
        {100ms, {{"sibling", ui::property::opacity, {{0ms, 1.f}, {100ms, 0.f}}}}}, {.delay = 25ms});
    view->advance_animation(50ms);
    publish(*view, 2);
    EXPECT_FLOAT_EQ(view->tree().paint(target).translation.x, -40.f);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.75f);
    view->reverse_animation(drawer);
    publish(*view, 3);
    EXPECT_FLOAT_EQ(view->tree().paint(target).translation.x, -40.f);
    view->advance_animation(25ms);
    publish(*view, 4);
    EXPECT_FLOAT_EQ(view->tree().paint(target).translation.x, -60.f);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.5f);
    view->pause_animation(focus, true);
    view->advance_animation(25ms);
    publish(*view, 5);
    EXPECT_FLOAT_EQ(view->tree().paint(target).translation.x, -80.f);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.5f);
}

/// @brief Retargeting starts from the last visible pose and an old handle cannot stop it.
TEST_F(composition_fixture, RetargetsFromCurrentPoseAndRejectsStaleHandles)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    const auto target = view->find("sibling");
    const auto first = view->tween_animation("sibling", ui::property::opacity, 0.f, 100ms);
    view->advance_animation(40ms);
    publish(*view, 2);
    const auto next = view->tween_animation("sibling", ui::property::opacity, 1.f, 100ms);
    EXPECT_THROW(view->cancel_animation(first), std::invalid_argument);
    publish(*view, 3);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.6f);
    view->advance_animation(50ms);
    publish(*view, 4);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.8f);
    view->cancel_animation(next);
    EXPECT_THROW(view->pause_animation(next, true), std::invalid_argument);
}

/// @brief Markers cross in deterministic traversal order and scrubbing stays silent.
TEST_F(composition_fixture, PingPongMarkersAndSilentSeek)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    const animation_clip clip{100ms,
                              {{"sibling", ui::property::opacity, {{0ms, 0.f}, {100ms, 1.f}}}},
                              {{25ms, 1}, {25ms, 2}, {75ms, 3}}};
    const auto player =
        view->start_animation(clip, {.repeat = animation_repeat::ping_pong, .traversals = 2});
    view->seek_animation(player, 10ms);
    EXPECT_TRUE(view->take_animation_events().empty());
    view->advance_animation(190ms);
    const auto events = view->take_animation_events();
    ASSERT_EQ(events.size(), 7U);
    const std::array expected{1U, 2U, 3U, 3U, 1U, 2U, 0U};
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        EXPECT_EQ(events[index].player, player);
        EXPECT_EQ(events[index].marker, expected[index]);
    }

    EXPECT_EQ(events.back().kind, animation_event_kind::completed);
}

/// @brief Higher priority samples blend deterministically and restore to the surviving layer.
TEST_F(composition_fixture, LayerWeightAndCancellationRestoreUnderlyingPose)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    const auto target = view->find("sibling");
    view->register_animation("focus", {100ms, {{"sibling", ui::property::opacity, {{0ms, 0.8f}}}}});
    const auto lower = view->start_named_animation("focus");
    const auto upper =
        view->start_animation({100ms, {{"sibling", ui::property::opacity, {{0ms, 0.2f}}}}},
                              {.priority = 1, .weight = 0.5f, .restore = true});
    publish(*view, 2);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.5f);
    view->set_animation_weight(upper, 1);
    publish(*view, 3);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.2f);
    view->cancel_animation(upper);
    publish(*view, 4);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.8f);
    view->cancel_animation(lower);
}

/// @brief Sequential group delays are relative to preceding clips and failed groups roll back.
TEST_F(composition_fixture, StaggeredGroupAndReducedMotion)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    const auto target = view->find("sibling");
    const animation_clip slide{100ms,
                               {{"sibling", ui::property::translate_x, {{0ms, 0}, {100ms, 40}}}}};
    const animation_clip fade{100ms, {{"sibling", ui::property::opacity, {{0ms, 1}, {100ms, 0}}}}};
    const animation_clip broken{100ms, {}};
    const std::array invalid{animation_group_entry{slide}, animation_group_entry{broken}};
    EXPECT_THROW((void)view->start_animation_group(invalid), std::invalid_argument);
    publish(*view, 2);
    EXPECT_FLOAT_EQ(view->tree().paint(target).translation.x, 0);
    const std::array group{animation_group_entry{slide},
                           animation_group_entry{fade, {.delay = 20ms}}};
    const auto handles = view->start_animation_group(group, animation_group_order::sequential);
    ASSERT_EQ(handles.size(), 2U);
    view->advance_animation(110ms);
    publish(*view, 3);
    EXPECT_FLOAT_EQ(view->tree().paint(target).translation.x, 40);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 1);
    view->advance_animation(60ms);
    publish(*view, 4);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.5f);
    view->set_reduced_motion(true);
    view->advance_animation(1ms);
    publish(*view, 5);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0);
    EXPECT_EQ(view->take_animation_events().size(), 2U);
}

/// @brief Input routes and Lua can invoke the same model action and reuse a clip.
TEST_F(composition_fixture, ModelActionStartsAReusableClip)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    view->bind_animation_action(
        {"hud", "gain"}, {100ms, {{"sibling", ui::property::opacity, {{0ms, 0.f}, {100ms, 1.f}}}}});
    auto& model = view->model("hud");
    view->begin_models(2);
    const std::array args{mvvm::value{std::int64_t{2}}};
    model.invoke(model.description().action("gain"), args);
    view->tree().commands().begin_frame(2);
    view->synchronize();
    view->advance_animation(50ms);
    view->publish_animation();
    view->tree().apply(view->tree().commands().seal());
    EXPECT_FLOAT_EQ(view->tree().requested(view->find("sibling")).opacity, 0.5f);
}

/// @brief A failed owner advance rolls back both earlier clocks and its event prefix.
TEST_F(composition_fixture, ExcessiveMarkerDeliveryRollsBackAllClocks)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    (void)view->start_animation(
        {100ms, {{"sibling", ui::property::opacity, {{0ms, 0.f}, {100ms, 1.f}}}}});
    (void)view->start_animation(
        {2ns, {{"sibling", ui::property::translate_x, {{0ns, 0.f}}}}, {{1ns, 1}}},
        {.repeat = animation_repeat::loop});
    EXPECT_THROW(view->advance_animation(1s), std::overflow_error);
    EXPECT_TRUE(view->take_animation_events().empty());
    view->advance_animation(1ns);
    const auto events = view->take_animation_events();
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events.front().marker, 1U);
    publish(*view, 2);
    EXPECT_LT(view->tree().requested(view->find("sibling")).opacity, 0.001f);
}

/// @brief Additive signed channels use an offset from the first key and retire with the target.
TEST_F(composition_fixture, AdditiveLayersAndRemovedTarget)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    const auto original = view->find("sibling");
    (void)view->start_animation({100ms, {{"sibling", ui::property::translate_x, {{0ms, 10.f}}}}});
    (void)view->start_animation(
        {100ms, {{"sibling", ui::property::translate_x, {{0ms, 3.f}, {100ms, 11.f}}}}},
        {.priority = 1, .additive = true});
    view->advance_animation(50ms);
    publish(*view, 2);
    EXPECT_FLOAT_EQ(view->tree().paint(original).translation.x, 14.f);
    view->tree().remove(original);
    const auto replacement = view->tree().create(view->find("root"));
    view->advance_animation(50ms);
    publish(*view, 3);
    EXPECT_FLOAT_EQ(view->tree().paint(replacement).translation.x, 0.f);
}

/// @brief Named state changes reverse from the presented pose while a second channel advances.
TEST_F(composition_fixture, DeclarativeStatesRetargetAndBlendIndependently)
{
    auto document = load(ui_asset, {*storage});
    auto& button = document.elements.at(1);
    button.interaction = ui::interaction_options{.focusable = true};
    auto& route = button.actions.front().binding;
    route.event = ui::event_kind::action;
    route.route = ui::route_phase::target;
    route.input_action = 42;
    auto view = instance::build(document, {*storage});
    const auto target = view->find("sibling");
    const auto focused_button = view->find("button");
    view->register_animation_states("drawer", {{"sibling", ui::property::translate_x}},
                                    {{"closed", {0.f}}, {"open", {80.f}}},
                                    {.duration = 100ms, .curve = animation_curve::linear});
    view->register_animation_states("button", {{"button", ui::property::opacity}},
                                    {{"focused", {0.8f}}, {"pressed", {0.4f}}},
                                    {.duration = 100ms, .curve = animation_curve::linear});
    view->bind_animation_state({"hud", "gain"}, {"drawer", "open"});
    view->begin_models(2);
    ui::layout_engine layout{jobs.dispatcher()};
    const auto geometry = layout.update(view->tree(), {0, 0, 160, 100});
    auto& input = view->input();
    input.begin_frame(2, view->tree().snapshot(), geometry);
    input.focus(focused_button);
    const std::array events{
        ui::input_event{.kind = ui::input_kind::action,
                        .action = atlas::input::action_event{42,
                                                             atlas::input::value_type::button,
                                                             atlas::input::action_phase::performed,
                                                             {1, 0},
                                                             1}}};
    input.process(events);
    input.tick();
    input.late_tick();
    input.end_frame();
    view->synchronize();
    view->set_animation_state({"button", "pressed"});
    view->advance_animation(50ms);
    view->publish_animation();
    view->tree().apply(view->tree().commands().seal());
    EXPECT_FLOAT_EQ(view->tree().paint(target).translation.x, 40.f);
    EXPECT_FLOAT_EQ(view->tree().requested(focused_button).opacity, 0.7f);
    view->set_animation_state({"drawer", "closed"});
    view->set_animation_state({"button", "focused"});
    publish(*view, 3);
    EXPECT_FLOAT_EQ(view->tree().paint(target).translation.x, 40.f);
    EXPECT_FLOAT_EQ(view->tree().requested(focused_button).opacity, 0.7f);
    view->advance_animation(50ms);
    publish(*view, 4);
    EXPECT_FLOAT_EQ(view->tree().paint(target).translation.x, 20.f);
    EXPECT_FLOAT_EQ(view->tree().requested(focused_button).opacity, 0.75f);
}

/// @brief The spring reaches exact endpoints and gives the same intermediate pose after replay.
TEST_F(composition_fixture, BoundedSpringSamplingIsRepeatable)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    const auto target = view->find("sibling");
    const auto clip =
        view->start_animation({100ms,
                               {{"sibling",
                                 ui::property::opacity,
                                 {{0ms, 0.f, animation_curve::spring}, {100ms, 1.f}}}}});
    publish(*view, 2);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.f);
    view->advance_animation(50ms);
    publish(*view, 3);
    const auto middle = view->tree().requested(target).opacity;
    EXPECT_GT(middle, 0.5f);
    EXPECT_LT(middle, 1.f);
    view->seek_animation(clip, 0ms);
    view->advance_animation(25ms);
    view->advance_animation(25ms);
    publish(*view, 4);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, middle);
    view->advance_animation(50ms);
    publish(*view, 5);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 1.f);
}

/// @brief Reject overlapping state owners and retain ownership after a completed blend.
TEST_F(composition_fixture, StateOwnershipAndReducedMotionAreExplicit)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    const auto target = view->find("sibling");
    const std::vector<animation_channel> channels{{"sibling", ui::property::opacity}};
    const std::vector<animation_pose> poses{{"visible", {1}}, {"dimmed", {0.2f}}};
    view->register_animation_states("focus", channels, poses, {.duration = 100ms});
    EXPECT_THROW(view->register_animation_states("duplicate", channels, poses, {.duration = 100ms}),
                 std::invalid_argument);
    EXPECT_THROW(view->set_animation_state({"focus", "missing"}), std::out_of_range);
    view->set_animation_state({"focus", "dimmed"});
    view->set_reduced_motion(true);
    view->advance_animation(1ms);
    publish(*view, 2);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.2f);
    EXPECT_THROW(
        (void)view->start_animation({100ms, {{"sibling", ui::property::opacity, {{0ms, 0.6f}}}}}),
        std::invalid_argument);
}

/// @brief A bound value remains model-owned even if a pose targets it explicitly.
TEST_F(composition_fixture, StateGroupsDoNotClaimBoundProperties)
{
    auto document = load(ui_asset, {*storage});
    document.elements.back().properties.push_back({"hud", {"health", ui::property::opacity}});
    auto view = instance::build(document, {*storage});
    EXPECT_THROW(view->register_animation_states("focus", {{"sibling", ui::property::opacity}},
                                                 {{"on", {1}}, {"off", {0}}}, {.duration = 100ms}),
                 std::invalid_argument);
    EXPECT_THROW(view->set_animation_state({"focus", "on"}), std::out_of_range);
}
} // namespace atlas::ui_content::tests
