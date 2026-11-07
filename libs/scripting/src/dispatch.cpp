/** @file @brief Protected owner dispatch commits producer demands only after successful Lua return.
 */
#include "session_state.hpp"
#include <stdexcept>

namespace atlas::scripting
{
/// @brief Protect one call and atomically admit its model and native-work demands.
std::vector<mvvm::value> session::impl::call(std::string_view name,
                                             std::span<const mvvm::value> args, bool internal,
                                             std::span<const mvvm::value_type> result_types)
{
    if (!internal)
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
    const diagnostics::operation_scope scope{
        destinations, {vm->debugging() ? "script.call.debug" : "script.call", "script.cpu"}};
    try
    {
        if (stats.calls == limits.max_calls)
        {
            throw std::overflow_error("script frame call budget exhausted");
        }

        ++stats.calls;
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

        std::size_t bytes = 0;
        for (const auto& request : vm->requests)
        {
            bytes += request.name.size() + detail::payload_bytes(request.arguments);
        }

        {
            const std::lock_guard lock{mail->mutex};
            if (vm->requests.size() > limits.max_pending_work - mail->pending.size() ||
                bytes > mail->limit - mail->bytes)
            {
                throw std::overflow_error("native work admission budget exhausted");
            }

            // Reserve before releasing the lock: workers can complete while MVVM checkpoints.
            mail->bytes += bytes;
        }
        model->submit(vm->commands);
        {
            const std::lock_guard lock{mail->mutex};
            for (auto& request : vm->requests)
            {
                vm->next_request = request.id;
                mail->pending.push_back(std::move(request));
            }
        }
        vm->commands.clear();
        vm->requests.clear();
        return results;
    }
    catch (...)
    {
        vm->commands.clear();
        vm->requests.clear();
        fail();
        throw;
    }
}
} // namespace atlas::scripting
