/** @file @brief Staged accessibility authoring with bounded committed-Unicode field drafts. */
#pragma once
#include "panels.hpp"
#include <atlas/input/text_entry.hpp>

namespace atlas::editor
{
/// @brief Dedicated inspector edits stay private until one validated Apply transaction.
class accessibility_inspector
{
  public:
    bool visible = false; ///< Explicit inspector mode; independent of document semantics.
    void append(panel_plan& plan, const document_session& session);    ///< Paginated controls.
    bool execute(std::string_view command, document_session& session); ///< Deferred owner action.
    /// @brief Consume text drafts before shortcuts; Enter commits and Escape cancels.
    bool input(std::span<const input::event> events, document_session& session);
    ///< Consume text editing input before ordinary shortcuts; Enter commits, Escape cancels.
    bool editing() const noexcept; ///< Suppress navigation while a field draft owns keyboard input.
  private:
    void
    synchronize(const document_session& session); ///< Retire drafts on selection/revision change.
    std::string value(std::string_view field) const; ///< Present one supported scalar value.

    /// @brief Named field/value transport avoids interchangeable string-view parameters.
    struct field_edit
    {
        std::string_view field; ///< Serialized semantic field name.
        std::string_view value; ///< Complete draft value; borrowed during assignment only.
    };

    void assign(field_edit edit);  ///< Validate a scalar draft edit.
    ui::accessible_element draft_; ///< Editable descriptor, separate from the source document.
    std::string element_;          ///< Stable selected asset-local identity.
    std::uint64_t revision_ = 0;   ///< Prevent stale drafts from overwriting intervening edits.
    unsigned mode_ = 0;            ///< Zero inferred, one explicit, two decorative.
    std::size_t page_ = 0;         ///< Bounded field page.
    std::string field_;            ///< Active staged text field; empty outside editing.
    input::text_entry text_;       ///< Bounded UTF-8 temporary edit.
    std::string status_;           ///< Last field/apply diagnostic.
};
} // namespace atlas::editor
