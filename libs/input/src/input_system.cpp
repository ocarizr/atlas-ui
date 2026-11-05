/** @file @brief Input ownership, bounded admission and explicit recovery. */
#include "input_state.hpp"
#include <cmath>

namespace atlas::input
{
/// @brief Labels are copied and never borrow caller memory.
std::array<char, 64> checked_name(std::string_view value)
{
    if (value.empty() || value.size() > 64 ||
        value.find_first_not_of(' ') == std::string_view::npos ||
        std::any_of(value.begin(), value.end(),
                    [](unsigned char c)
                    {
                        return c < 32 || c == 127;
                    }))
    {
        throw std::invalid_argument("input name requires 1..64 non-control bytes");
    }

    std::array<char, 64> result{};
    std::copy(value.begin(), value.end(), result.begin());
    return result;
}

/// @brief No backend or processor may publish nonfinite components.
void validate_value(input_value value)
{
    if (!std::isfinite(value.x) || !std::isfinite(value.y))
    {
        throw std::invalid_argument("nonfinite input value");
    }
}

/// @brief Reserve hot-path vector capacity before dispatch starts.
input_system::impl::impl(input_limits policy, std::shared_ptr<std::pmr::memory_resource> resource,
                         diagnostics::services services)
    : limits(policy), memory(std::move(resource)), destinations(std::move(services)),
      actions(0, memory.get()), contexts(0, memory.get()), order(0, memory.get()),
      bindings(0, memory.get()), controls(0, memory.get()), scratch_controls(0, memory.get()),
      devices(0, memory.get()), scratch_devices(0, memory.get()), pending(0, memory.get())
{
    actions.reserve(limits.max_actions);
    contexts.reserve(limits.max_contexts);
    order.reserve(limits.max_contexts);
    bindings.reserve(limits.max_bindings);
    pending.reserve(limits.max_contexts);
    controls.reserve(limits.max_controls);
    scratch_controls.reserve(limits.max_controls);
    devices.reserve(limits.max_devices);
    scratch_devices.reserve(limits.max_devices);
}

/// @brief Immutable identity is checked before mutable guards.
void input_system::impl::check_thread() const
{
    if (owner != std::this_thread::get_id())
    {
        throw std::logic_error("input system requires its owner");
    }
}

/// @brief Configuration/observation cannot reenter processing.
void input_system::impl::check() const
{
    check_thread();
    if (busy || requesting)
    {
        throw std::logic_error("input system is not reentrant");
    }
}

/// @brief Reusable input requires diagnostics, not UI, a window or an executor.
input_system::input_system(input_limits limits, std::shared_ptr<std::pmr::memory_resource> memory,
                           diagnostics::services destinations)
{
    if (!limits.max_actions || limits.max_actions > 4096 || !limits.max_contexts ||
        limits.max_contexts > 256 || !limits.max_bindings || limits.max_bindings > 16384 ||
        !limits.max_devices || limits.max_devices > 256 || !limits.max_controls ||
        limits.max_controls > 16384 || !limits.max_events || limits.max_events > 4096 ||
        !limits.max_callbacks || limits.max_callbacks > 1048576)
    {
        throw std::invalid_argument("invalid input limits");
    }

    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](std::pmr::memory_resource*)
                  {
                  }};
    }

    impl_ = std::make_unique<impl>(limits, std::move(memory), std::move(destinations));
}

/// @brief No subscription tail survives destruction.
input_system::~input_system()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy || impl_->requesting)
    {
        std::terminate();
    }

    impl_->busy = true;
}

/// @brief Retain definitions while clearing physical/interaction state.
void input_system::impl::neutralize()
{
    controls.clear();
    devices.clear();
    pending.clear();
    for (auto& a : actions)
    {
        a.value = a.sum = {};
        a.active = a.performed = a.rejected = false;
        a.winner = 0;
    }

    for (auto& c : contexts)
    {
        c.rearm = true;
    }
}

/// @brief Client side effects are not rolled back; recovery is explicit.
void input_system::impl::fail() noexcept
{
    stats.failed = true;
    delivering = false;
    listener = nullptr;
    neutralize();
    diagnostics::write(destinations, diagnostics::log_level::warning, "input",
                       "input dispatch failed; explicit reset required");
}

/// @brief Recovery does not silently invoke a faulty observer again.
void input_system::reset()
{
    impl_->check();
    const impl::guard guard{impl_->busy};
    impl_->neutralize();
    impl_->stats.failed = false;
}

/// @brief Observe on the publishing owner.
input_statistics input_system::statistics() const
{
    impl_->check();
    return impl_->stats;
}
} // namespace atlas::input
