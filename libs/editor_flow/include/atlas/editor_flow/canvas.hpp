/**
 * @file
 * @brief Reusable Atlas graph surface with captured node gestures and clipped connections.
 */
#pragma once
#include <atlas/editor_flow/workspace.hpp>
#include <atlas/ui/paint_engine.hpp>

namespace atlas::editor_flow
{
/// @brief Value-only label geometry for host text rendering.
struct canvas_label
{
    item_ref item;          ///< Stable node identity.
    std::string text;       ///< Display caption independent of identity.
    ui::layout_rect bounds; ///< Clipped logical view geometry.
};

/// @brief Persistent Atlas input registrations survive repaint and viewport changes.
class canvas
{
  public:
    explicit canvas(
        workspace& tool, diagnostics::services sinks = {},
        std::shared_ptr<std::pmr::memory_resource> memory = {}); ///< Borrow one history tool.
    ~canvas();                                 ///< Release handlers before the borrowed workspace.
    canvas(const canvas&) = delete;            ///< Unique capture owner.
    canvas& operator=(const canvas&) = delete; ///< No shared mutable input state.
    [[nodiscard]] ui::paint_snapshot
    arrange(ui::layout_rect bounds, ui::layout_engine& layout,
            ui::paint_engine& paint); ///< Reuse the application's shared CPU services.
    [[nodiscard]] bool
    process(std::span<const ui::input_event> events, bool extend = false,
            bool pan = false); ///< Dispatch then apply gesture commands outside Atlas callbacks.
    [[nodiscard]] std::span<const canvas_label> labels() const; ///< Current clipped labels.
  private:
    class impl;                  ///< Portable tree, input lifetime and pending gesture values.
    std::unique_ptr<impl> impl_; ///< No native SDK or graphics objects.
};
} // namespace atlas::editor_flow
