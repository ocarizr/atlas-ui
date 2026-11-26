/**
 * @file
 * @brief Shared owner-frame composition for independent accessible UI views.
 */
#pragma once
#include <atlas/ui/input_session.hpp>

namespace atlas::ui
{
/// @brief Borrow a window channel for one serial frame; no input or native pointers are retained.
class accessibility_frame
{
  public:
    accessibility_frame(accessibility::channel& channel, accessibility_view coordinates = {},
                        std::uint64_t focus_owner = 0); ///< Drain ready requests on their owner.
    void route(input_session& input); ///< Revalidate requests in an already open input phase.
    /// @brief Capture after input and before closing the matching presentation phase.
    void capture(input_session& input, const accessible_text& resolve = {});
    void publish(); ///< Normalize focus and atomically replace only changed semantic values.
    std::uint64_t focus_owner() const noexcept; ///< Retain this value between application frames.
    void
    focus_owner(std::uint64_t session) noexcept; ///< Transfer a rebuilt host's focus namespace.
    std::span<const accessibility::request> requests() const noexcept; ///< Borrow this frame only.
    void clear(); ///< Suppress all views when minimized, replaced or unavailable.
    std::size_t
    checkpoint() const noexcept;           ///< Mark contributions before a replaceable view group.
    void rollback(std::size_t checkpoint); ///< Retire a replaced group's captured identities.
  private:
    accessibility::channel& channel_; ///< Owner outlives this frame.
    accessibility_view coordinates_;  ///< Physical client transform captured on the window owner.
    std::vector<accessibility::request> requests_; ///< Bounded native ingress, drained once.
    std::vector<accessibility::node> nodes_;       ///< Owned active-view contributions.
    std::vector<element_handle> previous_focus_;   ///< Small per-view owner observations.
    std::uint64_t focus_owner_ = 0; ///< Exclusive keyboard scope across independent trees.
    std::size_t focus_request_ = 0; ///< Last accepted focus request, preserving cross-view FIFO.
};
} // namespace atlas::ui
