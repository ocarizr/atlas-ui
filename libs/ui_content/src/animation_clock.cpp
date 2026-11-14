/** @file @brief Deterministic bounded clock traversal and owner notification delivery. */
#include "instance_state.hpp"
#include <algorithm>
#include <limits>

namespace atlas::ui_content
{
namespace
{

/// @brief Convert elapsed time without integer overflow; preserve fractional clock ticks.
animation_time scaled(animation_time elapsed, std::uint32_t rate, std::uint32_t& remainder)
{
    const auto whole = elapsed.count() / 100;
    const auto part = (elapsed.count() % 100) * rate + remainder;
    remainder = static_cast<std::uint32_t>(part % 100);
    const auto maximum = animation_time::max().count();
    if (whole > (maximum - part / 100) / rate)
    {
        throw std::overflow_error("Scaled animation time exceeds its representable range");
    }

    return animation_time{whole * rate + part / 100};
}

/// @brief A half-open time interval in traversal order.
struct crossing
{
    animation_time from; ///< Excluded previous position.
    animation_time to;   ///< Included destination.
};

/// @brief Emit half-open crossings with stable declaration order for equal times.
template <class Emit>
void cross(const std::vector<animation_marker>& markers, crossing interval, bool backwards,
           const Emit& emit)
{
    const auto [from, to] = interval;
    const auto lower = [&](animation_time position)
    {
        return std::ranges::lower_bound(markers, position, {}, &animation_marker::time);
    };
    const auto upper = [&](animation_time position)
    {
        return std::ranges::upper_bound(markers, position, {}, &animation_marker::time);
    };
    if (!backwards)
    {
        for (auto key = upper(from), end = upper(to); key != end; ++key)
        {
            emit(key->id);
        }

        return;
    }

    const auto begin = lower(to);
    for (auto end = lower(from); end != begin;)
    {
        const auto first = lower((end - 1)->time);
        for (auto key = first; key != end; ++key)
        {
            emit(key->id);
        }

        end = first;
    }
}
} // namespace

/// @brief A full queue reports failure before accepting another notification.
void instance::impl::queue_event(animation_event event)
{
    if (animation_events.size() == detail::max_animation_events)
    {
        throw std::overflow_error("UI animation event capacity exhausted");
    }

    animation_events.push_back(event);
}

/// @brief Advance one clock, bounding expensive traversal even for huge elapsed values.
void instance::impl::advance_player(player& active, animation_time elapsed)
{
    auto& options = active.options;
    auto& clip = active.clip;
    if (active.retired || active.paused || elapsed == animation_time::zero())
    {
        return;
    }

    if (reduced_motion)
    {
        if (options.traversals != 0)
        {
            const bool backwards =
                options.backwards !=
                (options.repeat == animation_repeat::ping_pong && options.traversals % 2 == 0);
            queue_event({active.handle, animation_event_kind::completed});
            clip.position = backwards ? options.start : active.end;
            active.started = active.retired = clip.dirty = true;
        }

        return;
    }

    if (!active.started)
    {
        const auto waiting = std::min(options.delay, elapsed);
        options.delay -= waiting;
        elapsed -= waiting;
        active.started = options.delay == animation_time::zero();
        clip.dirty |= active.started;
    }

    auto remaining = scaled(elapsed, options.rate_percent, active.remainder);
    const auto width = active.end - options.start;
    const auto initial_edge = options.backwards ? options.start : active.end;
    if (active.markers.empty() && remaining >= width && clip.position != initial_edge)
    {
        const auto full = remaining / width;
        const auto skipped =
            options.traversals == 0
                ? full
                : std::min(full, static_cast<animation_time::rep>(options.traversals - 1));
        remaining -= width * skipped;
        if (options.traversals != 0)
        {
            options.traversals -= static_cast<std::uint32_t>(skipped);
        }

        if (options.repeat == animation_repeat::ping_pong && skipped % 2 != 0)
        {
            options.backwards = !options.backwards;
            clip.position = options.start + (active.end - clip.position);
            clip.dirty = true;
        }
    }

    for (std::size_t crossings = 0; remaining > animation_time::zero(); ++crossings)
    {
        if (crossings == detail::max_animation_events)
        {
            throw std::overflow_error("UI animation traversal capacity exhausted");
        }

        const auto edge = options.backwards ? options.start : active.end;
        const auto distance = options.backwards ? clip.position - edge : edge - clip.position;
        const auto step = std::min(remaining, distance);
        const auto next = options.backwards ? clip.position - step : clip.position + step;
        cross(active.markers, {clip.position, next}, options.backwards,
              [&](auto marker)
              {
                  queue_event({active.handle, animation_event_kind::marker, marker});
              });
        clip.dirty = true;
        clip.position = next;
        remaining -= step;
        if (next != edge)
        {
            break;
        }

        if (options.traversals != 0)
        {
            --options.traversals;
            if (options.traversals == 0)
            {
                queue_event({active.handle, animation_event_kind::completed});
                active.retired = true;
                break;
            }
        }

        if (options.repeat == animation_repeat::ping_pong)
        {
            options.backwards = !options.backwards;
        }
        else
        {
            clip.position = options.backwards ? active.end : options.start;
        }
    }
}

/// @brief Roll clocks and queue back together if event traversal exhausts its budget.
void instance::impl::advance_players(animation_time elapsed)
{
    /// @brief Preserve only clock state, never copy track or marker storage.
    struct clock_state
    {
        animation_options options; ///< Includes direction, delay and remaining trips.
        animation_time position;   ///< Previous local time.
        std::uint32_t remainder;   ///< Previous fractional time.
        bool dirty;                ///< Previous publication state.
        bool started;              ///< Previous delay status.
        bool retired;              ///< Previous terminal status.
    };

    if (players.empty())
    {
        return;
    }

    const diagnostics::operation_scope scope{diagnostics, {"ui.animation.advance", "ui.cpu"}};
    std::array<clock_state, detail::max_animation_players> previous{};
    const auto event_count = animation_events.size();
    std::size_t attempted = 0;
    try
    {
        for (auto& active : players)
        {
            previous.at(attempted++) = {active.options,    active.clip.position, active.remainder,
                                        active.clip.dirty, active.started,       active.retired};
            advance_player(active, elapsed);
        }
    }
    catch (...)
    {
        for (std::size_t index = 0; index < attempted; ++index)
        {
            auto& active = players[index];
            const auto& old = previous[index];
            active.options = old.options;
            active.clip.position = old.position;
            active.remainder = old.remainder;
            active.clip.dirty = old.dirty;
            active.started = old.started;
            active.retired = old.retired;
        }

        animation_events.resize(event_count);
        throw;
    }
}
} // namespace atlas::ui_content
