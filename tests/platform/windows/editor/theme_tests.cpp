/** @file @brief Cached chrome theme updates preserve native identities and preview geometry. */
#include "../../../streaming/temporary_assets.hpp"
#include "document_file.hpp"
#include "editor_view.hpp"
#include "theme_panel.hpp"
#include "tool_registry.hpp"
#include <gtest/gtest.h>

namespace atlas::editor::tests
{
/// @brief Reopening persisted YAML uses the real atomic writer and streamer, without user settings.
TEST(editor_theme, NativePreferencesPersistAndReopenWithoutTemporaryOutputs)
{
    streaming::tests::temporary_assets files;
    jobs::job_system workers{1};
    const auto path = files.path().parent_path() / "editor.yaml";
    editor_theme first;
    first.persist(
        [destination = path](std::string_view bytes)
        {
            write_editor_source(destination, bytes, true);
        });
    first.configure({"light", {{"accent", "#123456FF"}}});
    editor_theme reopened;
    reopened.configure(parse_theme_preferences(read_editor_source(path, workers, {})));
    const auto slot = reopened.context()->snapshot()->find("accent");
    EXPECT_EQ(reopened.context()->snapshot()->color(slot), ui_content::parse_color("#123456FF"));
    EXPECT_EQ(reopened.preferences().preset, "light");
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator{files.path().parent_path()},
                            std::filesystem::directory_iterator{}),
              1);
}

/// @brief The same headless application route used by the native host, without a screen session.
TEST(editor_theme, RecolorsCachedChromeWithoutRebuildingPreviewOrInput)
{
    jobs::job_system workers{1};
    streaming::catalog_streamer storage{
        workers,
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")}),
        {}};
    document_session session{new_document(), {storage}};
    tool_registry tools{{installed_tools_root(), {}}, workers, {}};
    editor_view view{session, workers, "unused.xml", {}, &tools};
    const auto channel = std::make_shared<accessibility::channel>();
    view.bind_accessibility(channel);
    const window::window_state size{960, 540, 1};
    const auto first = view.snapshot(size, {});
    const auto semantics = channel->read();
    const auto revision = session.revision_id();
    const auto preview = session.preview().snapshot().session();
    view.theme().configure({"light", {}});
    const auto next = view.snapshot(size, {});
    ASSERT_EQ(first->quads.size(), next->quads.size());
    EXPECT_EQ(first->atlas, next->atlas);
    EXPECT_EQ(channel->read(), semantics);
    EXPECT_EQ(session.revision_id(), revision);
    EXPECT_EQ(session.preview().snapshot().session(), preview);
    bool changed = false;
    for (std::size_t i = 0; i < first->quads.size(); ++i)
    {
        EXPECT_EQ(first->quads[i].target, next->quads[i].target);
        EXPECT_EQ(first->quads[i].bounds, next->quads[i].bounds);
        if (first->quads[i].target.session == preview)
        {
            EXPECT_EQ(first->quads[i], next->quads[i]);
        }

        changed |= first->quads[i].vertices != next->quads[i].vertices;
    }

    EXPECT_TRUE(changed);
    view.theme().configure({"light", {{"control", "#00000000"}}});
    const auto transparent = view.snapshot(size, {});
    EXPECT_LT(transparent->quads.size(), next->quads.size());
    EXPECT_EQ(channel->read(), semantics);
    view.theme().configure({"light", {}});
    const auto restored = view.snapshot(size, {});
    EXPECT_EQ(restored->quads.size(), next->quads.size());
    EXPECT_EQ(restored->atlas, first->atlas);
    EXPECT_EQ(channel->read(), semantics);
}

/// @brief Draft edits do not publish before Apply; invalid input and cancellation preserve colors.
TEST(editor_theme, InspectorEditsUseValidatedPreferenceTransactions)
{
    editor_theme theme;
    theme_panel panel;
    panel.execute("toggle", theme);
    EXPECT_TRUE(panel.visible);
    panel.execute("edit", theme);
    std::vector<input::event> input;
    input.reserve(18);
    for (unsigned i = 0; i < 9; ++i)
    {
        input.push_back(
            {.device = {1, input::device_kind::keyboard}, .control = 8, .value = {1, 0}});
    }

    for (const char c : std::string_view{"#123456FF"})
    {
        input.push_back(
            {.kind = input::event_kind::text_input, .control = static_cast<std::uint32_t>(c)});
    }

    const auto before = theme.context()->snapshot();
    EXPECT_TRUE(panel.input(input));
    EXPECT_EQ(theme.context()->snapshot(), before);
    panel.execute("apply", theme);
    EXPECT_EQ(theme.preferences().overrides.size(), 1);
    panel.execute("reset-all", theme);
    EXPECT_TRUE(theme.preferences().overrides.empty());
}
} // namespace atlas::editor::tests
