/**
 * @file
 * @brief Transactional graph editing and transient viewport gestures on one owner.
 */
#pragma once
#include <atlas/editor_flow/session.hpp>

namespace atlas::editor_flow
{
/// @brief Finite graph/view coordinates independent of platform pixels.
struct point
{
    double x = 0;                                  ///< Horizontal coordinate.
    double y = 0;                                  ///< Vertical coordinate.
    bool operator==(const point&) const = default; ///< Exact transform comparison.
};

/// @brief Reusable tool state borrows a history owner; cheap gestures never spawn CPU jobs.
class workspace
{
  public:
    explicit workspace(session& owner);              ///< Owner must outlive the tool.
    [[nodiscard]] session& edit() const;             ///< Enforce the session's thread affinity.
    void select(item_ref item, bool extend = false); ///< Transient bounded multi-selection.
    [[nodiscard]] std::vector<item_ref> selection() const;    ///< Prune removed items after undo.
    [[nodiscard]] point position(const item_ref& item) const; ///< Include uncommitted drag delta.
    void begin_move();                  ///< Capture selected layout and exact authored revision.
    void move_by(point delta);          ///< Replace graph-space displacement from gesture origin.
    void end_move(bool cancel = false); ///< One revision on release, none on cancel.
    void pan(point delta);              ///< View-only movement does not dirty the document.
    void zoom_at(double factor, point anchor);       ///< Keep the anchor's graph coordinate fixed.
    [[nodiscard]] point to_graph(point view) const;  ///< Inverse view transform.
    [[nodiscard]] point to_view(point graph) const;  ///< Forward view transform.
    [[nodiscard]] double zoom() const;               ///< Bounded scale from 0.25 to 4.
    void add_region(ui_content::flow_region region); ///< Add an editable region declaration.
    void add_screen(ui_content::flow_screen screen, point at,
                    std::string label = {});            ///< Reject incompatible asset kinds.
    void connect(ui_content::flow_transition route);    ///< Stable ID, same-region permissions.
    void update_region(ui_content::flow_region region); ///< Preserve region identity.
    void update_screen(ui_content::flow_screen screen); ///< Preserve node identity.
    void update_transition(ui_content::flow_transition route); ///< Preserve route identity.
    void rename(item_ref item, std::string label); ///< Captions never rename references.
    void erase_selection(); ///< Remove selected declarations and dependent links atomically.
    /// @brief Only outgoing permissions, in authored order.
    [[nodiscard]] std::vector<ui_content::flow_transition> routes(std::string_view source) const;

  private:
    session& owner_;                 ///< Single coordinator owner.
    std::vector<item_ref> selected_; ///< Transient selection is not an edit history stream.
    point pan_{};                    ///< Graph origin in view coordinates.
    double zoom_ = 1;                ///< Finite bounded scale.
    point delta_{};                  ///< Pending graph-space displacement.
    std::uint64_t gesture_ = 0;      ///< Exact source revision, zero when idle.
};
} // namespace atlas::editor_flow
