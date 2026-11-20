/** @file @brief Stable graph history and private runtime-candidate publication. */
#include "fixture.hpp"

namespace atlas::editor_flow::tests
{
/// @brief Display rename and graph movement never replace node/edge/asset identities.
TEST(flow_session, preserves_ids_and_last_valid_runtime_while_editing_invalid_drafts)
{
    session editor{example()};
    auto initial = editor.compile();
    ASSERT_TRUE(editor.publish(initial));
    const auto previous = editor.published();
    auto edit = editor.current();
    edit.layout.front().label = "Renamed";
    edit.layout.front().x = 99;
    edit.graph.regions.front().initial = "missing";
    editor.apply(std::move(edit));
    EXPECT_TRUE(editor.dirty());
    EXPECT_THROW((void)editor.compile(), std::invalid_argument);
    EXPECT_EQ(editor.published(), previous);
    EXPECT_FALSE(editor.publish(initial));
    ASSERT_TRUE(editor.undo());
    EXPECT_FALSE(editor.dirty());
    EXPECT_EQ(editor.current().graph.screens.front().id, "home");
    ASSERT_TRUE(editor.redo());
    EXPECT_EQ(editor.current().layout.front().label, "Renamed");
    EXPECT_EQ(editor.current().layout.front().x, 99);
}

/// @brief Divergent edits retire redo and keep revision tokens unique even at the history limit.
TEST(flow_session, bounds_history_and_rejects_foreign_candidates)
{
    session editor{example(), true, 2};
    session other{example()};
    const auto foreign = other.compile();
    EXPECT_FALSE(editor.publish(foreign));
    for (int i = 0; i < 4; ++i)
    {
        auto next = editor.current();
        next.layout.front().x = i;
        editor.apply(std::move(next));
    }

    ASSERT_TRUE(editor.undo());
    ASSERT_TRUE(editor.undo());
    EXPECT_FALSE(editor.undo());
    const auto before = editor.revision();
    auto next = editor.current();
    next.layout.front().label = "Diverged";
    editor.apply(std::move(next));
    EXPECT_GT(editor.revision(), before);
    EXPECT_FALSE(editor.can_redo());
}

/// @brief A removed asset cannot replace an accepted runtime dependency snapshot.
TEST(flow_session, rejected_catalog_candidate_preserves_last_valid_runtime)
{
    session editor{example()};
    ASSERT_TRUE(editor.publish(editor.compile()));
    const auto valid = editor.published();
    EXPECT_THROW((void)editor.compile(
                     [](assets::asset_ref)
                     {
                         return false;
                     }),
                 std::invalid_argument);
    EXPECT_EQ(editor.published(), valid);
}

/// @brief A candidate must still reference available assets when the host publishes it.
TEST(flow_session, revalidates_asset_generation_at_publication)
{
    session editor{example()};
    ASSERT_TRUE(editor.publish(editor.compile()));
    const auto previous = editor.published();
    bool available = true;
    const auto candidate = editor.compile(
        [&](assets::asset_ref)
        {
            return available;
        });
    available = false;
    EXPECT_FALSE(editor.publish(candidate));
    EXPECT_EQ(editor.published(), previous);
}

/// @brief A published candidate retains its immutable catalog capability after compilation ends.
TEST(flow_session, retains_published_dependency_snapshot)
{
    session editor{example()};
    std::weak_ptr<const int> lifetime;
    {
        auto snapshot = std::make_shared<const int>(1);
        lifetime = snapshot;
        const auto candidate = editor.compile(
            [snapshot](assets::asset_ref)
            {
                return *snapshot == 1;
            });
        ASSERT_TRUE(editor.publish(candidate));
    }
    EXPECT_FALSE(lifetime.expired());
}
} // namespace atlas::editor_flow::tests
