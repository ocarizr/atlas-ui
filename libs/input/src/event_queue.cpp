/** @file @brief Nonwaiting bounded owner ingress with explicit overflow reset. */
#include <atlas/input/events.hpp>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <utility>

namespace atlas::input
{
namespace
{
/// @brief Cold-path source identity never wraps to another queue's identity.
std::uint64_t next_source()
{
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load();
    do
    {
        if (value == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("input sources exhausted");
        }
    } while (!next.compare_exchange_weak(value, value + 1));

    return value;
}
} // namespace

/// @brief Collect on the construction thread; cross-thread SDK ingress needs another adapter.
event_queue::event_queue() : source_(next_source())
{
}

/// @brief No waits, heap allocation or user callback can occur on publication.
bool event_queue::try_push(event value) noexcept
{
    if (owner_ != std::this_thread::get_id())
    {
        return false;
    }

    if (batch_.overflow)
    {
        return false;
    }

    if (batch_.count == batch_.events.size())
    {
        batch_.count = 1;
        batch_.events[0] = {event_kind::source_reset, {}, 0, {}, source_};
        batch_.overflow = true;
        return false;
    }

    value.source = source_;
    batch_.events[batch_.count++] = value;
    return true;
}

/// @brief Draining returns immediately even when there are no events.
event_batch event_queue::drain()
{
    if (owner_ != std::this_thread::get_id())
    {
        throw std::logic_error("input queue requires its owner");
    }

    return std::exchange(batch_, {});
}
} // namespace atlas::input
