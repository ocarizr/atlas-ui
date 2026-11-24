/** @file @brief Atomic owner publication and deterministic cancellation-aware notifications. */
#include "state.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace atlas::localization::detail
{
/// @brief All callbacks and mutation are owner-affine; cancellation tokens are independently safe.
void context_state::check(bool mutation) const
{
    if (owner != std::this_thread::get_id())
    {
        throw std::logic_error("localization context belongs to another thread");
    }

    if (mutation && notifying)
    {
        throw std::logic_error("localization mutation during notification");
    }
}

namespace
{
/// @brief Exact tag, progressively shorter parents, then explicit fallback are tried per key.
std::vector<translation> resolve_all(const catalog& tables, std::string tag)
{
    std::vector<std::size_t> order;
    while (!tag.empty())
    {
        for (std::size_t i = 0; i < tables.data.languages.size(); ++i)
        {
            if (tables.data.languages[i].tag == tag)
            {
                order.push_back(i);
            }
        }

        const auto separator = tag.rfind('-');
        if (separator == tag.npos)
        {
            break;
        }

        tag.resize(separator);
    }

    for (std::size_t i = 0; i < tables.data.languages.size(); ++i)
    {
        if (tables.data.languages[i].tag == tables.data.fallback)
        {
            order.push_back(i);
        }
    }

    std::vector<translation> result;
    result.reserve(tables.keys.size());
    for (std::size_t key = 0; key < tables.keys.size(); ++key)
    {
        for (const auto table : order)
        {
            if (const auto* entry = tables.entries[table][key])
            {
                result.push_back({&tables.data.languages[table], entry});
                break;
            }
        }
    }

    return result;
}
} // namespace

/// @brief All potentially throwing preparation precedes the no-throw state swap.
void context_state::publish(std::shared_ptr<const catalog> candidate, std::string tag)
{
    const diagnostics::operation_scope scope{sinks, {"localization.publish", "localization.cpu"}};
    if (revision == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("localization revisions exhausted");
    }

    auto resolved_candidate = resolve_all(*candidate, tag);
    std::vector<std::shared_ptr<observer>> notifications;
    notifications.reserve(observers.size());
    for (const auto& reference : observers)
    {
        if (auto live = reference.lock(); live && live->active.load())
        {
            notifications.push_back(std::move(live));
        }
    }

    tables = std::move(candidate);
    selected.swap(tag);
    resolved.swap(resolved_candidate);
    ++revision;
    notifying = true;
    for (const auto& observer : notifications)
    {
        if (!observer->active.load())
        {
            continue;
        }

        try
        {
            observer->callback(revision);
        }
        catch (...)
        {
            diagnostics::write(sinks, diagnostics::log_level::error, "localization",
                               "change observer failed");
        }
    }

    notifying = false;
    std::erase_if(observers,
                  [](const auto& item)
                  {
                      const auto live = item.lock();
                      return !live || !live->active.load();
                  });
}
} // namespace atlas::localization::detail

namespace atlas::localization
{
/// @brief Registration is bounded and cannot change the active notification batch.
subscription context::observe(std::function<void(std::uint64_t)> callback)
{
    state_->check(true);
    std::erase_if(state_->observers,
                  [](const auto& item)
                  {
                      const auto live = item.lock();
                      return !live || !live->active.load();
                  });
    if (!callback)
    {
        throw std::invalid_argument("empty localization observer");
    }

    if (state_->observers.size() >= 1024)
    {
        throw std::overflow_error("localization observer budget exceeded");
    }

    auto item = std::make_shared<detail::observer>();
    item->callback = std::move(callback);
    state_->observers.push_back(item);
    return subscription{std::move(item)};
}

/// @brief A token retains only cancellation and callback state, not a context cycle.
subscription::subscription(std::shared_ptr<detail::observer> observer)
    : observer_(std::move(observer))
{
}

/// @brief Scope exit cancels delivery before releasing callback storage.
subscription::~subscription()
{
    reset();
}

/// @brief Transfer a single registration without cancelling it.
subscription::subscription(subscription&& other) noexcept : observer_(std::move(other.observer_))
{
}

/// @brief Cancel the replaced registration before taking new ownership.
subscription& subscription::operator=(subscription&& other) noexcept
{
    if (this != &other)
    {
        reset();
        observer_ = std::move(other.observer_);
    }

    return *this;
}

/// @brief Atomic cancellation remains harmless after owner teardown.
void subscription::reset() noexcept
{
    if (observer_)
    {
        observer_->active = false;
    }

    observer_.reset();
}
} // namespace atlas::localization
