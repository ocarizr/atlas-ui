/** @file @brief Invalid cross-document references preserve the previous live composition. */
#include "composition_fixture.hpp"

namespace atlas::ui_content::tests
{
/// @brief Malformed field/model/script routes cannot publish a partially constructed UI.
TEST_F(composition_fixture, RejectsMissingAndWrongTypeSourcesAndModelFields)
{
    const auto source = load(ui_asset, {*storage});
    auto active = instance::build(source, {*storage});
    auto invalid = source;
    invalid.models.front().source.id = script_asset.id;
    EXPECT_THROW((void)instance::build(invalid, {*storage}), std::invalid_argument);

    invalid = source;
    auto missing = script_asset;
    missing.id.local = 99;
    invalid.elements[1].script = behavior{missing, "hud"};
    EXPECT_THROW((void)instance::build(invalid, {*storage}), std::invalid_argument);

    invalid = source;
    invalid.elements[1].properties.front().binding.source = "xp";
    EXPECT_THROW((void)instance::build(invalid, {*storage}), std::invalid_argument);

    invalid = source;
    invalid.elements[1].actions.front().binding.arguments.front() = true;
    EXPECT_THROW((void)instance::build(invalid, {*storage}), std::invalid_argument);

    EXPECT_EQ(active->scripts().inspect().instances, 2U);
    EXPECT_TRUE(active->tree().contains(active->find("button")));
}

/// @brief Native factories cannot silently take precedence over a newer authored revision.
TEST_F(composition_fixture, RejectsStaleFactoryAndCancelledBuild)
{
    const auto source = load(ui_asset, {*storage});
    const std::array stale{prepared_model{vm_asset, 2, generated::make_ui_content_fixture}};
    EXPECT_THROW((void)instance::build(source, {*storage, stale}), std::invalid_argument);

    std::stop_source stop;
    stop.request_stop();
    const construction cancelled{*storage, {}, {}, {}, {.cancellation = stop.get_token()}};
    EXPECT_THROW((void)instance::build(source, cancelled), jobs::job_cancelled);
    EXPECT_THROW((void)load(ui_asset, cancelled), jobs::job_cancelled);
}
} // namespace atlas::ui_content::tests
