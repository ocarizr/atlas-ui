/** @file @brief Value-only Atlas editor panel and control descriptions. */
#pragma once
#include "document_session.hpp"

namespace atlas::editor
{
inline constexpr std::size_t hierarchy_page_size = 12; ///< Rows shown per hierarchy page.

/// @brief Named routes produced by editor controls.
enum class control_kind : std::uint8_t
{
    none,           ///< Passive decoration.
    select,         ///< Select by authored identity.
    add_child,      ///< Append a generic leaf.
    remove,         ///< Remove a subtree.
    narrower,       ///< Reduce requested width.
    wider,          ///< Increase requested width.
    toggle_visible, ///< Toggle authored visibility.
    undo,           ///< Restore previous revision.
    redo,           ///< Reapply a retained revision.
    new_document,   ///< Replace with the generic initial document.
    save,           ///< Persist the active document.
    reopen,         ///< Reload the selected file.
    previous,       ///< Browse the preceding hierarchy page.
    next            ///< Browse the next hierarchy page.
};

/// @brief Owned command materialized from a control after pointer release.
struct control_action
{
    control_kind kind = control_kind::none; ///< Command vocabulary.
    std::string target;                     ///< Stable element ID for selection.
};

/// @brief One chrome rectangle and optional shaped label.
struct control
{
    ui::layout_rect bounds;     ///< Logical rectangle.
    std::array<float, 4> color; ///< Linear panel or control background.
    std::string label;          ///< User-facing caption.
    control_action action{};    ///< Optional pointer action.
};

/// @brief Shared output of hierarchy, toolbar, inspector, and preview arrangement.
struct panel_plan
{
    std::vector<control> controls; ///< Insertion order, back to front.
    ui::layout_rect preview;       ///< Clip and viewport for the isolated runtime tree.
};

/// @brief Build chrome for the currently selected revision and viewport.
[[nodiscard]] panel_plan make_panels(const document_session& session, ui::layout_extent size,
                                     std::size_t page, std::string_view status);
} // namespace atlas::editor
