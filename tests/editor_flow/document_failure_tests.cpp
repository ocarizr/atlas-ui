/** @file @brief Strict draft syntax and bounded ownership invariants on failing paths. */
#include "fixture.hpp"
#include <limits>
#include <thread>

namespace atlas::editor_flow::tests
{
/// @brief Repeated invalid inputs share a fresh fixture without mutable state between cases.
class invalid_xml_test : public testing::TestWithParam<const char*>
{
};

/// @brief No malformed, unsupported or ambiguous XML is silently normalized into a valid draft.
TEST_P(invalid_xml_test, rejects_unsupported_or_malformed_content)
{
    EXPECT_THROW((void)read_xml({GetParam()}), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(
    drafts, invalid_xml_test,
    testing::Values(
        "<flow-authoring version='9' id='graph'/>",
        "<!DOCTYPE flow-authoring><flow-authoring version='1' id='graph'/>",
        "<flow-authoring version='1' id='graph' unknown='true'/>",
        "<flow-authoring version='1' version='1' id='graph'/>",
        "<flow-authoring version='1' id='graph'><selection kind='0' id=''/><selection kind='0' "
        "id=''/></flow-authoring>",
        "<flow-authoring version='1' id='graph'><region id='x' initial='' owner='' host='' "
        "remember='1'/></flow-authoring>",
        "<flow-authoring version='1' id='graph'>unexpected text</flow-authoring>",
        "<flow-authoring version='1' id='graph'/><flow-authoring version='1' id='other'/>"));

/// @brief Failed drafts leave the current revision and its history unchanged.
TEST(flow_document, rejects_duplicate_ids_invalid_metadata_and_selection_without_mutation)
{
    session editor{example()};
    const auto revision = editor.revision();
    auto duplicate = editor.current();
    duplicate.graph.screens.push_back(duplicate.graph.screens.front());
    EXPECT_THROW(editor.apply(std::move(duplicate)), std::invalid_argument);
    auto nonfinite = editor.current();
    nonfinite.layout.front().x = std::numeric_limits<double>::infinity();
    EXPECT_THROW(editor.apply(std::move(nonfinite)), std::invalid_argument);
    auto selection = editor.current();
    selection.selection.id = "missing";
    EXPECT_THROW(editor.apply(std::move(selection)), std::invalid_argument);
    EXPECT_EQ(editor.revision(), revision);
    EXPECT_FALSE(editor.can_undo());
    EXPECT_FALSE(editor.dirty());
}

/// @brief Validation callbacks cannot mutate a session whose private candidate is being compiled.
TEST(flow_session, rejects_foreign_threads_and_reentrant_catalog_callbacks)
{
    session editor{example()};
    bool rejected = false;
    std::jthread thread{[&]
                        {
                            try
                            {
                                (void)editor.current();
                            }
                            catch (const std::logic_error&)
                            {
                                rejected = true;
                            }
                        }};
    thread.join();
    EXPECT_TRUE(rejected);
    EXPECT_THROW((void)editor.compile(
                     [&](assets::asset_ref)
                     {
                         editor.mark_saved();
                         return true;
                     }),
                 std::logic_error);
    EXPECT_NO_THROW((void)editor.compile());
}

/// @brief A saveable draft cannot publish a runtime graph that exceeds the runtime codec budget.
TEST(flow_session, rejects_runtime_serialization_budget_without_losing_published_state)
{
    session editor{example()};
    ASSERT_TRUE(editor.publish(editor.compile()));
    const auto previous = editor.published();
    auto large = editor.current();
    const std::string text(4096, 'x');
    large.graph.parameters = {{"text", text}};
    for (int i = 0; i < 140; ++i)
    {
        large.graph.transitions.push_back({.id = "route_" + std::to_string(i),
                                           .from = "home",
                                           .operation = ui_content::flow_operation::reload,
                                           .guard = ui_content::flow_guard{"text", text}});
    }

    editor.apply(std::move(large));
    EXPECT_THROW((void)editor.compile(), std::length_error);
    EXPECT_EQ(editor.published(), previous);
}
} // namespace atlas::editor_flow::tests
