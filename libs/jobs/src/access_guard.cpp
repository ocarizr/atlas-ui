#include <atlas/jobs/job_sanitizer.hpp>

#include "access_registry.hpp"
#include <utility>

namespace atlas::jobs
{
access_guard::access_guard(std::shared_ptr<detail::access_registry> registry, std::size_t token)
    : registry_{std::move(registry)}, token_{token}
{
}
access_guard::~access_guard()
{
    release();
}
access_guard::access_guard(access_guard&& other) noexcept
    : registry_{std::move(other.registry_)}, token_{std::exchange(other.token_, 0)}
{
}
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
void access_guard::release() noexcept
{
    if (registry_ && token_ != 0)
    {
        registry_->release(std::exchange(token_, 0));
    }
}
} // namespace atlas::jobs
