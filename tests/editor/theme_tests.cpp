/** @file @brief Editor presets, strict preference serialization and failure-safe customization. */
#include "document_session.hpp"
#include "editor_theme.hpp"
#include <gtest/gtest.h>
#include <type_traits>

namespace atlas::editor::tests
{
static_assert(std::is_nothrow_move_constructible_v<theme_preferences>,
              "Preference ownership must transfer without allocating after persistence");
static_assert(std::is_nothrow_move_assignable_v<theme_preferences>,
              "Preference publication must not fail after persistence succeeds");

/// @brief An invalid native candidate cannot persist or replace accepted preferences.
TEST(editor_theme, DuplicateNativeOverridesDoNotPersistOrPublish)
{
    editor_theme theme;
    const auto before = theme.context()->snapshot();
    unsigned saves = 0;
    theme.persist(
        [&](std::string_view)
        {
            ++saves;
        });
    const theme_preferences duplicate{"dark", {{"accent", "#000000FF"}, {"accent", "#FFFFFFFF"}}};
    EXPECT_THROW(theme.configure(duplicate), std::invalid_argument);
    EXPECT_THROW((void)write_theme_preferences(duplicate), std::invalid_argument);
    EXPECT_EQ(saves, 0);
    EXPECT_EQ(theme.context()->snapshot(), before);
    EXPECT_TRUE(theme.preferences().overrides.empty());
}

/// @brief Serialized YAML stays canonical regardless of native override declaration order.
TEST(editor_theme, CanonicalPreferencesDoNotDependOnOverrideOrder)
{
    const theme_preferences first{"light", {{"text", "#010203FF"}, {"accent", "#123456FF"}}};
    const theme_preferences second{"light", {{"accent", "#123456FF"}, {"text", "#010203FF"}}};
    EXPECT_EQ(write_theme_preferences(first), write_theme_preferences(second));
    const auto restored = parse_theme_preferences(write_theme_preferences(first));
    ASSERT_EQ(restored.overrides.size(), 2);
    EXPECT_EQ(restored.overrides.front().key, "accent");
}

/// @brief Semantic source edits preserve literal data, undo history and failure atomicity.
TEST(editor_theme, AuthoredColorReferenceUsesOneValidatedUndoTransaction)
{
    jobs::job_system workers{1};
    streaming::catalog_streamer storage{
        workers,
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")}),
        {}};
    const auto palette = std::make_shared<ui::palette_context>(editor_preset("dark"));
    document_session session{new_document(), {.storage = storage, .palette = palette}};
    session.select("element_0");
    EXPECT_TRUE(session.set_color_key("accent"));
    EXPECT_EQ(session.document().elements.back().color_key, "accent");
    const auto revision = session.revision_id();
    EXPECT_THROW((void)session.set_color_key("missing"), std::out_of_range);
    EXPECT_THROW((void)session.set_color_key("accent", true), std::invalid_argument);
    EXPECT_EQ(session.revision_id(), revision);
    EXPECT_TRUE(session.undo());
    EXPECT_TRUE(session.document().elements.back().color_key.empty());
    EXPECT_TRUE(session.redo());
    EXPECT_EQ(session.document().elements.back().color_key, "accent");
}

/// @brief YAML selection/overrides survive restart; presets remain immutable and preview separate.
TEST(editor_theme, RoundTripsCustomSwatchesAndReset)
{
    editor_theme chrome;
    editor_theme preview;
    chrome.configure({"light", {{"accent", "#123456FF"}}});
    const auto yaml = write_theme_preferences(chrome.preferences());
    editor_theme restored;
    restored.configure(parse_theme_preferences(yaml));
    const auto slot = chrome.context()->snapshot()->find("accent");
    EXPECT_EQ(restored.context()->snapshot()->color(slot), ui_content::parse_color("#123456FF"));
    EXPECT_NE(preview.context()->snapshot()->color(slot),
              restored.context()->snapshot()->color(slot));
    chrome.configure({"light", {}});
    EXPECT_EQ(chrome.context()->snapshot()->color(slot), editor_preset("light")->color(slot));
}

/// @brief A persistence failure must not change colors, selected preset or accepted preferences.
TEST(editor_theme, FailedSavePreservesSelectionAndOverrides)
{
    editor_theme theme;
    const auto before = theme.context()->snapshot();
    theme.persist(
        [](std::string_view)
        {
            throw std::runtime_error("disk full");
        });
    EXPECT_THROW(theme.configure({"light", {}}), std::runtime_error);
    EXPECT_EQ(theme.context()->snapshot(), before);
    EXPECT_EQ(theme.preferences().preset, "dark");
    EXPECT_THROW(theme.configure({"dark", {{"unknown", "#000000FF"}}}), std::out_of_range);
    EXPECT_EQ(theme.context()->snapshot(), before);
}

/// @brief Invalid schema, aliases, duplicates and incompatible keys never silently coerce data.
class theme_yaml_failure : public testing::TestWithParam<const char*>
{
};

/// @brief Reject malformed preferences rather than accepting a partially applied theme.
TEST_P(theme_yaml_failure, RejectsInvalidPreferences)
{
    EXPECT_THROW((void)parse_theme_preferences(GetParam()), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(
    StrictPreferences, theme_yaml_failure,
    testing::Values(
        "version: 2\npreset: dark\n", "version: 1\npreset: missing\n",
        "version: 1\npreset: dark\npreset: light\n", "version: 1\npreset: dark\nunknown: 1\n",
        "version: 1\npreset: dark\noverrides: {accent: bad}\n",
        "version: 1\npreset: dark\noverrides: {missing: '#000000FF'}\n",
        "version: 1\npreset: dark\noverrides: {accent: '#000000FF', accent: '#FFFFFFFF'}\n"));
} // namespace atlas::editor::tests
