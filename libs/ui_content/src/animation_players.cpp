/** @file @brief Concurrent player admission, handles, and retargeting. */
#include "animation_values.hpp"
#include "instance_state.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace atlas::ui_content
{
namespace
{
/// @brief Reuse one source definition for repeated MVVM action notifications.
class animation_action final : public mvvm::observer
{
  public:
    animation_action(instance& view, animation_clip clip, animation_options options)
        : view_{view}, clip_{std::move(clip)}, options_{options}
    {
    } ///< The instance retires the model route before destroying playback state.

    void on_notify(const mvvm::notification&, mvvm::view_model&) override
    {
        (void)view_.start_animation(clip_, options_);
    } ///< Invoke on the model owner during its normal synchronization phase.

  private:
    instance& view_;            ///< Borrowed for the subscription lifetime.
    animation_clip clip_;       ///< Owned reusable source.
    animation_options options_; ///< Copy each invocation's clock and layer policy.
};

/// @brief Only unconstrained signed channels support additive offsets.
bool supports_addition(ui::property field)
{
    return field == ui::property::translate_x || field == ui::property::translate_y ||
           field == ui::property::rotate;
}

/// @brief Check the bounded timing and marker contract before allocating playback state.
animation_time validate_options(const animation_clip& clip, const animation_options& options)
{
    const auto end = options.end.value_or(clip.duration);
    if (options.delay.count() < 0 || options.start.count() < 0 || end > clip.duration ||
        end <= options.start || options.rate_percent == 0 || options.rate_percent > 400 ||
        options.repeat > animation_repeat::ping_pong || !std::isfinite(options.weight) ||
        options.weight < 0 || options.weight > 1 || clip.markers.size() > 128)
    {
        throw std::invalid_argument("Invalid animation playback options");
    }

    animation_time previous{};
    for (const auto& marker : clip.markers)
    {
        if (marker.id == 0 || marker.time < previous || marker.time > clip.duration)
        {
            throw std::invalid_argument("Invalid or unordered animation marker");
        }

        previous = marker.time;
    }

    return end;
}
} // namespace

/// @brief Keep reusable definitions independent from their live playback clocks.
void instance::register_animation(std::string name, animation_clip clip)
{
    (void)impl_->tree.size();
    if (name.empty() || name.size() > 64 || impl_->named_animations.size() == 32 ||
        impl_->named_animations.contains(name))
    {
        throw std::invalid_argument("Invalid, duplicate or excessive named animation");
    }

    (void)validate_options(clip, {});
    auto candidate = clip;
    (void)impl_->resolve(*this, candidate, animation_repeat::once);
    impl_->named_animations.emplace(std::move(name), std::move(clip));
}

/// @brief A named definition can drive multiple independent players.
animation_handle instance::start_named_animation(std::string_view name, animation_options options)
{
    (void)impl_->tree.size();
    const auto match = impl_->named_animations.find(name);
    if (match == impl_->named_animations.end())
    {
        throw std::out_of_range("Unknown named animation");
    }

    return start_animation(match->second, options);
}

/// @brief Semantic input and Lua can both invoke the same typed model action.
void instance::bind_animation_action(animation_action_route route, animation_clip clip,
                                     animation_options options)
{
    (void)validate_options(clip, options);
    auto& source = model(route.context);
    const auto slot = source.description().action(route.action);
    auto validate = clip;
    (void)impl_->resolve(*this, validate, animation_repeat::once);
    impl_->animation_routes.reserve(impl_->animation_routes.size() + 1);
    const auto subscription =
        source.subscribe(slot, std::make_shared<animation_action>(*this, std::move(clip), options));
    impl_->animation_routes.emplace_back(&source, subscription);
}

/// @brief Share the base of an already animated channel and diagnose equal-priority aliases.
animation_handle instance::start_animation(animation_clip clip, animation_options options)
{
    (void)impl_->tree.size();
    if (impl_->animation)
    {
        throw std::logic_error("Stop legacy playback before starting concurrent clips");
    }

    if (impl_->players.size() == detail::max_animation_players ||
        impl_->animation_sequence == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("UI animation player capacity exhausted");
    }

    const auto end = validate_options(clip, options);
    auto candidate = impl_->resolve(*this, clip, animation_repeat::once);
    if (options.repeat == animation_repeat::once)
    {
        options.traversals = 1;
    }

    candidate.position = options.backwards ? end : options.start;
    for (auto& track : candidate.tracks)
    {
        track.base = detail::read_animation_value(impl_->tree, track.target, track.property);
        if (options.additive && !supports_addition(track.property))
        {
            throw std::invalid_argument("Additive playback requires a signed visual channel");
        }

        for (const auto& active : impl_->players)
        {
            for (const auto& other : active.clip.tracks)
            {
                if (other.target == track.target && other.property == track.property)
                {
                    track.base = other.base;
                    if (options.priority == active.options.priority)
                    {
                        diagnostics::write(impl_->diagnostics, diagnostics::log_level::warning,
                                           "ui.animation",
                                           "Equal priority animation writers use creation order");
                    }
                }
            }
        }
    }

    const animation_handle handle{impl_->animation_sequence + 1,
                                  candidate.tracks.front().target.session};
    const bool started = options.delay == animation_time::zero();
    impl_->players.push_back({handle, std::move(candidate), options, end, std::move(clip.markers),
                              0, false, false, false, started});
    ++impl_->animation_sequence;
    return handle;
}

/// @brief Schedule a group without interleaving other admissions or retaining a failed prefix.
std::vector<animation_handle>
instance::start_animation_group(std::span<const animation_group_entry> group,
                                animation_group_order order)
{
    (void)impl_->tree.size();
    if (group.empty() || group.size() > detail::max_animation_players - impl_->players.size() ||
        order > animation_group_order::sequential)
    {
        throw std::invalid_argument("Invalid animation group size or order");
    }

    const auto previous = impl_->players.size();
    const auto sequence = impl_->animation_sequence;
    std::vector<animation_handle> handles;
    handles.reserve(group.size());
    animation_time cursor{};
    try
    {
        for (const auto& entry : group)
        {
            auto options = entry.options;
            if (order == animation_group_order::sequential)
            {
                const auto span = validate_options(entry.clip, options) - options.start;
                const auto available = animation_time::max() - cursor;
                if (options.repeat != animation_repeat::once || options.rate_percent != 100 ||
                    options.delay > available || span > available - options.delay)
                {
                    throw std::invalid_argument(
                        "Sequential group requires bounded normal-rate clips");
                }

                options.delay += cursor;
                cursor = options.delay + span;
            }

            handles.push_back(start_animation(entry.clip, options));
        }
    }
    catch (...)
    {
        impl_->players.erase(impl_->players.begin() + static_cast<std::ptrdiff_t>(previous),
                             impl_->players.end());
        impl_->animation_sequence = sequence;
        throw;
    }

    return handles;
}

/// @brief Retarget a single property using the currently published pose as the first key.
animation_handle instance::tween_animation(std::string_view element, ui::property field,
                                           float value, animation_time duration)
{
    const auto target = find(element);
    const auto current = detail::read_animation_value(impl_->tree, target, field);
    if (!std::holds_alternative<float>(current))
    {
        throw std::invalid_argument("Tween requires an explicit numeric starting value");
    }

    const auto overlaps = [&](const impl::player& active)
    {
        return std::ranges::any_of(active.clip.tracks,
                                   [&](const auto& track)
                                   {
                                       return track.target == target && track.property == field;
                                   });
    };
    for (const auto& active : impl_->players)
    {
        if (overlaps(active) && active.clip.tracks.size() != 1)
        {
            throw std::invalid_argument("Tween cannot replace part of a multi-track clip");
        }
    }

    const auto handle = start_animation(
        {duration,
         {{std::string{element},
           field,
           {{animation_time::zero(), std::get<float>(current)}, {duration, value}}}}});
    std::erase_if(impl_->players,
                  [&](const auto& active)
                  {
                      return active.handle != handle && overlaps(active);
                  });
    return handle;
}

/// @brief A stale or foreign identity cannot affect another clip.
instance::impl::player& instance::impl::require_player(animation_handle handle)
{
    (void)tree.size();
    const auto found = std::ranges::find(players, handle, &player::handle);
    if (found == players.end() || found->retired)
    {
        throw std::invalid_argument("Unknown or retired animation handle");
    }

    return *found;
}

/// @brief Paused clips retain their current contribution.
void instance::pause_animation(animation_handle handle, bool paused)
{
    impl_->require_player(handle).paused = paused;
}

/// @brief Only the clock direction changes.
void instance::reverse_animation(animation_handle handle)
{
    auto& active = impl_->require_player(handle);
    active.options.backwards = !active.options.backwards;
}

/// @brief Scrub the pose without marker or lifecycle delivery.
void instance::seek_animation(animation_handle handle, animation_time position)
{
    auto& active = impl_->require_player(handle);
    if (position < animation_time::zero())
    {
        throw std::invalid_argument("Animation seek time must be nonnegative");
    }

    active.clip.position = std::clamp(position, active.options.start, active.end);
    active.clip.dirty = true;
    active.remainder = 0;
}

/// @brief Cancel the active layer and let the next publication apply its end policy.
void instance::cancel_animation(animation_handle handle)
{
    auto& active = impl_->require_player(handle);
    impl_->queue_event({handle, animation_event_kind::cancelled});
    active.cancelled = true;
    active.retired = true;
    active.clip.dirty = true;
}

/// @brief Change blend weight without changing the clock.
void instance::set_animation_weight(animation_handle handle, float weight)
{
    auto& active = impl_->require_player(handle);
    if (!std::isfinite(weight) || weight < 0 || weight > 1)
    {
        throw std::invalid_argument("Animation weight must be in [0, 1]");
    }

    active.clip.dirty |= active.options.weight != weight;
    active.options.weight = weight;
}

/// @brief The host controls reduced motion independently of application time.
void instance::set_reduced_motion(bool enabled)
{
    (void)impl_->tree.size();
    impl_->reduced_motion = enabled;
}

/// @brief Drain the queue without executing application callbacks.
std::vector<animation_event> instance::take_animation_events()
{
    (void)impl_->tree.size();
    std::vector<animation_event> events;
    events.swap(impl_->animation_events);
    return events;
}
} // namespace atlas::ui_content
