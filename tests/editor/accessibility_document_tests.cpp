/** @file @brief Semantic authoring preserves transactional history and serialized source values. */
#include "accessibility_inspector.hpp"
#include "document_session.hpp"
#include <gtest/gtest.h>

namespace atlas::editor::tests
{
/// @brief Isolate authoring from native windows and files.
class accessibility_document_test : public testing::Test
{
  protected:
    /// @brief Named scalar draft input keeps field and text arguments unambiguous.
    struct field_text
    {
        std::string_view field; ///< Serialized field identity.
        std::string_view text;  ///< Proposed complete scalar value.
    };

    /// @brief Replace a short scalar draft through public input, without calling private writers.
    void replace_ascii(accessibility_inspector& inspector, field_text edit)
    {
        const auto& [field, text] = edit;
        ASSERT_TRUE(inspector.execute("field:" + std::string{field}, session));
        std::vector<input::event> events(
            32, input::event{.device = {1, input::device_kind::keyboard},
                             .control = static_cast<unsigned>(input::key::backspace),
                             .value = {1, 0}});
        for (const auto character : text)
        {
            events.push_back({.kind = input::event_kind::text_input,
                              .device = {1, input::device_kind::keyboard},
                              .control = static_cast<unsigned char>(character)});
        }

        events.push_back({.device = {1, input::device_kind::keyboard},
                          .control = static_cast<unsigned>(input::key::enter),
                          .value = {1, 0}});
        ASSERT_TRUE(inspector.input(events, session));
    }

    jobs::job_system workers{1}; ///< Joined after every document and storage owner.
    /// @brief Asset-free typed source mount; outlives the authored preview.
    streaming::catalog_streamer storage{
        workers,
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")}),
        {}};
    document_session session{new_document(), {storage}}; ///< Per-test authoring history.
};

/// @brief Names/help and role changes serialize and undo as a single validated edit.
TEST_F(accessibility_document_test, DescriptorRoundTripsAndRestoresSelection)
{
    session.select("element_0");
    session.mark_saved();
    const ui::accessible_element descriptor{
        .role = accessibility::role::button, .name = "Apply", .description = "Save settings"};
    ASSERT_TRUE(session.set_accessibility(descriptor, false));
    EXPECT_FALSE(session.set_accessibility(descriptor, false));
    const auto restored = ui_content::parse_xml({ui_content::write_xml(session.document())});
    EXPECT_EQ(restored.elements.back().accessible, descriptor);
    ASSERT_TRUE(session.undo());
    EXPECT_FALSE(session.dirty());
    EXPECT_EQ(session.selected(), "element_0");
    EXPECT_FALSE(session.document().elements.back().accessible);
    ASSERT_TRUE(session.redo());
    EXPECT_EQ(session.document().elements.back().accessible, descriptor);
}

/// @brief Invalid semantics cannot alter source, selection, dirty state or history.
TEST_F(accessibility_document_test, InvalidEditsLeaveThePriorRevisionIntact)
{
    session.mark_saved();
    const auto before = ui_content::write_xml(session.document());
    ui::accessible_element invalid{.name = "literal", .name_key = "localized"};
    EXPECT_THROW(session.set_accessibility(invalid, false), std::invalid_argument);
    EXPECT_THROW(session.set_accessibility(ui::accessible_element{}, true), std::invalid_argument);
    EXPECT_EQ(ui_content::write_xml(session.document()), before);
    EXPECT_FALSE(session.dirty());
    EXPECT_FALSE(session.can_undo());
}

/// @brief Editing a Unicode field stays private until Apply and creates exactly one undo step.
TEST_F(accessibility_document_test, InspectorStagesUnicodeUntilApply)
{
    session.mark_saved();
    accessibility_inspector inspector;
    ASSERT_TRUE(inspector.execute("mode", session));
    ASSERT_TRUE(inspector.execute("field:name", session));
    const std::array events{input::event{.kind = input::event_kind::text_input,
                                         .device = {1, input::device_kind::keyboard},
                                         .control = 0xe9},
                            input::event{.device = {1, input::device_kind::keyboard},
                                         .control = static_cast<unsigned>(input::key::enter),
                                         .value = {1, 0}}};
    ASSERT_TRUE(inspector.input(events, session));
    EXPECT_FALSE(inspector.editing());
    EXPECT_FALSE(session.dirty());
    ASSERT_TRUE(inspector.execute("selected", session));
    ASSERT_TRUE(inspector.execute("checked", session));
    ASSERT_TRUE(inspector.execute("apply", session));
    const auto& descriptor = session.document().elements.front().accessible;
    if (!descriptor.has_value())
    {
        ADD_FAILURE() << "Applied accessibility descriptor is missing";
        return;
    }

    const auto& value = descriptor.value();
    EXPECT_EQ(value.name, "\xc3\xa9");
    EXPECT_TRUE(value.selected);
    EXPECT_TRUE(value.checked);
    EXPECT_TRUE(session.undo());
    EXPECT_FALSE(session.can_undo());
    EXPECT_FALSE(session.dirty());
}

/// @brief Cancelled or stale text drafts never change a different selected widget.
TEST_F(accessibility_document_test, InspectorCancelsOnSelectionAndEscape)
{
    session.mark_saved();
    accessibility_inspector inspector;
    ASSERT_TRUE(inspector.execute("mode", session));
    ASSERT_TRUE(inspector.execute("field:name", session));
    session.select("element_0");
    EXPECT_FALSE(inspector.input({}, session));
    EXPECT_FALSE(inspector.editing());
    ASSERT_TRUE(inspector.execute("mode", session));
    ASSERT_TRUE(inspector.execute("field:name", session));
    const input::event escape{.device = {1, input::device_kind::keyboard},
                              .control = static_cast<unsigned>(input::key::escape),
                              .value = {1, 0}};
    EXPECT_TRUE(inspector.input({&escape, 1}, session));
    EXPECT_FALSE(inspector.editing());
    EXPECT_FALSE(session.dirty());
}

/// @brief Typed fields and localization keys commit together while preserving literal/key
/// exclusivity.
TEST_F(accessibility_document_test, InspectorAppliesTypedFieldsAsOneRevision)
{
    auto source = new_document();
    source.elements.front().interaction = ui::interaction_options{.focusable = true};
    session.replace(std::move(source), true);
    accessibility_inspector inspector;
    ASSERT_TRUE(inspector.execute("mode", session));
    const std::array fields{std::pair{"name", "Apply"},
                            std::pair{"description", "Save settings"},
                            std::pair{"value", "Ready"},
                            std::pair{"invoke-action", "42"},
                            std::pair{"select-action", "43"},
                            std::pair{"number", "0.5"},
                            std::pair{"minimum", "0"},
                            std::pair{"maximum", "1"},
                            std::pair{"name-key", "settings.apply"},
                            std::pair{"description-key", "settings.help"}};
    for (const auto& [field, text] : fields)
    {
        SCOPED_TRACE(field);
        replace_ascii(inspector, {field, text});
        ASSERT_FALSE(inspector.editing());
    }

    ASSERT_TRUE(inspector.execute("apply", session));
    const auto& descriptor = session.document().elements.front().accessible;
    if (!descriptor)
    {
        ADD_FAILURE() << "Missing applied descriptor";
        return;
    }

    EXPECT_TRUE(descriptor->name.empty());
    EXPECT_TRUE(descriptor->description.empty());
    EXPECT_EQ(descriptor->name_key, "settings.apply");
    EXPECT_EQ(descriptor->description_key, "settings.help");
    EXPECT_EQ(descriptor->value, "Ready");
    EXPECT_EQ(descriptor->invoke_action, 42);
    EXPECT_EQ(descriptor->select_action, 43);
    EXPECT_DOUBLE_EQ(descriptor->number, 0.5);
    EXPECT_DOUBLE_EQ(descriptor->minimum, 0);
    EXPECT_DOUBLE_EQ(descriptor->maximum, 1);
    EXPECT_TRUE(session.undo());
    EXPECT_FALSE(session.can_undo());
}

/// @brief Invalid numeric text remains an uncommitted draft, and decorative mode clears overrides.
TEST_F(accessibility_document_test, InspectorRejectsInvalidScalarsAndCanExcludeDecoration)
{
    session.mark_saved();
    accessibility_inspector inspector;
    ASSERT_TRUE(inspector.execute("mode", session));
    replace_ascii(inspector, {"invoke-action", "-1"});
    EXPECT_TRUE(inspector.editing());
    EXPECT_THROW(inspector.execute("apply", session), std::logic_error);
    EXPECT_FALSE(session.dirty());
    ASSERT_TRUE(inspector.execute("mode", session));
    ASSERT_TRUE(inspector.execute("apply", session));
    EXPECT_TRUE(session.document().elements.front().decorative);
    EXPECT_FALSE(session.document().elements.front().accessible);
}

/// @brief Pagination and draft navigation stay within the inspector without editing the document.
TEST_F(accessibility_document_test, InspectorPaginatesWithinItsAssignedPanel)
{
    session.mark_saved();
    accessibility_inspector inspector;
    inspector.visible = true;
    ASSERT_TRUE(inspector.execute("mode", session));
    ASSERT_TRUE(inspector.execute("role", session));
    for (unsigned page = 0; page < 3; ++page)
    {
        panel_plan plan;
        plan.inspector = {600, 40, 200, 400};
        inspector.append(plan, session);
        ASSERT_FALSE(plan.controls.empty());
        for (const auto& control : plan.controls)
        {
            EXPECT_GE(control.bounds.x, 600);
            EXPECT_GE(control.bounds.y, 40);
            EXPECT_LE(control.bounds.y + control.bounds.height, 440);
        }

        ASSERT_TRUE(inspector.execute("next", session));
    }

    EXPECT_FALSE(session.dirty());
}
} // namespace atlas::editor::tests
