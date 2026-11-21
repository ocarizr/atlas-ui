/**
 * @file
 * @brief Shared viewport composition and exclusive input routing for Flow presentations.
 */
#pragma once
#include <atlas/ui/paint_engine.hpp>
#include <atlas/ui_content/flow.hpp>

namespace atlas::ui_content
{
/// @brief Explicit host clock and semantic Back mapping avoid interchangeable scalar arguments.
struct flow_frame_options
{
    float delta_seconds = 0;       ///< Finite nonnegative monotonic elapsed time.
    std::uint32_t back_action = 0; ///< Optional performed semantic action; Escape also works.
};

/// @brief Owner-only presentation adapter; the manager and shared engines outlive this object.
class flow_viewport
{
  public:
    flow_viewport(flow_manager& flow, ui::layout_engine& layout, ui::paint_engine& paint,
                  diagnostics::services sinks = {}); ///< Borrow application-owned services.
    ~flow_viewport();                                ///< Release immutable geometry on its owner.
    /// @brief Arrange nested hosts, route real/replayed input, close phases, then paint in order.
    [[nodiscard]] std::vector<ui::paint_quad> frame(std::uint64_t frame, ui::layout_rect bounds,
                                                    std::span<const ui::input_event> events = {},
                                                    flow_frame_options options = {});
    flow_viewport(const flow_viewport&) = delete; ///< Input capture belongs to one coordinator.
    flow_viewport& operator=(const flow_viewport&) = delete; ///< Never share mutable routing state.
  private:
    class impl;                  ///< Geometry, focus ownership and dismissal gesture state.
    std::unique_ptr<impl> impl_; ///< No native SDK or separate job pool.
};
} // namespace atlas::ui_content
