/** @file @brief Exercise interrupted and concurrent playback at the tree command boundary. */
#include "composition_fixture.hpp"

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
} // namespace atlas::ui_content::tests
