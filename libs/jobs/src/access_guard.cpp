/**
 * @file
 * @brief Move-only live-access registration and no-throw scoped retirement.
 */

#include <atlas/jobs/job_sanitizer.hpp>

#include "access_registry.hpp"
#include <utility>

namespace atlas::jobs
{
/// @brief Initialize move-only declared-access registration; destroy it inside the callback before
/// the last access ends.
access_guard::access_guard(std::shared_ptr<detail::access_registry> registry, std::size_t token)
    : registry_{std::move(registry)}, token_{token}
{
}

/// @brief Retire this live access registration without throwing.
access_guard::~access_guard()
{
    release();
}

/// @brief Initialize move-only declared-access registration; destroy it inside the callback before
/// the last access ends.
access_guard::access_guard(access_guard&& other) noexcept
    : registry_{std::move(other.registry_)}, token_{std::exchange(other.token_, 0)}
{
}

/// @brief Transfer the owned state and leave the moved-from object safely destructible.
access_guard& access_guard::operator=(access_guard&& other) noexcept
{
    if (this != &other)
    {
        release();
        registry_ = std::move(other.registry_);
        token_ = std::exchange(other.token_, 0);
    }

    return *this;
}

/// @brief Retire owned registration state; scoped cleanup must not throw.
void access_guard::release() noexcept
{
    if (registry_ && token_ != 0)
    {
        registry_->release(std::exchange(token_, 0));
    }
}
} // namespace atlas::jobs
