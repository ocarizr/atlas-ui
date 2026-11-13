/** @file @brief Exercise the animation clock and property composition through the authored runtime.
 */
#include "composition_fixture.hpp"
#include <algorithm>
#include <atlas/ui/layout_engine.hpp>
#include <limits>

namespace atlas::ui_content::tests
{
namespace
{
using namespace std::chrono_literals;

/// @brief Use one visible scalar track with a known midpoint.
animation_clip fade()
{
    return {100ms, {{"sibling", ui::property::opacity, {{0ms, 0.f}, {100ms, 1.f}}}}};
}

/// @brief Publish through the same owner phases as an authored view.
ui::command_batch publish(instance& view, jobs::job_system& jobs, std::uint64_t frame)
{
    ui::layout_engine layout{jobs.dispatcher()};
    const auto geometry = layout.update(view.tree(), {0, 0, 160, 100});
    view.begin_models(frame);
    view.input().begin_frame(frame, view.tree().snapshot(), geometry);
    view.input().process({});
    view.input().tick();
    view.input().late_tick();
    view.input().end_frame();
    view.synchronize();
    view.publish_animation();
    auto batch = view.tree().commands().seal();
    view.tree().apply(batch);
    return batch;
}
} // namespace

/// @brief Passive advancement produces no writes and completion is observed exactly once.
TEST_F(composition_fixture, AnimationAdvancesPassivelyAndPublishesTheTerminalSample)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    const auto target = view->find("sibling");
    view->play_animation(fade());
    view->advance_animation(25ms);
    EXPECT_EQ(view->tree().requested(target).opacity, 1.f);
    (void)publish(*view, jobs, 2);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.25f);
    view->advance_animation(2s);
    ASSERT_EQ(view->animation_position(), 100ms);
    EXPECT_TRUE(view->take_animation_completion());
    EXPECT_FALSE(view->take_animation_completion());
    (void)publish(*view, jobs, 3);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 1.f);
    const auto idle = publish(*view, jobs, 4);
    EXPECT_TRUE(std::ranges::none_of(idle.writes(),
                                     [&](const auto& write)
                                     {
                                         return write.target == target;
                                     }));
}

/// @brief Exact seeks, loop boundaries and large deltas use the same integer clock.
TEST_F(composition_fixture, AnimationSeeksAndLoopsWithoutCompletionOrOverflow)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    const auto target = view->find("sibling");
    auto clip = fade();
    clip.tracks.front().keys.front().curve = animation_curve::smoothstep;
    view->play_animation(std::move(clip), animation_repeat::loop);
    view->seek_animation(25ms);
    (void)publish(*view, jobs, 2);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.15625f);
    view->advance_animation(175ms);
    EXPECT_EQ(view->animation_position(), 0ms);
    view->advance_animation(animation_time::max());
    EXPECT_EQ(view->animation_position(), animation_time::max() % 100ms);
    EXPECT_FALSE(view->take_animation_completion());
    view->stop_animation();
    EXPECT_FALSE(view->animation_position().has_value());
}

/// @brief Invalid replacements leave playback intact and bindings retain exclusive ownership.
TEST_F(composition_fixture, AnimationRejectsBoundPropertiesAndInvalidTracks)
{
    auto document = load(ui_asset, {*storage});
    auto& target = document.elements.back();
    target.properties.push_back({"hud", {"health", ui::property::opacity}});
    auto view = instance::build(document, {*storage});
    auto valid = fade();
    valid.tracks.front().property = ui::property::width;
    view->play_animation(valid);
    view->advance_animation(25ms);
    EXPECT_THROW(view->play_animation(fade()), std::invalid_argument);
    auto invalid = valid;
    invalid.tracks.front().keys.back().value = std::numeric_limits<float>::infinity();
    EXPECT_THROW(view->play_animation(invalid), std::invalid_argument);
    invalid = valid;
    invalid.tracks.front().keys.back().time = 0ms;
    EXPECT_THROW(view->play_animation(invalid), std::invalid_argument);
    EXPECT_THROW(view->advance_animation(-1ms), std::invalid_argument);
    EXPECT_EQ(view->animation_position(), 25ms);
    (void)publish(*view, jobs, 2);
    const auto properties = view->tree().requested(view->find(target.id));
    EXPECT_FLOAT_EQ(properties.opacity, 0.75f);
    EXPECT_FLOAT_EQ(properties.width, 0.25f);
}

/// @brief Step interpolation holds to the next key; seek alone never emits completion.
TEST_F(composition_fixture, AnimationStepAndExactSeekShareTheSameClock)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    auto clip = fade();
    clip.tracks.front().keys = {{20ms, 0.25f, animation_curve::step}, {80ms, 0.75f}};
    const auto target = view->find("sibling");
    view->play_animation(std::move(clip));
    (void)publish(*view, jobs, 2);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.25f);
    view->seek_animation(79ms);
    (void)publish(*view, jobs, 3);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.25f);
    view->seek_animation(80ms);
    (void)publish(*view, jobs, 4);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.75f);
    view->seek_animation(150ms);
    EXPECT_EQ(view->animation_position(), 100ms);
    EXPECT_FALSE(view->take_animation_completion());
}

/// @brief Capacity failure admits neither part of the sample and allows a later retry.
TEST_F(composition_fixture, AnimationPreflightsAllWritesBeforePublishing)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    auto clip = fade();
    clip.tracks.push_back({"sibling", ui::property::height, {{0ms, 16.f}}});
    view->play_animation(std::move(clip));
    auto& commands = view->tree().commands();
    commands.begin_frame(2);
    const auto target = view->find("sibling");
    for (std::size_t index = 0; index < ui::command_limits{}.max_commands - 1; ++index)
    {
        commands.set(target, ui::property::height, 12.f, 1);
    }

    const auto pending = commands.pending();
    EXPECT_THROW(view->publish_animation(), std::overflow_error);
    EXPECT_EQ(commands.pending(), pending);
    commands.discard_pending();
    view->publish_animation();
    EXPECT_EQ(commands.seal().writes().size(), 2U);
}

/// @brief Commands from other producers retain normal diagnostics and stale targets are skipped.
TEST_F(composition_fixture, AnimationComposesAfterOtherCommandsAndSkipsRemovedTargets)
{
    auto view = instance::build(load(ui_asset, {*storage}), {*storage});
    const auto target = view->find("sibling");
    view->play_animation(fade());
    view->advance_animation(25ms);
    auto& commands = view->tree().commands();
    commands.begin_frame(2);
    commands.set(target, ui::property::opacity, 0.75f, 1);
    view->publish_animation();
    const auto batch = commands.seal();
    ASSERT_EQ(batch.conflicts().size(), 1U);
    view->tree().apply(batch);
    EXPECT_FLOAT_EQ(view->tree().requested(target).opacity, 0.25f);
    view->tree().remove(target);
    const auto replacement = view->tree().create(view->find("root"));
    view->advance_animation(25ms);
    commands.begin_frame(3);
    view->publish_animation();
    const auto next = commands.seal();
    EXPECT_TRUE(next.writes().empty());
    view->tree().apply(next);
    EXPECT_FLOAT_EQ(view->tree().requested(replacement).opacity, 1.f);
}
} // namespace atlas::ui_content::tests
