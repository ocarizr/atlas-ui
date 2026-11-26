/** @file @brief Editor hierarchy moves are atomic, validated and one undo step. */
#include "document_session.hpp"
#include <gtest/gtest.h>

namespace atlas::editor::tests
{
/// @brief Isolate drag participants, callbacks and document history for each test.
class drag_document_test : public testing::Test
{
  protected:
    void SetUp() override
    {
        auto document = new_document();
        ui_content::element panel;
        panel.id = "panel";
        panel.parent = "root";
        panel.panel = ui::vertical_layout{};
        document.elements.push_back(panel);
        session.replace(std::move(document), true);
    } ///< Reproducible source and destination without external assets.

    jobs::job_system workers{1}; ///< Outlives mount and preview.
    streaming::catalog_streamer storage{
        workers,
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")}),
        {}};                                             ///< Empty source mount.
    document_session session{new_document(), {storage}}; ///< Isolated history.
};

/// @brief Reparents in one undo and preserves stable identity.
TEST_F(drag_document_test, ReparentsInOneUndoAndPreservesStableIdentity)
{
    const auto before = ui_content::write_xml(session.document());
    ASSERT_TRUE(session.can_reparent("element_0", "panel"));
    ASSERT_TRUE(session.reparent("element_0", "panel"));
    EXPECT_EQ(session.selected(), "element_0");
    EXPECT_EQ(session.document().elements.back().id, "element_0");
    EXPECT_EQ(session.document().elements.back().parent, "panel");
    ASSERT_TRUE(session.undo());
    EXPECT_EQ(ui_content::write_xml(session.document()), before);
    EXPECT_FALSE(session.can_undo());
    EXPECT_FALSE(session.dirty());
    ASSERT_TRUE(session.redo());
    EXPECT_EQ(session.document().elements.back().parent, "panel");
}

/// @brief Rejects root leaf cycle and no op without history.
TEST_F(drag_document_test, RejectsRootLeafCycleAndNoOpWithoutHistory)
{
    const auto before = ui_content::write_xml(session.document());
    EXPECT_FALSE(session.reparent("root", "panel"));
    EXPECT_FALSE(session.reparent("panel", "element_0"));
    EXPECT_FALSE(session.reparent("element_0", "root"));
    EXPECT_FALSE(session.reparent("panel", "panel"));
    EXPECT_FALSE(session.reparent("missing", "panel"));
    EXPECT_EQ(ui_content::write_xml(session.document()), before);
    EXPECT_FALSE(session.can_undo());
    EXPECT_FALSE(session.dirty());
}
} // namespace atlas::editor::tests
