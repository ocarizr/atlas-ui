/** @file @brief One checked execution policy for standalone sessions and shared domains. */
#include "session_state.hpp"
#include <stdexcept>

namespace atlas::scripting::detail
{
/// @brief Reject invalid bounds before allocating interpreters or retaining source catalogs.
void validate_limits(session_limits limits)
{
    if (limits.max_memory_bytes < 65536 ||
        limits.max_memory_bytes > std::size_t{64} * 1024 * 1024 || !limits.max_source_bytes ||
        limits.max_source_bytes > 1048576 || !limits.max_commands || limits.max_commands > 4096 ||
        !limits.max_text_bytes || limits.max_text_bytes > 1048576 || !limits.max_pending_work ||
        limits.max_pending_work > 1024 || !limits.max_calls || limits.max_calls > 65536 ||
        limits.max_instructions < 100 || limits.max_instructions > 10000000 ||
        !limits.max_pending_bytes || limits.max_pending_bytes > std::size_t{64} * 1024 * 1024)
    {
        throw std::invalid_argument("invalid scripting execution limits");
    }
}
} // namespace atlas::scripting::detail
