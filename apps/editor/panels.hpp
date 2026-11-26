/** @file @brief Value-only Atlas editor panel and control descriptions. */
#pragma once
#include "document_session.hpp"
#include <atlas/ui/paint_engine.hpp>

namespace atlas::editor
{
inline constexpr std::size_t hierarchy_page_size = 12; ///< Rows shown per hierarchy page.

/// @brief Named routes produced by editor controls.
enum class control_kind : std::uint8_t
{
    none,             ///< Passive decoration.
    select,           ///< Select by authored identity.
    add_child,        ///< Append a generic leaf.
    remove,           ///< Remove a subtree.
    narrower,         ///< Reduce requested width.
    wider,            ///< Increase requested width.
    toggle_visible,   ///< Toggle authored visibility.
    undo,             ///< Restore previous revision.
    redo,             ///< Reapply a retained revision.
    new_document,     ///< Replace with the generic initial document.
    save,             ///< Persist the active document.
    reopen,           ///< Reload the selected file.
    previous,         ///< Browse the preceding hierarchy page.
    next,             ///< Browse the next hierarchy page.
    cycle_tool,       ///< Cycle preview and registered tool contributions.
    cycle_document,   ///< Select the next independent session.
    close_document,   ///< Preserve active edits to recovery before closing.
    recover_document, ///< Explicitly restore recovery without replacing the source file.
    assets_toggle,    ///< Toggle the typed asset panel.
    assets_type,      ///< Cycle the asset category.
    assets_select,    ///< Select a stable asset ID.
    assets_import,    ///< Import or register a source.
    assets_move,      ///< Move a source preserving identity.
    assets_relink,    ///< Replace a missing source.
    assets_refresh,   ///< Detect changed bytes.
    assets_remove,    ///< Remove unreferenced registration.
    assets_undo,      ///< Undo an asset catalog edit.
    assets_redo,      ///< Redo an asset catalog edit.
    assets_cancel,    ///< Cancel source preparation.
    assets_open,      ///< Open an authored UI asset.
    assets_assign,    ///< Assign a typed element reference.
    assets_next,      ///< Cycle the next page.
    assets_settings,  ///< Advance importer settings.
    flow_toggle,      ///< Switch between the graph workspace and ordinary UI authoring.
    drop_asset,       ///< Assign a dragged stable asset reference.
    accessibility,    ///< Stage or apply a dedicated accessibility inspector edit.
    flow              ///< Typed graph/inspector command, applied after Atlas input dispatch.
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
    ui::layout_rect bounds;                               ///< Logical rectangle.
    std::array<float, 4> color;                           ///< Linear panel or control background.
    std::string label;                                    ///< User-facing caption.
    control_action action{};                              ///< Optional pointer action.
    std::optional<ui::drag_source_options> source{};      ///< Immutable drag offer.
    std::optional<ui::drop_target_options> destination{}; ///< Typed drop policy.
    std::function<bool(const ui::drag_event&)> accepts{}; ///< Pure dynamic target guard.
};

/// @brief Shared output of hierarchy, toolbar, inspector, and preview arrangement.
struct panel_plan
{
    ui::layout_rect inspector{};            ///< Dedicated authoring inspector bounds.
    std::vector<control> controls;          ///< Insertion order, back to front.
    ui::layout_rect preview;                ///< Clip and viewport for the isolated runtime tree.
    ui::paint_snapshot shell;               ///< Authored editor background, panels and controls.
    std::optional<ui::paint_snapshot> tool; ///< Active authored contribution in the center.
    std::optional<std::vector<ui::paint_quad>>
        flow_preview{}; ///< Composed Flow layers, back to front.
};

/// @brief Named slots in the installed self-authored shell.
struct shell_areas
{
    ui::layout_rect hierarchy; ///< Hierarchy display bounds.
    ui::layout_rect inspector; ///< Inspector display bounds.
    ui::layout_rect preview;   ///< Preview and tool viewport bounds.
};

/// @brief Build chrome for the currently selected revision and viewport.
[[nodiscard]] panel_plan make_panels(const document_session& session, ui::layout_extent size,
                                     std::size_t page, std::string_view status,
                                     const shell_areas* shell = nullptr);
} // namespace atlas::editor
