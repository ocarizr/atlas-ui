/** @file @brief Lifetime order and per-context subscriptions for the authored UI owner. */
#pragma once
#include "model_sources.hpp"
#include "tree_build.hpp"

namespace atlas::ui_content
{
namespace detail
{
inline constexpr std::uint64_t animation_producer = 2; ///< Reserved before model producer IDs.
} // namespace detail

/// @brief The input adapter dies before the domain and subscriptions before models/tree.
class instance::impl
{
  public:
    impl(const document& source, const construction& options); ///< Construct a private candidate.

    /// @brief Compile and retain one reverse-write token for the authored target property.
    struct bound_property
    {
        ui_mvvm::binding_session* session; ///< Borrowed from this instance's owned sessions.
        ui_mvvm::binding_handle token;     ///< Owner-local binding identity.
    };

    /// @brief Playback references the original generation even if a tree slot is later reused.
    struct resolved_track
    {
        ui::element_handle target;       ///< Exact tree identity.
        ui::property property;           ///< Previously validated scalar property.
        std::vector<animation_key> keys; ///< Owned, validated curve.
    };

    /// @brief A single owner clock composes one active clip per instance.
    struct playback
    {
        animation_time duration;            ///< Positive upper bound.
        animation_repeat repeat;            ///< Once or loop.
        std::vector<resolved_track> tracks; ///< Stable authored order.
        animation_time position{};          ///< Normalized elapsed time.
        bool dirty = true;                  ///< Current sample awaits admission.
        bool completed = false;             ///< Completion edge awaits consumption.
    };

    ui::element_tree tree;                ///< First constructed and last released.
    detail::elements elements;            ///< Authored IDs resolved into this tree only.
    detail::model_map models;             ///< Named independent instances.
    std::vector<std::string> model_order; ///< Authored model ordering for frame/flush phases.
    std::map<std::string, std::unique_ptr<ui_mvvm::binding_session>, std::less<>>
        bindings; ///< Observers.
    std::map<std::pair<std::uint32_t, ui::property>, bound_property>
        properties;                                                 ///< Reverse writes.
    std::map<std::string, assets::asset_ref, std::less<>> textures; ///< Persistent texture refs.
    std::shared_ptr<scripting::domain> domain; ///< Shared owner interpreter, created on demand.
    ui::input_session input;                   ///< Releases all retained element handlers first.
    diagnostics::services diagnostics;         ///< Optional composition and frame instrumentation.
    std::optional<playback> animation; ///< Clock and resolved tracks owned by this instance.

  private:
    void bind_elements(const document& source, const construction& options); ///< Wire each ID.
    void bind_properties(const element& node, ui::element_handle target); ///< Resolve field names.
};
} // namespace atlas::ui_content
