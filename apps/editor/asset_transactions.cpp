/** @file @brief Persisted catalog history and transitive derived-product invalidation. */
#include "asset_repository.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace atlas::editor
{
namespace
{
/// @brief Reserve history at construction and retire the oldest immutable snapshot at capacity.
void retain(std::vector<assets::catalog>& history, const assets::catalog& snapshot)
{
    if (history.size() == 16)
    {
        history.erase(history.begin());
    }

    history.push_back(snapshot);
}
} // namespace

/// @brief Publication performs no allocation after the durable metadata replacement.
void asset_repository::commit(assets::catalog_document draft)
{
    if (generation_ == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("Asset publication generations exhausted");
    }

    draft.next_local = std::max(draft.next_local, watermark_);
    auto candidate = assets::catalog::compile(std::move(draft), sinks_);
    storage_->publish(candidate);
    retain(undo_, current_);
    current_ = std::move(candidate);
    watermark_ = current_.document().next_local;
    redo_.clear();
    ++generation_;
}

/// @brief Changed generations invalidate consumers once in deterministic catalog order.
void asset_repository::invalidate(assets::catalog_document& draft, assets::asset_ref changed)
{
    std::set<assets::asset_ref> affected{changed};
    bool added = true;
    while (added)
    {
        added = false;
        for (const auto& item : draft.records)
        {
            if (std::ranges::any_of(item.dependencies,
                                    [&](auto ref)
                                    {
                                        return affected.contains(ref);
                                    }))
            {
                added |= affected.insert(item.reference).second;
            }
        }
    }

    for (auto& item : draft.records)
    {
        if (affected.contains(item.reference))
        {
            if (item.revision == std::numeric_limits<std::uint64_t>::max())
            {
                throw std::overflow_error("Asset revisions exhausted");
            }

            ++item.revision;
            item.products.clear();
        }
    }
}

/// @brief Removing registration preserves source files for undo and requires no remaining users.
void asset_repository::remove(assets::asset_ref asset)
{
    if (!dependents(asset).empty())
    {
        throw std::invalid_argument("Asset is still referenced");
    }

    auto draft = current_.document();
    (void)record(draft, asset);
    std::erase_if(draft.records,
                  [&](const auto& item)
                  {
                      return item.reference == asset;
                  });
    commit(std::move(draft));
}

/// @brief Importer settings advance the same generation contract as changed source bytes.
void asset_repository::configure(assets::asset_ref asset, std::uint32_t settings_version)
{
    if (settings_version == 0)
    {
        throw std::invalid_argument("Importer settings version must be positive");
    }

    auto draft = current_.document();
    record(draft, asset).settings_version = settings_version;
    invalidate(draft, asset);
    commit(std::move(draft));
}

/// @brief Undo restores metadata with fresh revisions so old prepared consumers remain stale.
bool asset_repository::restore(bool forward,
                               const std::function<bool(assets::asset_ref)>& referenced)
{
    auto& from = forward ? redo_ : undo_;
    auto& to = forward ? undo_ : redo_;
    if (from.empty())
    {
        return false;
    }

    if (generation_ == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("Asset publication generations exhausted");
    }

    auto draft = from.back().document();
    draft.next_local = watermark_;
    for (auto& item : draft.records)
    {
        const auto found = std::ranges::find(current_.document().records, item.reference,
                                             &assets::asset_record::reference);
        if (found != current_.document().records.end())
        {
            item.revision = std::max(item.revision, found->revision);
        }

        if (item.revision == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("Asset revisions exhausted");
        }

        ++item.revision;
        item.products.clear();
    }

    if (referenced)
    {
        for (const auto& item : current_.document().records)
        {
            if (std::ranges::find(draft.records, item.reference,
                                  &assets::asset_record::reference) == draft.records.end() &&
                referenced(item.reference))
            {
                throw std::invalid_argument("Catalog history would remove a referenced asset");
            }
        }
    }

    auto candidate = assets::catalog::compile(std::move(draft), sinks_);
    storage_->publish(candidate);
    retain(to, current_);
    current_ = std::move(candidate);
    from.pop_back();
    ++generation_;
    return true;
}

/// @brief Restore the previous catalog with fresh source generations.
bool asset_repository::undo(const std::function<bool(assets::asset_ref)>& referenced)
{
    return restore(false, referenced);
}

/// @brief Restore the next catalog with the same atomic history contract.
bool asset_repository::redo(const std::function<bool(assets::asset_ref)>& referenced)
{
    return restore(true, referenced);
}
} // namespace atlas::editor
