/** @file @brief Compose sampled layers and admit one write per live property. */
#include "animation_values.hpp"
#include "instance_state.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace atlas::ui_content
{
namespace
{
/// @brief Group writes by stable target identity while preserving layer order within each key.
auto key(const detail::animation_contribution& layer)
{
    return std::tuple{layer.target.index, layer.target.generation, layer.field};
}

/// @brief Compose on a double accumulator and validate before any command is queued.
ui::property_value compose(std::span<const detail::animation_contribution> layers,
                           const ui::element_tree& tree)
{
    const auto& first = layers.front();
    std::optional<double> result;
    if (const auto* value = std::get_if<float>(&first.base))
    {
        result = *value;
    }

    bool painted = false;
    bool hold = false;
    for (const auto& layer : layers)
    {
        hold |= layer.hold;
        if (layer.release || layer.weight == 0)
        {
            continue;
        }

        painted = true;
        if (layer.additive)
        {
            result = result.value_or(0) + layer.value * layer.weight;
        }
        else
        {
            result = std::lerp(result.value_or(layer.value), layer.value, layer.weight);
        }
    }

    if (!painted && hold)
    {
        return detail::read_animation_value(tree, first.target, first.field);
    }

    if (!result)
    {
        return first.base;
    }

    if (std::abs(*result) > std::numeric_limits<float>::max() ||
        !ui::valid_scalar(first.field, static_cast<float>(*result)))
    {
        throw std::overflow_error("Composed animation value exceeds the property range");
    }

    return static_cast<float>(*result);
}
} // namespace

/// @brief A dirty sample includes unchanged layers for overlap resolution and retires only on
/// success.
void instance::impl::publish_players()
{
    const auto dirty = [](const player& active)
    {
        return (active.started || active.retired) && active.clip.dirty;
    };
    if (std::ranges::none_of(players, dirty) && std::ranges::none_of(animation_states,
                                                                     [](const auto& entry)
                                                                     {
                                                                         return entry.second.dirty;
                                                                     }))
    {
        return;
    }

    const diagnostics::operation_scope scope{diagnostics, {"ui.animation.compose", "ui.cpu"}};
    std::array<const player*, detail::max_animation_players> ordered{};
    std::size_t count = 0;
    for (const auto& active : players)
    {
        if (active.started || active.retired)
        {
            ordered.at(count++) = &active;
        }
    }

    auto visible = std::span{ordered}.first(count);
    std::ranges::sort(visible, {},
                      [](const player* active)
                      {
                          return std::pair{active->options.priority, active->handle.value};
                      });
    auto& contributions = animation_layers;
    contributions.clear();
    for (const auto* active : visible)
    {
        const auto& options = active->options;
        for (const auto& track : active->clip.tracks)
        {
            if (!tree.contains(track.target) || (active->cancelled && !active->started))
            {
                continue;
            }

            const auto value =
                static_cast<double>(detail::sample(track.keys, active->clip.position));
            contributions.push_back({track.target, track.property, track.base,
                                     options.additive ? value - track.keys.front().value : value,
                                     options.weight, contributions.size(), options.additive,
                                     active->retired && (options.restore || active->cancelled),
                                     active->cancelled && !options.restore});
        }
    }

    std::ranges::sort(contributions,
                      [](const auto& first, const auto& second)
                      {
                          return std::pair{key(first), first.order} <
                                 std::pair{key(second), second.order};
                      });
    auto& composed_writes = animation_writes;
    composed_writes.clear();
    composed_writes.reserve(contributions.size());
    for (std::size_t begin = 0; begin < contributions.size();)
    {
        auto end = begin + 1;
        while (end < contributions.size() && key(contributions[end]) == key(contributions[begin]))
        {
            ++end;
        }

        const auto& first = contributions[begin];
        const auto group = std::span{contributions}.subspan(begin, end - begin);
        const auto value = compose(group, tree);
        composed_writes.push_back({first.target, first.field, value, detail::animation_producer});

        begin = end;
    }

    collect_state_writes();
    admit_animation_writes();

    std::erase_if(players,
                  [](const player& active)
                  {
                      return active.retired;
                  });
    for (auto& active : players)
    {
        active.clip.dirty = false;
    }

    for (auto& entry : animation_states)
    {
        entry.second.dirty = false;
    }
}

/// @brief No write can reach the buffer before the full sample fits in the frame.
void instance::impl::admit_animation_writes()
{
    if (animation_writes.empty())
    {
        return;
    }

    auto& commands = tree.commands();
    commands.require_capacity(animation_writes.size());
    for (const auto& write : animation_writes)
    {
        commands.set(write.target, write.field, write.value, write.producer);
    }
}
} // namespace atlas::ui_content
