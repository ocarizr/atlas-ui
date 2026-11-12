/** @file @brief Exercise authored shell commands, project registration and recovery. */
#include "../../../streaming/temporary_assets.hpp"
#include "tool_panel.hpp"
#include <fstream>
#include <gtest/gtest.h>

namespace atlas::editor::tests
{
namespace
{
/// @brief Produce an exact pointer click through the portable window ingress.
std::array<input::event, 2> click(input::input_value position)
{
    const input::event press{.device = {1, input::device_kind::pointer},
                             .control = input::pointer_control::primary,
                             .value = {1, 0},
                             .position = position};
    auto release = press;
    release.value = {};
    return {press, release};
}

/// @brief Write an isolated test registration using the production catalog format.
void write_fixture(const std::filesystem::path& file, std::string_view bytes)
{
    std::ofstream output{file, std::ios::binary};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output)
    {
        throw std::runtime_error("Cannot write editor tool test fixture");
    }
}

/// @brief Save a project panel with the authoring persistence path before registering it.
void project_catalog(const std::filesystem::path& root, jobs::job_system& jobs)
{
    const auto installed = installed_tools_root();
    const auto bytes = read_editor_source(installed / "catalog.xml", jobs, {});
    auto draft = assets::read_xml({bytes, "installed test catalog"}).document();
    std::erase_if(draft.records,
                  [](const auto& row)
                  {
                      return row.source.path == "shell.xml";
                  });
    std::filesystem::create_directory(root);
    for (auto& row : draft.records)
    {
        const auto original = installed / row.source.path;
        if (row.source.path == "width.xml")
        {
            row.source.path = "project_width.xml";
            auto document = open_document(original, jobs, {});
            document.elements.back().caption = "Widen project selection";
            const auto destination = root / row.source.path;
            save_document(destination, document, {});
            row.byte_size = read_editor_source(destination, jobs, {}).size();
        }
        else
        {
            std::filesystem::copy_file(original, root / row.source.path);
        }
    }

    write_fixture(root / "catalog.xml",
                  assets::write_xml(assets::catalog::compile(std::move(draft))));
}
} // namespace

/// @brief The authored toolbar and both origin types use the same Lua runtime and command bridge.
TEST(editor_tools, DispatchesAuthoredToolbarAndReopensProjectTool)
{
    streaming::tests::temporary_assets files;
    const auto project = files.path().parent_path() / "editor_tools";
    jobs::job_system jobs{2};
    project_catalog(project, jobs);
    const tool_registry registry{{installed_tools_root(), project}, jobs, {}};
    ASSERT_TRUE(registry.shell().has_value());
    ASSERT_EQ(registry.tools().size(), 2U);
    EXPECT_EQ(registry.tools()[0].title, "Editor: width");
    EXPECT_EQ(registry.tools()[1].title, "Project: project_width");
    EXPECT_NE(registry.tools()[0].storage, registry.tools()[1].storage);

    if (const auto& entry = registry.shell())
    {
        tool_panel shell{*entry, true, {}};
        ui::layout_engine layout{jobs.dispatcher()};
        ui::paint_engine paint{jobs.dispatcher()};
        EXPECT_FALSE(shell.paint({0, 0, 960, 540}, layout, paint).quads().empty());
        const auto result = shell.dispatch(click({96, 18}), 1);
        ASSERT_EQ(result.commands.size(), 1U);
        EXPECT_EQ(result.commands.front().kind, control_kind::save);
    }

    const auto empty =
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")});
    streaming::catalog_streamer storage{jobs, empty, {}};
    document_session session{new_document(), {storage}};
    const window::window_state state{960, 540, 1};
    editor_view view{session, jobs, files.path(), {}, &registry};
    (void)view.snapshot(state, click({350, 60}));
    (void)view.snapshot(state, click({350, 60}));
    (void)view.snapshot(state, click({275, 100}));
    EXPECT_EQ(session.document().elements.front().requested.width, 136.f);
    (void)view.snapshot(state, click({350, 60}));
    (void)view.snapshot(state, click({350, 60}));
    (void)view.snapshot(state, click({350, 60}));
    (void)view.snapshot(state, click({275, 100}));
    EXPECT_EQ(session.document().elements.front().requested.width, 152.f);
}

/// @brief Exercise fallback for failures in registration and in the registered XML.
class editor_tool_recovery : public testing::TestWithParam<bool>
{
};

/// @brief The recovery controls retain persistence even when authored chrome cannot load.
TEST_P(editor_tool_recovery, KeepsNativeSaveAvailable)
{
    streaming::tests::temporary_assets files;
    const auto root = files.path().parent_path() / "tools";
    std::filesystem::create_directory(root);
    jobs::job_system jobs{1};
    constexpr std::string_view broken = "<broken/>";
    const bool valid_catalog = GetParam();
    if (valid_catalog)
    {
        const auto installed = installed_tools_root();
        const auto bytes = read_editor_source(installed / "catalog.xml", jobs, {});
        auto draft = assets::read_xml({bytes, "installed test catalog"}).document();
        for (auto& row : draft.records)
        {
            const auto destination = root / row.source.path;
            if (row.source.path == "shell.xml")
            {
                write_fixture(destination, broken);
                row.byte_size = broken.size();
            }
            else
            {
                std::filesystem::copy_file(installed / row.source.path, destination);
            }
        }

        write_fixture(root / "catalog.xml",
                      assets::write_xml(assets::catalog::compile(std::move(draft))));
    }
    else
    {
        write_fixture(root / "catalog.xml", broken);
    }

    const tool_registry registry{{root, {}}, jobs, {}};
    EXPECT_EQ(registry.shell().has_value(), valid_catalog);
    const auto empty =
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")});
    streaming::catalog_streamer storage{jobs, empty, {}};
    document_session session{new_document(), {storage}};
    editor_view view{session, jobs, files.path(), {}, &registry};
    (void)view.snapshot({960, 540, 1}, click({96, 18}));
    EXPECT_TRUE(document_exists(files.path()));
    EXPECT_FALSE(session.dirty());
}

INSTANTIATE_TEST_SUITE_P(InvalidAuthoring, editor_tool_recovery, testing::Bool(),
                         [](const testing::TestParamInfo<bool>& scenario)
                         {
                             return scenario.param ? "InvalidShell" : "InvalidCatalog";
                         });
} // namespace atlas::editor::tests
