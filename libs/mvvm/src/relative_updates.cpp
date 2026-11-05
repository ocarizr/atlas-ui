/** @file @brief Ordered numeric deltas avoid stale read/modify/write across independent actions. */
#include "runtime_state.hpp"
#include <cmath>
#include <limits>

namespace atlas::mvvm
{
/// @brief Preserve every integer demand and check overflow against committed state at dispatch.
void view_model::add_integer(field_handle slot, std::int64_t delta)
{
    impl_->writable();
    (void)impl_->offset(slot, value_type::integer);
    value argument{delta};
    impl_->enqueue(notification_kind::changed, slot.index, std::span{&argument, 1}, &argument,
                   mutation_kind::add_integer);
}

/// @brief Floating deltas remain finite and are never coalesced as absolute replacements.
void view_model::add_number(field_handle slot, double delta)
{
    impl_->writable();
    (void)impl_->offset(slot, value_type::number);
    value argument{delta};
    detail::validate_value(argument, impl_->layout->limits.max_text_bytes);
    impl_->enqueue(notification_kind::changed, slot.index, std::span{&argument, 1}, &argument,
                   mutation_kind::add_number);
}

/// @brief Notifications receive the new committed candidate rather than its relative demand.
void view_model::impl::resolve(const queued_command& command, value& argument) const
{
    if (command.mutation == mutation_kind::add_integer)
    {
        const auto current = integers[layout->offsets[command.index]];
        const auto delta = std::get<std::int64_t>(argument);
        if ((delta > 0 && current > std::numeric_limits<std::int64_t>::max() - delta) ||
            (delta < 0 && current < std::numeric_limits<std::int64_t>::min() - delta))
        {
            throw std::overflow_error("MVVM integer delta overflow");
        }

        argument = current + delta;
    }
    else if (command.mutation == mutation_kind::add_number)
    {
        const auto result = numbers[layout->offsets[command.index]] + std::get<double>(argument);
        if (!std::isfinite(result))
        {
            throw std::overflow_error("MVVM number delta overflow");
        }

        argument = result;
    }
}
} // namespace atlas::mvvm
