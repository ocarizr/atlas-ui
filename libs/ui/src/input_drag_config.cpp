/** @file @brief Sparse boundary-only typed source and destination admission. */
#include "input_state.hpp"
#include <cmath>

namespace atlas::ui
{
namespace
{
/// @brief Keep sparse metadata bounded and prune removed generations at each boundary.
template <class T>
void set_metadata(std::pmr::vector<std::pair<element_handle, T>>& values, element_handle target,
                  T options, const element_tree& tree, std::size_t limit)
{
    std::erase_if(values,
                  [&](const auto& value)
                  {
                      return !tree.contains(value.first);
                  });
    const auto found = std::find_if(values.begin(), values.end(),
                                    [&](const auto& value)
                                    {
                                        return value.first == target;
                                    });
    if (found != values.end())
    {
        found->second = std::move(options);
    }
    else
    {
        if (values.size() >= limit)
        {
            throw std::length_error("drag configuration limit exhausted");
        }

        values.emplace_back(target, std::move(options));
    }
}
} // namespace

/// @brief A source must be a live bound element; dynamic offers are prepared on lift.
void input_session::configure_drag_source(element_handle target, drag_source_options options)
{
    impl_->idle();
    const auto* binding = impl_->find(target);
    if (!impl_->tree.contains(target) || !binding || binding->retiring || binding->ended ||
        !std::isfinite(options.threshold) || options.threshold < 0 || options.threshold > 4096 ||
        options.operations < drag_operations::copy ||
        options.operations > drag_operations::copy_move)
    {
        throw std::invalid_argument("invalid drag source configuration");
    }

    set_metadata(impl_->drag_sources, target, std::move(options), impl_->tree,
                 impl_->limits.max_bindings);
}

/// @brief Typed policy is validated completely before replacing an existing target.
void input_session::configure_drop_target(element_handle target, drop_target_options options)
{
    impl_->idle();
    const auto* binding = impl_->find(target);
    if (!impl_->tree.contains(target) || !binding || binding->retiring || binding->ended ||
        options.types.empty() || options.types.size() > 32 ||
        options.operations < drag_operations::copy ||
        options.operations > drag_operations::copy_move)
    {
        throw std::invalid_argument("invalid drop target configuration");
    }

    for (const auto& type : options.types)
    {
        (void)drag_payload::binary(type, {});
    }

    std::sort(options.types.begin(), options.types.end());
    if (std::adjacent_find(options.types.begin(), options.types.end()) != options.types.end())
    {
        throw std::invalid_argument("duplicate drop type");
    }

    set_metadata(impl_->drop_targets, target, std::move(options), impl_->tree,
                 impl_->limits.max_bindings);
}

/// @brief Remove a source policy only at an owner boundary.
void input_session::reset_drag_source(element_handle target)
{
    impl_->idle();
    std::erase_if(impl_->drag_sources,
                  [&](const auto& item)
                  {
                      return item.first == target;
                  });
}

/// @brief Remove a destination policy only at an owner boundary.
void input_session::reset_drop_target(element_handle target)
{
    impl_->idle();
    std::erase_if(impl_->drop_targets,
                  [&](const auto& item)
                  {
                      return item.first == target;
                  });
}
} // namespace atlas::ui
