/** @file @brief Guarded development attachment never exposes a Lua pointer to consumers. */
#include "session_state.hpp"
#include <stdexcept>

namespace atlas::scripting
{
/// @brief The linked library, rather than consumer preprocessor flags, owns the capability.
bool session::debugger_available() noexcept
{
#if ATLAS_LUA_DEBUGGER_ENABLED
    return true;
#else
    return false;
#endif
}

/// @brief Attach only to a loaded, healthy owner session outside event dispatch.
void session::attach_debugger(debugger_options options)
{
    impl_->check();
    if (!impl_->vm || impl_->stats.failed)
    {
        throw std::logic_error("debugger requires a loaded healthy session");
    }

    const detail::busy_guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"script.debugger.attach", "script.debugger"}};
    impl_->vm->attach(std::move(options));
    diagnostics::write(impl_->destinations, diagnostics::log_level::info, "scripting",
                       "development debugger attached; debug timings include pauses");
}

/// @brief Detach before closing application state; no Lua callback is required.
void session::detach_debugger()
{
    impl_->check();
    const detail::busy_guard guard{impl_->busy};
    if (impl_->vm)
    {
        impl_->vm->detach();
    }

    diagnostics::write(impl_->destinations, diagnostics::log_level::info, "scripting",
                       "development debugger detached");
}
} // namespace atlas::scripting
