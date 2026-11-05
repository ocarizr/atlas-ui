/**
 * @file
 * @brief Extensible lightweight value processors and stateless interaction policies.
 */
#pragma once
#include <atlas/input/events.hpp>
#include <memory>
#include <vector>

namespace atlas::input
{
class value_modifier
{
  public:
    virtual ~value_modifier() = default; ///< Shared immutable processor ownership.
    virtual input_value
    apply(input_value value) const = 0; ///< Fast finite transformation; no waits.
}; ///< Consumers can extend processing without changing binding dispatch.

/// @brief Independent finite component scaling.
class scale_modifier final : public value_modifier
{
  public:
    explicit scale_modifier(input_value factors); ///< Finite per-component scale/swizzle weight.
    input_value apply(input_value value) const override; ///< Scale both components with checks.
  private:
    input_value factors_; ///< Immutable policy.
};

/// @brief Map a scalar control into a weighted two-dimensional direction.
class axis_modifier final : public value_modifier
{
  public:
    explicit axis_modifier(input_value direction);       ///< Map a scalar into a vector direction.
    input_value apply(input_value value) const override; ///< Explicit scalar-to-vector composite.
  private:
    input_value direction_; ///< Finite authored contribution.
};

/// @brief Radial analog deadzone with normalized remainder.
class deadzone_modifier final : public value_modifier
{
  public:
    explicit deadzone_modifier(float radius);            ///< Finite radius in [0, 1).
    input_value apply(input_value value) const override; ///< Radial zero/remap into the unit range.
  private:
    float radius_; ///< Immutable deadzone threshold.
};

class normalize_modifier final : public value_modifier
{
  public:
    input_value apply(input_value value) const override; ///< Limit magnitude to one, preserve zero.
}; ///< Useful for composite keyboard directions and gamepad vectors.

/// @brief Ordered bounded immutable modifier composition.
class modifier_chain final : public value_modifier
{
  public:
    /// @brief Retain at most eight nonnull immutable modifiers.
    explicit modifier_chain(std::vector<std::shared_ptr<const value_modifier>> modifiers);
    ///< Retain at most eight nonnull processors, applied in authored order.
    input_value
    apply(input_value value) const override; ///< Sequential lightweight transformations.
  private:
    std::vector<std::shared_ptr<const value_modifier>> modifiers_; ///< Immutable retained policy.
};

struct trigger_sample
{
    double elapsed = 0;     ///< Caller-clock seconds since actuation.
    bool released = false;  ///< The action just became neutral.
    bool performed = false; ///< Already performed in the current actuation.
}; ///< Stateful timing belongs to the action, not a shared trigger instance.

/// @brief One recognition result from a caller-clock interaction sample.
enum class trigger_decision : std::uint8_t
{
    perform, ///< Activate now.
    wait,    ///< Still recognizing the interaction.
    reject   ///< Cancel this actuation until neutral.
};

/// @brief Immutable extension contract; per-action timing is owned by the input system.
class input_trigger
{
  public:
    virtual ~input_trigger() = default; ///< Shared immutable interaction rule.
    /// @brief Evaluate without waiting or mutating shared interaction state.
    virtual trigger_decision evaluate(trigger_sample sample) const = 0;
    ///< Evaluate without sleeps, device polling or mutation of shared processor state.
};

/// @brief Perform after continuous actuation for the configured duration.
class hold_trigger final : public input_trigger
{
  public:
    explicit hold_trigger(double seconds); ///< Finite positive recognition duration.
    trigger_decision evaluate(trigger_sample sample) const override; ///< Perform at the threshold.
  private:
    double seconds_; ///< Immutable duration.
};

class tap_trigger final : public input_trigger
{
  public:
    explicit tap_trigger(double seconds); ///< Finite positive maximum tap duration.
    trigger_decision evaluate(trigger_sample sample) const override; ///< Perform on timely release.
  private:
    double seconds_; ///< Immutable duration.
}; ///< A null action trigger implements ordinary press/value behavior.
} // namespace atlas::input
