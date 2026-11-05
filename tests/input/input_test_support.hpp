/** @file @brief Per-test callback observations and deterministic normalized samples. */
#pragma once
#include <atlas/input/input_system.hpp>
#include <functional>
#include <gtest/gtest.h>

namespace atlas::input::tests
{
/// @brief Per-test raw and semantic recording with optional failure injection.
class recorder final : public input_listener
{
  public:
    std::vector<action_event> actions;               ///< Owned semantic event observations.
    std::vector<event> raw;                          ///< Owned normalized event observations.
    std::function<void(const action_event&)> action; ///< Optional serial callback demand/failure.

    void on_action(const action_event& value) override
    {
        actions.push_back(value);
        if (action)
        {
            action(value);
        }
    } ///< Record before invoking test behavior.

    void on_input(const event& value) override
    {
        raw.push_back(value);
    } ///< Preserve arrival order.
};

inline event key_sample(key code, bool down, std::uint64_t source = 0)
{
    return {event_kind::control_changed,
            {1, device_kind::keyboard},
            static_cast<std::uint32_t>(code),
            {down ? 1.F : 0.F, 0},
            source};
} ///< No native SDK or wall clock in replay fixtures.

inline std::vector<action_phase> phases(const recorder& value)
{
    std::vector<action_phase> result;
    result.reserve(value.actions.size());
    for (const auto& action : value.actions)
    {
        result.push_back(action.phase);
    }

    return result;
} ///< Compare semantic traces without inspecting implementation state.
} // namespace atlas::input::tests
