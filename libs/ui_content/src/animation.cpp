/** @file @brief Owner-driven animation clock, curve evaluation and command publication. */
#include "instance_state.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace atlas::ui_content
{
namespace
{
constexpr std::size_t max_tracks = 256;          ///< Upper bound on writes per animation sample.
constexpr std::size_t max_keys_per_track = 1024; ///< Upper bound on each curve's storage.

/// @brief Reject incomplete, unordered or unsafe data before playback can reach the command queue.
void validate_track(const animation_track& track, animation_time duration)
{
    switch (track.property)
    {
        case ui::property::width:
        case ui::property::height:
        case ui::property::opacity:
            break;
        default:
            throw std::invalid_argument("Animation requires width, height or opacity");
    }

    if (track.keys.empty() || track.keys.size() > max_keys_per_track)
    {
        throw std::invalid_argument("Animation track must have 1 to 1024 keys");
    }

    animation_time previous{-1};
    for (const auto& key : track.keys)
    {
        if (key.time <= previous || key.time > duration || !std::isfinite(key.value) ||
            key.value < 0 || (track.property == ui::property::opacity && key.value > 1) ||
            key.curve > animation_curve::smoothstep)
        {
            throw std::invalid_argument("Animation key time, value or curve is invalid");
        }

        previous = key.time;
    }
}

/// @brief Hold outside the keyed interval and evaluate the outgoing rule within a segment.
float sample(const std::vector<animation_key>& keys, animation_time position)
{
    const auto next = std::ranges::upper_bound(keys, position, {}, &animation_key::time);
    if (next == keys.begin())
    {
        return next->value;
    }

    const auto& left = *(next - 1);
    if (next == keys.end() || left.curve == animation_curve::step)
    {
        return left.value;
    }

    auto fraction = static_cast<double>((position - left.time).count()) /
                    static_cast<double>((next->time - left.time).count());
    if (left.curve == animation_curve::smoothstep)
    {
        fraction = fraction * fraction * (3 - 2 * fraction);
    }

    return static_cast<float>(
        std::lerp(static_cast<double>(left.value), static_cast<double>(next->value), fraction));
}
} // namespace

/// @brief Commit a candidate only after every track resolves and passes ownership checks.
void instance::play_animation(animation_clip clip, animation_repeat repeat)
{
    (void)impl_->tree.size();
    if (clip.duration <= animation_time::zero() || clip.tracks.empty() ||
        clip.tracks.size() > max_tracks || repeat > animation_repeat::loop)
    {
        throw std::invalid_argument(
            "Animation requires a positive duration, 1 to 256 tracks and a valid repeat mode");
    }

    impl::playback candidate{clip.duration, repeat, {}};
    candidate.tracks.reserve(clip.tracks.size());
    std::set<std::pair<std::uint32_t, ui::property>> targets;
    for (auto& track : clip.tracks)
    {
        const auto target = find(track.element);
        const auto key = std::pair{target.index, track.property};
        if (impl_->properties.contains(key))
        {
            diagnostics::write(impl_->diagnostics, diagnostics::log_level::warning, "ui.animation",
                               "Animation track targets a property owned by an MVVM binding");
            throw std::invalid_argument(
                "Animation track targets a property owned by an MVVM binding");
        }

        validate_track(track, clip.duration);
        if (!targets.insert(key).second)
        {
            throw std::invalid_argument("Duplicate animation track for an element/property");
        }

        candidate.tracks.push_back({target, track.property, std::move(track.keys)});
    }

    impl_->animation = std::move(candidate);
}

/// @brief Cancel timing and any undelivered completion while holding already applied values.
void instance::stop_animation()
{
    (void)impl_->tree.size();
    impl_->animation.reset();
}

/// @brief Normalize in integer time and avoid overflow even for the largest elapsed value.
void instance::advance_animation(animation_time elapsed)
{
    (void)impl_->tree.size();
    if (elapsed < animation_time::zero())
    {
        throw std::invalid_argument("Animation elapsed time must be nonnegative");
    }

    auto& active = impl_->animation;
    if (!active || elapsed == animation_time::zero())
    {
        return;
    }

    const auto remaining = active->duration - active->position;
    auto next = active->position;
    if (active->repeat == animation_repeat::loop)
    {
        const auto offset = elapsed % active->duration;
        next = offset >= remaining ? offset - remaining : active->position + offset;
    }
    else if (remaining > animation_time::zero())
    {
        next += std::min(remaining, elapsed);
        active->completed |= next == active->duration;
    }

    active->dirty |= next != active->position;
    active->position = next;
}

/// @brief Explicit seeking selects a pose and clears previous completion without firing events.
void instance::seek_animation(animation_time position)
{
    (void)impl_->tree.size();
    if (position < animation_time::zero())
    {
        throw std::invalid_argument("Animation seek time must be nonnegative");
    }

    if (auto& active = impl_->animation)
    {
        active->position = active->repeat == animation_repeat::loop
                               ? position % active->duration
                               : std::min(position, active->duration);
        active->dirty = true;
        active->completed = false;
    }
}

/// @brief Distinguish a stopped player from an active clip at time zero.
std::optional<animation_time> instance::animation_position() const
{
    (void)impl_->tree.size();
    if (const auto& active = impl_->animation)
    {
        return active->position;
    }

    return std::nullopt;
}

/// @brief Return the natural end transition once to the owner.
bool instance::take_animation_completion()
{
    (void)impl_->tree.size();
    if (auto& active = impl_->animation)
    {
        return std::exchange(active->completed, false);
    }

    return false;
}

/// @brief Admit the entire live sample in deterministic track order after a capacity preflight.
void instance::publish_animation()
{
    (void)impl_->tree.size();
    auto& active = impl_->animation;
    if (!active || !active->dirty)
    {
        return;
    }

    const diagnostics::operation_scope scope{impl_->diagnostics, {"ui.animation.sample", "ui.cpu"}};
    const auto live = [&](const impl::resolved_track& track)
    {
        return impl_->tree.contains(track.target);
    };
    const auto writes = std::ranges::count_if(active->tracks, live);
    auto& commands = impl_->tree.commands();
    if (writes > 0)
    {
        commands.require_capacity(static_cast<std::size_t>(writes));
        for (const auto& track : active->tracks)
        {
            if (live(track))
            {
                commands.set(track.target, track.property, sample(track.keys, active->position),
                             detail::animation_producer);
            }
        }
    }

    active->dirty = false;
}
} // namespace atlas::ui_content
