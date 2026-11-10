/** @file @brief Protected owner dispatch commits producer demands only after successful Lua return.
 */
#include "session_state.hpp"
#include <optional>
#include <stdexcept>

namespace atlas::scripting
{
/// @brief Protect one call and atomically admit its model and native-work demands.
std::vector<mvvm::value> session::impl::call(std::string_view name,
                                             std::span<const mvvm::value> args,
                                             std::uint64_t native_request,
                                             std::span<const mvvm::value_type> result_types)
{
    if (!native_request)
    {
        check();
    }

    if (!vm || stats.failed || !stats.frame || model->inspect().frame != stats.frame ||
        model->inspect().failed)
    {
        throw std::logic_error("scripting needs a healthy admitted frame");
    }

    mvvm::validate_text(name, 128);
    if (name.empty())
    {
        throw std::invalid_argument("empty script handler name");
    }

    detail::validate_values(args, limits.max_text_bytes);
    if (result_types.size() > 32)
    {
        throw std::invalid_argument("invalid script result type contract");
    }

    const detail::busy_guard guard{busy};
    if (interpreter && interpreter->dispatching)
    {
        throw std::logic_error("shared Lua dispatch is reentrant");
    }

    std::optional<detail::busy_guard> shared_guard;
    if (interpreter)
    {
        shared_guard.emplace(interpreter->dispatching);
    }

    const diagnostics::operation_scope scope{
        destinations, {vm->debugging() ? "script.call.debug" : "script.call", "script.cpu"}};
    try
    {
        if (stats.calls == limits.max_calls)
        {
            throw std::overflow_error("script frame call budget exhausted");
        }

        ++stats.calls;
        if (vm->authored)
        {
            vm->authored->frame = stats.frame;
            vm->authored->request = native_request;
            vm->authored->observations = 0;
        }

        vm->listener_sequence = sequence;
        auto results = vm->call(name, args);
        if (!result_types.empty())
        {
            if (results.size() > result_types.size())
            {
                throw std::invalid_argument("script returned too many protocol values");
            }

            for (std::size_t i = 0; i < results.size(); ++i)
            {
                if (mvvm::type_of(results[i]) != result_types[i])
                {
                    throw std::invalid_argument("script result type mismatch");
                }
            }
        }

        if (model->inspect().frame != stats.frame || model->inspect().failed)
        {
            throw std::logic_error("model frame changed during script dispatch");
        }

        admit();
        return results;
    }
    catch (...)
    {
        vm->commands.clear();
        vm->requests.clear();
        vm->listeners.clear();
        fail();
        throw;
    }
}
} // namespace atlas::scripting
