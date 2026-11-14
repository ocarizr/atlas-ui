/** @file @brief Resolve named UI poses once and cross-fade them on the owner clock. */
#include "animation_values.hpp"
#include "instance_state.hpp"
#include <algorithm>
#include <cmath>

namespace atlas::ui_content
{
namespace
{
/// @brief Invoke an already resolved pose without a name lookup during model dispatch.
template <class Select> class state_action final : public mvvm::observer
{
  public:
    explicit state_action(Select select) : select_{std::move(select)}
    {
    } ///< Keep the selection for the lifetime of its model subscription.

    void on_notify(const mvvm::notification&, mvvm::view_model&) override
    {
        select_();
    } ///< Call the owner with a resolved group and pose.

  private:
    Select select_; ///< Borrowed instance state until explicit unsubscription.
};

/// @brief Resolve destination indices only when selecting or binding by name.
std::size_t pose_index(const std::vector<animation_pose>& poses, std::string_view name)
{
    const auto found = std::ranges::find(poses, name, &animation_pose::name);
    if (found == poses.end())
    {
        throw std::out_of_range("Unknown animation pose: " + std::string{name});
    }

    return static_cast<std::size_t>(found - poses.begin());
}

/// @brief Each pose covers the complete group so interruption is well defined.
void validate_poses(const std::vector<animation_channel>& channels,
                    const std::vector<animation_pose>& poses)
{
    std::set<std::string_view> names;
    for (const auto& pose : poses)
    {
        if (pose.name.empty() || pose.name.size() > 64 || pose.values.size() != channels.size() ||
            !names.insert(pose.name).second)
        {
            throw std::invalid_argument("Invalid animation pose name or channel count");
        }

        for (std::size_t i = 0; i < channels.size(); ++i)
        {
            if (!ui::valid_scalar(channels[i].field, pose.values[i]))
            {
                throw std::invalid_argument("Animation pose value outside the property range");
            }
        }
    }
}
} // namespace

/// @brief Register and reserve an exclusive group only after validation succeeds.
void instance::register_animation_states(std::string name, std::vector<animation_channel> channels,
                                         std::vector<animation_pose> poses,
                                         animation_transition transition)
{
    (void)impl_->tree.size();
    const diagnostics::operation_scope scope{impl_->diagnostics,
                                             {"ui.animation.register_states", "ui.cpu"}};
    if (name.empty() || name.size() > 64 || impl_->animation_states.size() >= 16 ||
        impl_->animation_states.contains(name) || impl_->animation || channels.empty() ||
        channels.size() > 64 || poses.size() < 2 || poses.size() > 32 ||
        transition.duration.count() <= 0 ||
        !detail::valid_curve(transition.curve, transition.controls))
    {
        throw std::invalid_argument("Invalid animation state group");
    }

    validate_poses(channels, poses);
    animation_clip validation{transition.duration, {}};
    validation.tracks.reserve(channels.size());
    for (std::size_t i = 0; i < channels.size(); ++i)
    {
        validation.tracks.push_back({std::move(channels[i].element),
                                     channels[i].field,
                                     {{animation_time::zero(), poses.front().values[i]}}});
    }

    auto resolved = impl_->resolve(*this, validation, animation_repeat::once);
    auto ownership = impl_->state_channels;
    for (auto& channel : resolved.tracks)
    {
        for (const auto& player : impl_->players)
        {
            if (std::ranges::any_of(player.clip.tracks,
                                    [&](const auto& track)
                                    {
                                        return track.target == channel.target &&
                                               track.property == channel.property;
                                    }))
            {
                throw std::invalid_argument("A running clip owns the animation state channel");
            }
        }

        ownership.emplace(channel.target.index, channel.property);
        channel.keys.clear();
    }

    impl::state_group group{std::move(resolved.tracks),
                            std::move(poses),
                            std::vector<float>(channels.size()),
                            transition,
                            {}};
    impl_->animation_states.emplace(std::move(name), std::move(group));
    impl_->state_channels.swap(ownership);
}

/// @brief Lookup happens once when the owner requests a state change.
void instance::set_animation_state(animation_state_target target)
{
    (void)impl_->tree.size();
    const auto match = impl_->animation_states.find(target.group);
    if (match == impl_->animation_states.end())
    {
        throw std::out_of_range("Unknown animation state group: " + std::string{target.group});
    }

    impl_->select_state(match->second, pose_index(match->second.poses, target.pose));
}

/// @brief Persist the resolved action-to-state connection until instance teardown.
void instance::bind_animation_state(animation_action_route route, animation_state_target target)
{
    (void)impl_->tree.size();
    const auto match = impl_->animation_states.find(target.group);
    if (match == impl_->animation_states.end())
    {
        throw std::out_of_range("Unknown animation state group: " + std::string{target.group});
    }

    const auto index = pose_index(match->second.poses, target.pose);
    auto& source = model(route.context);
    const auto slot = source.description().action(route.action);
    const auto select = [state = impl_.get(), group = &match->second, index]
    {
        state->select_state(*group, index);
    };
    impl_->animation_routes.reserve(impl_->animation_routes.size() + 1);
    const auto token =
        source.subscribe(slot, std::make_shared<state_action<decltype(select)>>(select));
    impl_->animation_routes.emplace_back(&source, token);
}

/// @brief Restart from the displayed pose and leave any pending selection intact on invalid input.
void instance::impl::select_state(state_group& group, std::size_t pose)
{
    if (group.selected == pose)
    {
        return;
    }

    std::array<float, 64> source{};
    for (std::size_t i = 0; i < group.channels.size(); ++i)
    {
        const auto& channel = group.channels[i];
        if (!tree.contains(channel.target))
        {
            continue;
        }

        const auto current = detail::read_animation_value(tree, channel.target, channel.property);
        const auto* scalar = std::get_if<float>(&current);
        if (!scalar)
        {
            throw std::invalid_argument("State transition requires an explicit source value");
        }

        source[i] = *scalar;
    }

    std::copy_n(source.begin(), group.channels.size(), group.source.begin());
    group.selected = pose;
    group.position = reduced_motion ? group.transition.duration : animation_time::zero();
    group.dirty = true;
}

/// @brief Time can advance while presentation is inactive without issuing UI commands.
void instance::impl::advance_states(animation_time elapsed)
{
    for (auto& [name, group] : animation_states)
    {
        if (group.selected)
        {
            const auto remaining = group.transition.duration - group.position;
            const auto delta = reduced_motion ? remaining : std::min(remaining, elapsed);
            group.position += delta;
            group.dirty |= delta != animation_time::zero();
        }
    }
}

/// @brief Sample dirty groups in stable declaration order without resolving target names.
void instance::impl::collect_state_writes()
{
    for (const auto& [name, group] : animation_states)
    {
        if (!group.dirty || !group.selected)
        {
            continue;
        }

        const auto& pose = group.poses[*group.selected];
        const auto fraction = static_cast<double>(group.position.count()) /
                              static_cast<double>(group.transition.duration.count());
        const auto weight =
            detail::ease(group.transition.curve, group.transition.controls, fraction);
        for (std::size_t i = 0; i < group.channels.size(); ++i)
        {
            const auto& channel = group.channels[i];
            if (tree.contains(channel.target))
            {
                const auto value = std::lerp(static_cast<double>(group.source[i]),
                                             static_cast<double>(pose.values[i]), weight);
                animation_writes.push_back({channel.target, channel.property,
                                            static_cast<float>(value), detail::animation_producer});
            }
        }
    }
}
} // namespace atlas::ui_content
