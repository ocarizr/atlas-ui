/** @file @brief Private owner routing and nested geometry for the shared Flow viewport. */
#pragma once
#include <atlas/ui_content/flow_viewport.hpp>
#include <map>
#include <set>

namespace atlas::ui_content
{
/// @brief Rebuilt bounded presentation metadata never outlives a manager publication.
class flow_viewport::impl
{
  public:
    /// @brief Share layout/paint coordinators without creating workers.
    impl(flow_manager& manager, ui::layout_engine& layouts, ui::paint_engine& paints,
         diagnostics::services destinations)
        : flow{manager}, layout{layouts}, paint{paints}, sinks{std::move(destinations)}
    {
    }

    /// @brief One presented view's immutable input and paint geometry.
    struct layer
    {
        flow_presentation item;       ///< Borrowed only during this closed manager frame.
        ui::element_snapshot source;  ///< Exact tree snapshot used by input.
        ui::layout_snapshot geometry; ///< Mapped logical viewport.
        ui::layout_rect bounds;       ///< Visible host/popup hit boundary.
        float opacity = 1; ///< Inherited host opacity outside this independently authored tree.
    };

    void arrange(ui::layout_rect bounds); ///< Parent-first host mapping and clipping.
    void activate(layer& entry, std::uint64_t frame,
                  float seconds);                   ///< Track host visibility and lifecycle.
    void begin(std::uint64_t frame, float seconds); ///< Open eligible frames and select focus.
    void accessibility_begin(const flow_frame_options& options); ///< Owner-phase native admission.
    /// @brief Keep one keyboard owner across the preview and independent editor chrome.
    void accessibility_route(const ui::input_event& event, const flow_frame_options& options);
    ///< One window-level keyboard owner, including independent editor chrome.
    void accessibility_capture(const flow_frame_options& options); ///< Active layer snapshots.
    void finish(); ///< Close serial phases before immutable paint preparation.
    void route_pointer(const ui::input_event& event); ///< Front-to-back pointer admission.
    void route(const ui::input_event& event, std::uint32_t back_action); ///< Exactly one recipient.
    void deliver(std::size_t target,
                 const ui::input_event& event); ///< Clear hover in other layers.
    void focus(std::size_t selected); ///< Preserve one keyboard owner, including modeless layers.
    flow_manager& flow;               ///< Owner-thread publication service.
    ui::layout_engine& layout;        ///< Shared immutable layout preparation.
    ui::paint_engine& paint;          ///< Shared deterministic paint preparation.
    diagnostics::services sinks;      ///< Optional logging/profiling.
    std::vector<layer> layers;        ///< Rebuilt after each publication.
    std::set<std::uint64_t> input_sessions; ///< Bounded currently enabled view identities.
    std::string keyboard;       ///< Stable selected screen rather than a borrowed instance pointer.
    std::string front;          ///< New foreground presentations take initial keyboard ownership.
    ui::layout_rect viewport{}; ///< Dismiss gestures belong to this viewport, not editor chrome.
    std::map<std::string, std::string> bookmarks; ///< Focus IDs across exclusive focus changes.
    bool swallow_release = false; ///< Consume the entire outside-dismiss gesture across frames.
};
} // namespace atlas::ui_content
