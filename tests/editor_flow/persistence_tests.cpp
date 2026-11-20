/** @file @brief Explicit checkpoint recovery and failed atomic-write preservation. */
#include "fixture.hpp"

namespace atlas::editor_flow::tests
{
/// @brief Reopening leaves recovery untouched until the user chooses restore or discard.
TEST(flow_persistence, restores_incomplete_checkpoint_without_overwriting_saved_source)
{
    memory_storage disk;
    disk.files["settings.xml"] = write_xml(example());
    const auto saved = disk.files.at("settings.xml");
    {
        persistence file{"settings.xml", disk.adapter()};
        auto draft = file.edit().current();
        draft.graph.regions.front().initial = "unfinished";
        file.edit().apply(std::move(draft));
        file.checkpoint();
    }
    persistence reopened{"settings.xml", disk.adapter()};
    EXPECT_TRUE(reopened.recovery_pending());
    EXPECT_FALSE(reopened.edit().dirty());
    ASSERT_TRUE(reopened.recover());
    EXPECT_TRUE(reopened.edit().dirty());
    EXPECT_EQ(reopened.edit().current().graph.regions.front().initial, "unfinished");
    EXPECT_EQ(disk.files.at("settings.xml"), saved);
    disk.fail_write = true;
    EXPECT_THROW(reopened.save(), std::runtime_error);
    EXPECT_TRUE(reopened.edit().dirty());
    EXPECT_EQ(disk.files.at("settings.xml"), saved);
}

/// @brief Autosave cannot silently replace recovery from an earlier interrupted session.
TEST(flow_persistence, preserves_pending_recovery_and_requires_explicit_discard)
{
    memory_storage disk;
    disk.files["settings.xml"] = write_xml(example());
    auto draft = example();
    draft.layout.front().label = "Recovered caption";
    disk.files[recovery_path("settings.xml")] = write_xml(draft);
    const auto old = disk.files.at(recovery_path("settings.xml"));
    persistence file{"settings.xml", disk.adapter()};
    auto next = file.edit().current();
    next.layout.front().label = "New edit";
    file.edit().apply(std::move(next));
    EXPECT_THROW(file.checkpoint(), std::logic_error);
    EXPECT_THROW(file.close(false), std::logic_error);
    EXPECT_EQ(disk.files.at(recovery_path("settings.xml")), old);
    file.discard_recovery();
    file.checkpoint();
    EXPECT_NE(disk.files.at(recovery_path("settings.xml")), old);
    file.close(true);
    EXPECT_FALSE(disk.files.contains(recovery_path("settings.xml")));
    EXPECT_THROW((void)file.edit(), std::logic_error);
}

/// @brief Explicit recovery bypasses malformed source; cleanup failure never marks the draft saved.
TEST(flow_persistence, handles_malformed_source_and_failed_cleanup_without_data_loss)
{
    memory_storage disk;
    disk.files["settings.xml"] = "<broken>";
    disk.files[recovery_path("settings.xml")] = write_xml(example());
    EXPECT_THROW((persistence{"settings.xml", disk.adapter()}), std::invalid_argument);
    persistence file{"settings.xml", disk.adapter(), true};
    EXPECT_TRUE(file.edit().dirty());
    disk.fail_remove = true;
    EXPECT_THROW(file.save(), std::runtime_error);
    EXPECT_TRUE(file.edit().dirty());
    EXPECT_TRUE(disk.files.contains(recovery_path("settings.xml")));
    disk.fail_remove = false;
    file.save();
    EXPECT_FALSE(file.edit().dirty());
    EXPECT_FALSE(disk.files.contains(recovery_path("settings.xml")));
}

/// @brief Interrupted checkpoint replacement leaves the complete previous checkpoint recoverable.
TEST(flow_persistence, interrupted_checkpoint_and_malformed_recovery_preserve_live_state)
{
    memory_storage disk;
    persistence file{"settings.xml", example(), disk.adapter()};
    file.checkpoint();
    const auto previous = disk.files.at(recovery_path("settings.xml"));
    auto edit = file.edit().current();
    edit.layout.front().label = "New label";
    file.edit().apply(std::move(edit));
    const auto revision = file.edit().revision();
    disk.fail_write = true;
    EXPECT_THROW(file.checkpoint(), std::runtime_error);
    EXPECT_EQ(disk.files.at(recovery_path("settings.xml")), previous);
    disk.fail_write = false;
    disk.files[recovery_path("settings.xml")] = "<broken>";
    EXPECT_THROW((void)file.recover(), std::invalid_argument);
    EXPECT_EQ(file.edit().revision(), revision);
    EXPECT_EQ(file.edit().current().layout.front().label, "New label");
    file.discard_recovery();
    file.save();
    EXPECT_FALSE(file.edit().dirty());
}

/// @brief Storage callbacks may not reopen or close a persistence owner during its save.
TEST(flow_persistence, rejects_storage_callback_reentry)
{
    memory_storage disk;
    auto storage = disk.adapter();
    persistence* active = nullptr;
    const auto write = storage.write;
    storage.write = [&](const std::filesystem::path& path, std::string_view bytes)
    {
        EXPECT_THROW((void)active->edit(), std::logic_error);
        EXPECT_THROW(active->close(true), std::logic_error);
        write(path, bytes);
    };
    persistence file{"settings.xml", example(), storage};
    active = &file;
    file.save();
    EXPECT_FALSE(file.edit().dirty());
}
} // namespace atlas::editor_flow::tests
