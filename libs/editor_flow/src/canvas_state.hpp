/** @file @brief Graph canvas input state shared by routing and presentation. */
#pragma once
#include <atlas/editor_flow/canvas.hpp>
#include <map>

namespace atlas::editor_flow
{
/// @brief Input callbacks collect value-only intents; authoring mutation happens after dispatch.
struct canvas_intent
{
    item_ref item;       ///< Empty item refers to background panning.
    ui::event_kind kind; ///< Down/move/up/cancel vocabulary.
    point position;      ///< Logical view-space pointer position.
};

/// @brief One persistent input tree owns stable node registrations across repaint.
class canvas::impl
{
  public:
    impl(workspace& source, diagnostics::services destinations,
         std::shared_ptr<std::pmr::memory_resource> memory); ///< Allocate bounded tree services.
    ui::element_handle create(item_ref item);                ///< Bind one graph gesture handler.
    void rect(ui::element_handle node, ui::layout_rect area, std::array<float, 4> color,
              std::int32_t layer = 0); ///< Update a standard Atlas canvas child.
    void connections();                ///< Build clipped elbow links behind nodes.
    bool apply(std::span<const canvas_intent> commands, bool extend,
               bool pan);                            ///< Outside dispatch.
    workspace& tool;                                 ///< Borrowed history owner.
    diagnostics::services sinks;                     ///< Optional capture/logging.
    std::vector<canvas_intent> pending;              ///< One bounded input batch.
    ui::element_tree tree;                           ///< Persistent logical graph nodes.
    ui::input_session input;                         ///< Destroy before the tree and pending queue.
    ui::element_handle root;                         ///< Viewport-clipped background.
    std::map<std::string, ui::element_handle> nodes; ///< Stable screen ID to live UI node.
    std::vector<ui::element_handle> edges;           ///< Reused line-segment leaves.
    std::vector<canvas_label> labels;                ///< Current host text geometry.
    ui::element_snapshot source;                     ///< Matching immutable hit-test snapshot.
    ui::layout_snapshot geometry;                    ///< Matching immutable geometry.
    ui::layout_rect bounds{};                        ///< Viewport in logical DIPs.
    std::uint64_t frame = 0;                         ///< Increasing Atlas input frame.
    std::optional<point> origin;                     ///< View-space pointer at gesture start.
    point last{};                                    ///< Previous background pan coordinate.
    bool moving = false;                             ///< Gesture owns one pending node edit.
    bool panning = false;                            ///< View-only gesture.
};
} // namespace atlas::editor_flow
