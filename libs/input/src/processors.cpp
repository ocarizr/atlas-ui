/** @file @brief Finite value kernels and deterministic caller-clock interactions. */
#include <algorithm>
#include <atlas/input/processors.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace atlas::input
{
namespace
{
/// @brief Intermediate double arithmetic cannot publish nonfinite float components.
float checked(double value)
{
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
    {
        throw std::overflow_error("input modifier value overflow");
    }

    return static_cast<float>(value);
}
} // namespace

/// @brief Construction rejects nonfinite policy before it can enter an action map.
scale_modifier::scale_modifier(input_value factors) : factors_(factors)
{
    if (!std::isfinite(factors.x) || !std::isfinite(factors.y))
    {
        throw std::invalid_argument("invalid input scale");
    }
}

/// @brief Scalar/button input can be weighted into a 2D composite by an axis modifier.
input_value scale_modifier::apply(input_value value) const
{
    return {checked(static_cast<double>(value.x) * factors_.x),
            checked(static_cast<double>(value.y) * factors_.y)};
}

/// @brief Direction weights are independent from keyboard/device codes.
axis_modifier::axis_modifier(input_value direction) : direction_(direction)
{
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y))
    {
        throw std::invalid_argument("invalid input direction");
    }
}

/// @brief Positive/negative directions form deterministic scalar-key composites.
input_value axis_modifier::apply(input_value value) const
{
    return {checked(static_cast<double>(value.x) * direction_.x),
            checked(static_cast<double>(value.x) * direction_.y)};
}

/// @brief A zero-sized deadzone is valid; a radius of one cannot be remapped safely.
deadzone_modifier::deadzone_modifier(float radius) : radius_(radius)
{
    if (!std::isfinite(radius) || radius < 0 || radius >= 1)
    {
        throw std::invalid_argument("invalid input deadzone");
    }
}

/// @brief Radial remapping preserves direction and clamps excessive magnitude.
input_value deadzone_modifier::apply(input_value value) const
{
    const auto length = std::hypot(static_cast<double>(value.x), value.y);
    if (!std::isfinite(length))
    {
        throw std::invalid_argument("invalid input vector");
    }

    if (length <= radius_)
    {
        return {};
    }

    const auto scale = (std::min(1.0, length) - radius_) / (1 - radius_) / length;
    return {checked(value.x * scale), checked(value.y * scale)};
}

/// @brief Analog values below unit length retain their strength.
input_value normalize_modifier::apply(input_value value) const
{
    const auto length = std::hypot(static_cast<double>(value.x), value.y);
    if (!std::isfinite(length))
    {
        throw std::invalid_argument("invalid input vector");
    }

    if (length <= 1)
    {
        return value;
    }

    return {checked(value.x / length), checked(value.y / length)};
}

/// @brief Ordered immutable composition is bounded independently of event count.
modifier_chain::modifier_chain(std::vector<std::shared_ptr<const value_modifier>> modifiers)
    : modifiers_(std::move(modifiers))
{
    if (modifiers_.size() > 8 || std::any_of(modifiers_.begin(), modifiers_.end(),
                                             [](const auto& p)
                                             {
                                                 return !p;
                                             }))
    {
        throw std::invalid_argument("input modifier chain needs at most eight nonnull processors");
    }
}

/// @brief Custom processors execute on the owner and must honor the no-wait contract.
input_value modifier_chain::apply(input_value value) const
{
    for (const auto& modifier : modifiers_)
    {
        value = modifier->apply(value);
    }

    return value;
}

/// @brief Timing policies retain no mutable interaction state.
hold_trigger::hold_trigger(double seconds) : seconds_(seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0)
    {
        throw std::invalid_argument("invalid hold duration");
    }
}

/// @brief No worker, timer thread or sleep is needed for recognition.
trigger_decision hold_trigger::evaluate(trigger_sample sample) const
{
    return sample.elapsed >= seconds_ ? trigger_decision::perform
           : sample.released          ? trigger_decision::reject
                                      : trigger_decision::wait;
}

/// @brief Tap uses the same monotonic owner clock as hold.
tap_trigger::tap_trigger(double seconds) : seconds_(seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0)
    {
        throw std::invalid_argument("invalid tap duration");
    }
}

/// @brief Timely release performs; expiry rejects until neutral.
trigger_decision tap_trigger::evaluate(trigger_sample sample) const
{
    if (sample.elapsed > seconds_)
    {
        return trigger_decision::reject;
    }

    return sample.released ? trigger_decision::perform : trigger_decision::wait;
}
} // namespace atlas::input
