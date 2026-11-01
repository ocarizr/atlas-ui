/**
 * @file
 * @brief Simultaneously live declared-region validation and bounded findings.
 */

#include "access_registry.hpp"
#include "task_state.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace atlas::jobs::detail
{
namespace
{
/// @brief Reject invalid or overflowing declared-memory descriptors before registering them.
access_region checked_region(const memory_access& request)
{
    const auto begin = reinterpret_cast<std::uintptr_t>(request.data);
    if (!request.data || request.bytes > std::numeric_limits<std::uintptr_t>::max() - begin ||
        (request.mode != access_mode::read && request.mode != access_mode::write))
    {
        throw std::invalid_argument{
            "A declared access needs a valid, non-overflowing memory region"};
    }

    return {begin, begin + request.bytes, request.mode};
}

#if ATLAS_JOBS_SANITIZER
/// @brief Normalize one borrowed span descriptor into a validated byte-address range.
std::vector<access_region> normalize(std::span<const memory_access> requests)
{
    std::vector<access_region> result;
    result.reserve(requests.size());
    for (const auto& request : requests)
    {
        if (request.bytes != 0)
        {
            result.push_back(checked_region(request));
        }
    }

    return result;
}
#endif
} // namespace

/// @brief Configure bounded live-access diagnostics and validate the cache-line heuristic.
access_registry::access_registry(job_sanitizer_options options) : options_{options}
{
    if (!std::has_single_bit(options.cache_line_size))
    {
        throw std::invalid_argument{
            "The sanitizer cache-line size must be a positive power of two"};
    }
}

/// @brief Retain a bounded access finding and account for report overflow.
void access_registry::record(diagnostic_kind kind, const std::string& name,
                             const std::string& other)
{
    if (diagnostics_.size() == options_.diagnostic_limit)
    {
        ++dropped_;
        return;
    }

    diagnostics_.push_back({kind,
                            kind == diagnostic_kind::possible_false_sharing
                                ? diagnostic_severity::warning
                                : diagnostic_severity::error,
                            name, other});
}

/// @brief Reject incompatible overlapping live declarations and record cache-line heuristic
/// warnings.
void access_registry::check(const active_access& incoming)
{
    bool warned = false;
    for (const auto& existing : active_)
    {
        if (existing.owner == incoming.owner)
        {
            continue;
        }

        for (const auto& left : incoming.regions)
        {
            for (const auto& right : existing.regions)
            {
                if (left.mode == access_mode::read && right.mode == access_mode::read)
                {
                    continue;
                }

                if (left.begin < right.end && right.begin < left.end)
                {
                    record(diagnostic_kind::conflicting_access, incoming.name, existing.name);
                    throw job_sanitizer_error{"Conflicting declared access: " + incoming.name +
                                              " / " + existing.name};
                }

                const auto line = options_.cache_line_size;
                if (!warned && left.begin / line <= (right.end - 1) / line &&
                    right.begin / line <= (left.end - 1) / line)
                {
                    record(diagnostic_kind::possible_false_sharing, incoming.name, existing.name);
                    warned = true;
                }
            }
        }
    }
}

/// @brief Validate and register simultaneous declared memory scopes, rejecting conflicting access.
std::size_t access_registry::acquire(const task_state& owner,
                                     std::span<const memory_access> requests)
{
#if ATLAS_JOBS_SANITIZER
    auto regions = normalize(requests);
    if (!regions.empty())
    {
        active_access incoming{0, &owner, owner.name, std::move(regions)};
        const std::lock_guard lock{mutex_};
        if (next_token_ == std::numeric_limits<std::size_t>::max())
        {
            throw std::overflow_error{"Sanitizer access tokens exhausted"};
        }

        check(incoming);
        incoming.token = next_token_++;
        active_.push_back(std::move(incoming));
        return active_.back().token;
    }
#else
    (void)owner;
    for (const auto& request : requests)
    {
        if (request.bytes != 0)
        {
            (void)checked_region(request);
        }
    }
#endif
    return 0;
}

/// @brief Retire owned registration state; scoped cleanup must not throw.
void access_registry::release(std::size_t token) noexcept
{
    try
    {
        const std::lock_guard lock{mutex_};
        std::erase_if(active_,
                      [token](const auto& item)
                      {
                          return item.token == token;
                      });
    }
    catch (...)
    {
        std::terminate();
    }
}

/// @brief Retire callback-scoped access registrations and report guards escaping their scope.
bool access_registry::finish(const task_state& owner)
{
#if ATLAS_JOBS_SANITIZER
    const std::lock_guard lock{mutex_};
    const auto removed = std::erase_if(active_,
                                       [&owner](const auto& item)
                                       {
                                           return item.owner == &owner;
                                       });
    if (removed != 0)
    {
        record(diagnostic_kind::escaped_access, owner.name);
        return true;
    }
#else
    (void)owner;
#endif
    return false;
}

/// @brief Copy bounded declared-access findings under the registry mutex; this does not invoke an
/// output or reset findings.
job_sanitizer_report access_registry::report() const
{
    const std::lock_guard lock{mutex_};
    return {static_cast<bool>(ATLAS_JOBS_SANITIZER), diagnostics_, dropped_};
}
} // namespace atlas::jobs::detail
