/** @file @brief Streamed, interpreted, and generated authored UI composition. */
#include "composition_fixture.hpp"
#include <atlas/ui/layout_engine.hpp>

namespace atlas::ui_content::tests
{
namespace
{
/// @brief Record the authored action's exact payload during model synchronization.
class action_probe final : public mvvm::observer
{
  public:
    /// @brief Observe the action after the owner publishes the model queue.
    void on_notify(const mvvm::notification& note, mvvm::view_model&) override
    {
        amount = std::get<std::int64_t>(note.arguments.front());
    }

    std::int64_t amount = 0; ///< Last action payload.
};

/// @brief Use the normal owner-frame phases and the same layout/command contracts.
void exercise(instance& ui, jobs::job_system& jobs)
{
    auto& model = ui.model("hud");
    auto& second = ui.model("secondary");
    auto action = std::make_shared<action_probe>();
    (void)model.subscribe(model.description().action("gain"), action);
    ui::layout_engine layout{jobs.dispatcher()};
    const auto geometry = layout.update(ui.tree(), {0, 0, 160, 100});
    const auto button = ui.find("button");
    const auto sibling = ui.find("sibling");
    const auto button_geometry = ui.tree().computed(button);
    const auto sibling_geometry = ui.tree().computed(sibling);
    EXPECT_EQ(button_geometry.value_or(ui::layout_geometry{}).bounds.x, 8.f);
    EXPECT_EQ(sibling_geometry.value_or(ui::layout_geometry{}).bounds.x, 62.f);
    ui.begin_models(2);
    ui.input().begin_frame(2, ui.tree().snapshot(), geometry);
    ui.input().process({});
    ui.input().tick();
    ui.write_back("button", ui::property::visible, false);
    ui.synchronize();
    ui.tree().apply(ui.tree().commands().seal());
    EXPECT_FALSE(ui.tree().requested(button).visible);
    EXPECT_EQ(model.integer(model.description().field("xp")), 8);
    EXPECT_EQ(second.integer(second.description().field("xp")), 8);
    EXPECT_EQ(action->amount, 17);
    EXPECT_EQ(ui.scripts().inspect().interpreters, 1U);
    EXPECT_EQ(ui.scripts().inspect().instances, 2U);
    ui.input().late_tick();
    ui.input().end_frame();
}
} // namespace

/// @brief Source and generated model forms drive the same authored tree and bindings.
TEST_F(composition_fixture, StreamedAndGeneratedModelsHaveEquivalentWiring)
{
    const auto document = load(ui_asset, {*storage});
    const auto bytes = write_xml(document);
    auto interpreted = instance::build(parse_xml({bytes}), {*storage});
    const std::array factories{prepared_model{vm_asset, 1, generated::make_ui_content_fixture}};
    auto prepared = instance::build(document, {*storage, factories});
    EXPECT_NE(interpreted->find("button"), prepared->find("button"));
    EXPECT_EQ(prepared->texture("button"), texture_asset);
    exercise(*interpreted, jobs);
    exercise(*prepared, jobs);
}

/// @brief Removing a live element never redirects its authored ID to another slot.
TEST_F(composition_fixture, RemovedElementDoesNotResolveAfterReplacement)
{
    auto composed = instance::build(load(ui_asset, {*storage}), {*storage});
    composed->tree().remove(composed->find("sibling"));
    (void)composed->tree().create(composed->find("root"));
    EXPECT_THROW((void)composed->find("sibling"), std::out_of_range);
}

/// @brief Retiring an instance cancels its native work without exposing old results to its
/// replacement.
TEST_F(composition_fixture, ReplacementRetiresOldBehaviorWork)
{
    const auto source = load(ui_asset, {*storage});
    auto active = instance::build(source, {*storage});
    exercise(*active, jobs);
    auto requests = active->scripts().take_requests();
    ASSERT_EQ(requests.size(), 1U);
    auto replacement = instance::build(source, {*storage});
    active.reset();
    for (const auto& request : requests)
    {
        EXPECT_TRUE(request.work.cancellation.stop_requested());
        EXPECT_FALSE(request.work.complete({}));
    }

    EXPECT_EQ(replacement->scripts().inspect().instances, 2U);
}
} // namespace atlas::ui_content::tests
