/** @file @brief Atomic model/native/listener producer admission after successful Lua execution. */
#include "session_state.hpp"
#include <stdexcept>

namespace atlas::scripting
{
/// @brief Reserve fallible host storage before admitting model demands or publishing native work.
void session::impl::admit()
{
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
        if (!vm->requests.empty())
        {
            mail->pending.reserve(limits.max_pending_work);
        }

        mail->bytes += bytes;
    }
    if (vm->listeners.size() > 1024 - listener_changes.size())
    {
        throw std::overflow_error("pending script listener capacity exhausted");
    }

    listener_changes.reserve(listener_changes.size() + vm->listeners.size());
    model->submit(vm->commands);
    for (auto& listener : vm->listeners)
    {
        listener_changes.push_back(std::move(listener));
    }

    sequence = vm->listener_sequence;
    vm->listeners.clear();
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
}
} // namespace atlas::scripting
