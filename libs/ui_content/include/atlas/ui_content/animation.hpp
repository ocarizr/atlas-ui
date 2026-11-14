/**
 * @file
 * @brief Value definitions for viewport UI animation playback.
 */
#pragma once
#include <atlas/ui/commands.hpp>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace atlas::ui_content
{
using animation_time = std::chrono::nanoseconds; ///< Integral elapsed time supplied by the host.

/// @brief Interpolation from a key to the following key in the same track.
enum class animation_curve : std::uint8_t
{
    step,       ///< Hold until the following key.
    linear,     ///< Interpolate by normalized time.
    smoothstep, ///< Apply cubic easing with zero slope at each endpoint.
    cubic       ///< Bézier easing with fixed horizontal controls.
};

/// @brief An absolute scalar sample with outgoing interpolation.
struct animation_key
{
    animation_time time;                             ///< Time relative to the clip start.
    float value;                                     ///< Value in the target property's units.
    animation_curve curve = animation_curve::linear; ///< Segment rule after this key.
    std::array<float, 2> controls{0.33333334f, 0.66666669f}; ///< Normalized cubic value controls.
};

/// @brief One absolute scalar property addressed by a document-local ID.
struct animation_track
{
    std::string element;             ///< Authored target ID.
    ui::property property;           ///< Supported numeric command property.
    std::vector<animation_key> keys; ///< Strictly ordered nonnegative key times.
};

/// @brief A typed notification key relative to the clip start.
struct animation_marker
{
    animation_time time; ///< Crossed during playback, silent during seek.
    std::uint32_t id;    ///< Nonzero application-defined marker identity.
};

/// @brief A host-supplied clip resolved to runtime handles when started.
struct animation_clip
{
    animation_time duration;                 ///< Strictly positive playback duration.
    std::vector<animation_track> tracks;     ///< One track per element/property.
    std::vector<animation_marker> markers{}; ///< Sorted by time, ties preserve declaration order.
};

/// @brief Treatment of the terminal clip time.
enum class animation_repeat : std::uint8_t
{
    once,     ///< Clamp to the terminal sample and signal completion.
    loop,     ///< Wrap to the starting endpoint.
    ping_pong ///< Alternate traversal directions.
};

/// @brief Instance-local player identity, never reused by that instance.
struct animation_handle
{
    std::uint64_t value = 0; ///< Monotonic player sequence; zero is invalid.
    std::uint64_t owner = 0; ///< Tree session identity.
    bool operator==(const animation_handle&) const = default; ///< Compare full identity.
};

/// @brief Choose timing and deterministic layer composition.
struct animation_options
{
    animation_time delay{};              ///< Owner time before the first sample.
    animation_time start{};              ///< Inclusive lower endpoint in clip time.
    std::optional<animation_time> end{}; ///< Upper endpoint, defaulting to clip duration.
    animation_repeat repeat = animation_repeat::once; ///< End behavior.
    std::uint32_t traversals = 0;     ///< Zero uses one for once and unlimited otherwise.
    std::uint32_t rate_percent = 100; ///< Positive playback speed.
    std::int32_t priority = 0;        ///< Higher layers compose last.
    float weight = 1;                 ///< Blend from lower layers to the evaluated sample.
    bool backwards = false;           ///< Start at the upper endpoint.
    bool restore = false;  ///< Release to the captured base on completion or cancellation.
    bool additive = false; ///< Add the offset from the first key on compatible channels.
};

/// @brief One reusable clip with its own options within a timed group.
struct animation_group_entry
{
    const animation_clip& clip;  ///< Borrowed during group admission.
    animation_options options{}; ///< In parallel groups the delay can stagger entry.
};

/// @brief Instance-local ViewModel action used as a declarative playback trigger.
struct animation_action_route
{
    std::string_view context; ///< Authored model context name.
    std::string_view action;  ///< Action defined by that model.
};

/// @brief Interpret delays from the group origin or the previous entry's end.
enum class animation_group_order : std::uint8_t
{
    parallel,  ///< All entries use the same origin.
    sequential ///< Each entry follows the previous one.
};

/// @brief Owner notifications delivered by draining the bounded event queue.
enum class animation_event_kind : std::uint8_t
{
    marker,    ///< Crossed a named point on the timeline.
    completed, ///< Reached the final endpoint.
    cancelled  ///< Explicitly retired before its terminal sample.
};

/// @brief Stable notification ordered by player creation and then traversal.
struct animation_event
{
    animation_handle player;   ///< Source playback identity.
    animation_event_kind kind; ///< Marker or lifecycle edge.
    std::uint32_t marker = 0;  ///< Nonzero only for markers.
};
} // namespace atlas::ui_content
