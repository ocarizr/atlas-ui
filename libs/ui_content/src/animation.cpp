/** @file @brief Owner-driven animation clock, curve evaluation and command publication. */
#include "animation_values.hpp"
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
    if (track.property == ui::property::visible ||
        track.property > ui::property::canvas_y_anchor_max)
    {
        throw std::invalid_argument("Animation requires a supported numeric UI property");
    }

    if (track.keys.empty() || track.keys.size() > max_keys_per_track)
    {
        throw std::invalid_argument("Animation track must have 1 to 1024 keys");
    }

    animation_time previous{-1};
    for (const auto& key : track.keys)
    {
        if (key.time <= previous || key.time > duration ||
            !ui::valid_scalar(track.property, key.value) || key.curve > animation_curve::cubic ||
            (key.curve == animation_curve::cubic &&
             std::ranges::any_of(key.controls,
                                 [](float value)
                                 {
                                     return !std::isfinite(value) || value < 0 || value > 1;
                                 })))
        {
            throw std::invalid_argument("Animation key time, value or curve is invalid");
        }

        previous = key.time;
    }
}
} // namespace

/// @brief Hold outside the keyed interval and evaluate the outgoing rule within a segment.
float detail::sample(const std::vector<animation_key>& keys, animation_time position)
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
    else if (left.curve == animation_curve::cubic)
    {
        const auto inverse = 1 - fraction;
        fraction = 3 * inverse * inverse * fraction * left.controls[0] +
                   3 * inverse * fraction * fraction * left.controls[1] +
                   fraction * fraction * fraction;
    }

    return static_cast<float>(
        std::lerp(static_cast<double>(left.value), static_cast<double>(next->value), fraction));
}

/// @brief Resolve a candidate without modifying existing players.
instance::impl::playback instance::impl::resolve(const instance& view, animation_clip& clip,
                                                 animation_repeat repeat)
{
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
        const auto target = view.find(track.element);
        const auto key = std::pair{target.index, track.property};
        if (properties.contains(key))
        {
            diagnostics::write(diagnostics, diagnostics::log_level::warning, "ui.animation",
                               "Animation track targets a property owned by an MVVM binding");
            throw std::invalid_argument(
                "Animation track targets a property owned by an MVVM binding");
        }

        validate_track(track, clip.duration);
        if (track.property == ui::property::spacing)
        {
            const auto panel = tree.panel(target);
            if (!std::holds_alternative<ui::horizontal_layout>(panel) &&
                !std::holds_alternative<ui::vertical_layout>(panel))
            {
                throw std::invalid_argument("Animation spacing track requires a stack");
            }
        }

        if (track.property >= ui::property::canvas_x_offset_min && !tree.canvas_slot_for(target))
        {
            throw std::invalid_argument("Animation canvas track requires a canvas parent");
        }

        if (!targets.insert(key).second)
        {
            throw std::invalid_argument("Duplicate animation track for an element/property");
        }

        candidate.tracks.push_back({target, track.property, std::move(track.keys)});
    }

    return candidate;
}

/// @brief Preserve the original single-player API for existing hosts.
void instance::play_animation(animation_clip clip, animation_repeat repeat)
{
    (void)impl_->tree.size();
    if (!impl_->players.empty())
    {
        throw std::logic_error("Finish concurrent clips before starting legacy playback");
    }

    impl_->animation = impl_->resolve(*this, clip, repeat);
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

    impl_->advance_players(elapsed);
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
    impl_->publish_players();
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
                commands.set(track.target, track.property,
                             detail::sample(track.keys, active->position),
                             detail::animation_producer);
            }
        }
    }

    active->dirty = false;
}
} // namespace atlas::ui_content
