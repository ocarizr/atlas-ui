/** @file @brief Apply script listener edits outside model notification traversal. */
#include "domain_state.hpp"
#include <algorithm>

namespace atlas::scripting
{
/// @brief Additions are pre-admitted before removals; a failed admission retires its new prefix.
void session::impl::synchronize_listeners()
{
    check();
    if (listener_changes.empty())
    {
        return;
    }

    std::vector<std::uint64_t> added;
    added.reserve(listener_changes.size());
    try
    {
        for (const auto& change : listener_changes)
        {
            if (!change.handler.empty())
            {
                watch(change.slot, change.handler, change.token);
                added.push_back(change.token);
            }
        }
    }
    catch (...)
    {
        for (auto identity : added)
        {
            auto found = std::find_if(watches.begin(), watches.end(),
                                      [identity](const auto& item)
                                      {
                                          return item.id == identity;
                                      });
            if (found != watches.end())
            {
                model->unsubscribe(found->token);
                watches.erase(found);
            }
        }

        listener_changes.clear();
        fail();
        throw;
    }

    for (const auto& change : listener_changes)
    {
        if (!change.handler.empty())
        {
            continue;
        }

        auto found = std::find_if(watches.begin(), watches.end(),
                                  [&](const auto& item)
                                  {
                                      return item.id == change.token;
                                  });
        if (found != watches.end())
        {
            model->unsubscribe(found->token);
            watches.erase(found);
        }
    }

    listener_changes.clear();
}

/// @brief The host selects the safe boundary; Lua never flushes or changes traversal recursively.
void session::synchronize()
{
    impl_->synchronize_listeners();
}

/// @brief Apply enabled instances in deterministic registration order between model flushes.
void domain::synchronize()
{
    impl_->check();
    const detail::busy_guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"script.domain.listeners", "script.dispatch"}};
    for (auto& entry : impl_->entries)
    {
        if (entry.enabled)
        {
            entry.behavior->synchronize();
        }
    }
}
} // namespace atlas::scripting
