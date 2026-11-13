/**
 * @file
 * @brief Value definitions for viewport UI animation playback.
 */
#pragma once
#include <atlas/ui/commands.hpp>
#include <chrono>
#include <string>
#include <vector>

namespace atlas::ui_content
{
using animation_time = std::chrono::nanoseconds; ///< Integral elapsed time supplied by the host.

/// @brief Interpolation from a key to the following key in the same track.
enum class animation_curve : std::uint8_t
{
    step,      ///< Hold until the following key.
    linear,    ///< Interpolate by normalized time.
    smoothstep ///< Apply cubic easing with zero slope at each endpoint.
};

/// @brief An absolute scalar sample with outgoing interpolation.
struct animation_key
{
    animation_time time;                             ///< Time relative to the clip start.
    float value;                                     ///< Width, height or normalized opacity.
    animation_curve curve = animation_curve::linear; ///< Segment rule after this key.
};

/// @brief One absolute scalar property addressed by a document-local ID.
struct animation_track
{
    std::string element;             ///< Authored target ID.
    ui::property property;           ///< Width, height or opacity.
    std::vector<animation_key> keys; ///< Strictly ordered nonnegative key times.
};

/// @brief A host-supplied clip resolved to runtime handles when started.
struct animation_clip
{
    animation_time duration;             ///< Strictly positive playback duration.
    std::vector<animation_track> tracks; ///< One track per element/property.
};

/// @brief Treatment of the terminal clip time.
enum class animation_repeat : std::uint8_t
{
    once, ///< Clamp to the terminal sample and signal completion.
    loop  ///< Wrap into the half-open interval [0, duration).
};
} // namespace atlas::ui_content
