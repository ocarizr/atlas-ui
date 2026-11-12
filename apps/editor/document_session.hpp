/** @file @brief Editor-owned commands, stable selection and isolated preview publication. */
#pragma once
#include <atlas/ui_content/instance.hpp>

namespace atlas::editor
{
/// @brief Bounded operations available to the inspector and hierarchy.
enum class edit_kind : std::uint8_t
{
    add_child,     ///< Append a generic painted leaf to the selected panel.
    remove,        ///< Remove the selected subtree, except the document root.
    narrower,      ///< Reduce the selected element's requested width.
    wider,         ///< Increase the selected element's requested width.
    toggle_visible ///< Change the selected element's authored visibility.
};

/// @brief Create the generic viewport document used for a new editor session.
[[nodiscard]] ui_content::document new_document();

/// @brief One editor owner retains authored values and builds a separate preview candidate.
class document_session
{
  public:
    /// @brief Build the initial preview from an authored document.
    document_session(ui_content::document source, const ui_content::construction& options);
    [[nodiscard]] const ui_content::document&
    document() const noexcept;                                ///< Current authoring values.
    [[nodiscard]] std::string_view selected() const noexcept; ///< Stable selected element ID.
    [[nodiscard]] ui::element_tree& preview(); ///< The isolated runtime tree used for presentation.
    [[nodiscard]] bool dirty() const noexcept; ///< Current revision differs from the last save.
    [[nodiscard]] bool can_undo() const noexcept; ///< Whether an earlier edit remains retained.
    [[nodiscard]] bool can_redo() const noexcept; ///< Whether an undone edit remains retained.
    void select(std::string_view id);             ///< Selection alone creates no history entry.
    bool apply(edit_kind command); ///< Validate and atomically publish a single authored edit.
    bool undo();                   ///< Restore a retained authored revision and rebuild preview.
    bool redo();                   ///< Reapply a retained authored revision and rebuild preview.
    void replace(ui_content::document source, bool saved); ///< Publish valid replacement only.
    void mark_saved() noexcept; ///< Record the current revision after persistence succeeds.

  private:
    /// @brief A history entry owns authored values independently from the runtime state.
    struct revision
    {
        ui_content::document source; ///< Editable values owned by one history entry.
        std::string selected;        ///< Selection to restore when this entry becomes active.
        std::uint64_t id = 0;        ///< Unique revision identity for persistence state.
    };

    /// @brief Validate a private candidate without changing active state.
    [[nodiscard]] std::unique_ptr<ui_content::instance>
    make_preview(const ui_content::document& source);
    [[nodiscard]] std::uint64_t allocate_revision(); ///< Allocate a fresh persistence identity.
    ui_content::construction options_; ///< Stable streamer reference and retained services.
    revision current_;                 ///< Current authoring and selection.
    std::unique_ptr<ui_content::instance> preview_; ///< Independent live UI state.
    std::uint64_t saved_ = 0;                       ///< Identity of the last successful save.
    std::uint64_t next_revision_ = 1;               ///< Nonreused revision identity.
    std::vector<revision> undo_; ///< Oldest to newest, with a bounded retention policy.
    std::vector<revision> redo_; ///< Oldest to newest undo results.
    std::uint64_t next_id_ = 1;  ///< Monotonic generated suffix within this session.
};
} // namespace atlas::editor
