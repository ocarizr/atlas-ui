/** @file @brief Lifetime order and per-context subscriptions for the authored UI owner. */
#pragma once
#include "model_sources.hpp"
#include "tree_build.hpp"

namespace atlas::ui_content
{
namespace detail
{
inline constexpr std::uint64_t animation_producer = 2;    ///< Reserved before model producer IDs.
inline constexpr std::size_t max_animation_players = 16;  ///< Maximum concurrent animation layers.
inline constexpr std::size_t max_animation_events = 4096; ///< Maximum undrained notifications.

/// @brief Resolved contribution from one layer to one animated property.
struct animation_contribution
{
    ui::element_handle target; ///< Exact live generation.
    ui::property field;        ///< Numeric channel.
    ui::property_value base;   ///< Value underlying the stack.
    double value;              ///< Absolute sample or additive delta.
    float weight;              ///< Mix coefficient.
    std::size_t order;         ///< Stable position in the sorted layers.
    bool additive;             ///< Sample is relative to first key.
    bool release;              ///< Omit the retired layer's sample.
    bool hold; ///< A cancelled exclusive layer leaves its previously published pose.
};
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
        ui::property_value base{};       ///< Captured value underneath the concurrent layers.
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

    /// @brief Timing and lifetime state for a typed concurrent player.
    struct player
    {
        animation_handle handle;               ///< Stable identity and tie-breaker.
        playback clip;                         ///< Resolved tracks and current time.
        animation_options options;             ///< Composition and traversal policy.
        animation_time end;                    ///< Resolved upper endpoint.
        std::vector<animation_marker> markers; ///< Ordered lookups by clip time.
        std::uint32_t remainder = 0;           ///< Subnanosecond rate remainder in hundredths.
        bool paused = false;                   ///< User pause.
        bool retired = false;                  ///< Terminal or cancelled layer awaits publication.
        bool cancelled = false; ///< Hold last applied pose on cancellation unless restoring.
        bool started = false;   ///< A delayed layer has reached its first sample.
    };

    playback resolve(const instance& view, animation_clip& clip,
                     animation_repeat repeat);       ///< Validate the complete target set.
    player& require_player(animation_handle handle); ///< Reject foreign and retired handles.
    void advance_players(animation_time elapsed);    ///< Advance concurrent clocks on the owner.
    void advance_player(player& active, animation_time elapsed); ///< Advance one player.
    void queue_event(animation_event event); ///< Bounded owner notification queue.
    void publish_players();                  ///< Compose complete samples before command admission.

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
    std::vector<player> players;       ///< Bounded live clips in creation order.
    std::map<std::string, animation_clip, std::less<>> named_animations; ///< Reusable definitions.
    std::vector<animation_event> animation_events; ///< Pending owner notifications.
    std::vector<std::pair<mvvm::view_model*, mvvm::subscription>>
        animation_routes;                                         ///< Model registrations.
    std::vector<detail::animation_contribution> animation_layers; ///< Reused sampled scratch.
    std::vector<ui::property_write> animation_writes; ///< Reused validated command scratch.
    std::uint64_t animation_sequence = 0;             ///< Monotonic source of player identities.
    bool reduced_motion = false;                      ///< Host policy for concurrent playback.

  private:
    void bind_elements(const document& source, const construction& options); ///< Wire each ID.
    void bind_properties(const element& node, ui::element_handle target); ///< Resolve field names.
};
} // namespace atlas::ui_content
