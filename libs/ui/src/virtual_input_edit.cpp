/** @file @brief Bounded transactional interaction changes for recycled rows. */
#include "virtual_input_edit.hpp"
#include <tuple>

namespace atlas::ui::detail
{
/// @brief Validate the shared composition boundary before jobs, source callbacks or allocation.
void input_edit::require_idle(input_session& input, std::uint64_t session)
{
    input.impl_->idle();
    if (session && input.impl_->session != session)
    {
        throw std::invalid_argument("virtual list input belongs to another tree");
    }
}

/// @brief Prepare an idle owner registration table without publishing any changes.
input_edit::input_edit(input_session& input)
    : input_(input), bindings_(0, input.impl_->memory.get())
{
    require_idle(input);
    bindings_.reserve(input.impl_->limits.max_bindings);
    for (const auto& value : input.impl_->bindings)
    {
        if (!value.ended)
        {
            bindings_.push_back(value);
        }
    }

    input.impl_->busy = true;
}

/// @brief Release the input owner guard on every exit.
input_edit::~input_edit()
{
    input_.impl_->busy = false;
}

/// @brief Retain the old handler generation for ordered disable/end cleanup.
void input_edit::retire(element_handle target)
{
    for (auto& binding : bindings_)
    {
        if (binding.target == target)
        {
            binding.retiring = true;
        }
    }
}

/// @brief Virtual source rebasing replaces only this domain's physical focused-anchor compensation.
void input_edit::manage_anchor(element_handle target)
{
    for (auto& binding : bindings_)
    {
        if (binding.target == target)
        {
            binding.logical_anchor = true;
            return;
        }
    }

    throw std::logic_error("virtual viewport binding is missing");
}

/// @brief Preflight bounded unique binding admission before tree publication.
void input_edit::add(element_handle target, std::shared_ptr<input_handler> handler,
                     interaction_options options)
{
    if (!handler || std::any_of(bindings_.begin(), bindings_.end(),
                                [target](const auto& value)
                                {
                                    return value.target == target;
                                }))
    {
        throw std::invalid_argument("duplicate or empty virtual row input binding");
    }

    if (bindings_.size() >= input_.impl_->limits.max_bindings)
    {
        throw std::overflow_error("virtual row input admission exhausted");
    }

    bindings_.push_back({target, std::move(handler), options});
}

/// @brief Publish completely prepared state without allocating or invoking user callbacks.
void input_edit::commit() noexcept
{
    std::sort(bindings_.begin(), bindings_.end(),
              [](const auto& a, const auto& b)
              {
                  return std::tie(a.target.index, a.target.generation) <
                         std::tie(b.target.index, b.target.generation);
              });
    input_.impl_->bindings.swap(bindings_);
}
} // namespace atlas::ui::detail
