/** @file @brief Bounded catalog views and revision-stamped source preparation. */
#include "asset_repository.hpp"
#include <algorithm>
#include <limits>

namespace atlas::editor
{
/// @brief Reserve history before any transaction can persist data.
asset_repository::asset_repository(assets::catalog initial, std::shared_ptr<asset_store> storage,
                                   diagnostics::services sinks)
    : current_{std::move(initial)}, storage_{std::move(storage)}, sinks_{std::move(sinks)},
      watermark_{current_.document().next_local}
{
    if (!storage_ || current_.document().records.size() > 1024)
    {
        throw std::invalid_argument("Asset repository requires storage and at most 1024 records");
    }

    undo_.reserve(16);
    redo_.reserve(16);
}

/// @brief No completion callback owns or borrows this repository.
asset_repository::~asset_repository()
{
    if (pending_)
    {
        pending_->read->cancel();
    }
}

/// @brief Borrow an immutable snapshot independently of subsequent edits.
const assets::catalog& asset_repository::snapshot() const noexcept
{
    return current_;
}

/// @brief Case-insensitive ASCII search follows the catalog's portable locator alphabet.
std::vector<asset_row> asset_repository::browse(assets::asset_type type,
                                                std::string_view query) const
{
    const auto folded = [](std::string text)
    {
        std::ranges::transform(text, text.begin(),
                               [](char c)
                               {
                                   return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a')
                                                               : c;
                               });
        return text;
    };
    const auto search = folded(std::string{query});
    std::vector<asset_row> rows;
    for (const auto& record : current_.document().records)
    {
        if (record.reference.type == type &&
            folded(record.source.path).find(search) != std::string::npos)
        {
            rows.push_back({record, !storage_->exists(record.source)});
        }
    }

    return rows;
}

/// @brief Both explicit dependencies and derived-product bindings prevent dangling deletion.
std::vector<assets::asset_ref> asset_repository::dependents(assets::asset_ref asset) const
{
    (void)current_.resolve(asset);
    std::vector<assets::asset_ref> output;
    for (const auto& item : current_.document().records)
    {
        if (std::ranges::find(item.dependencies, asset) != item.dependencies.end() ||
            std::ranges::any_of(item.products,
                                [&](const auto& link)
                                {
                                    return link.target == asset;
                                }))
        {
            output.push_back(item.reference);
        }
    }

    return output;
}

/// @brief Require a catalog-owned identity before changing its locator or importer state.
assets::asset_record& asset_repository::record(assets::catalog_document& draft,
                                               assets::asset_ref ref)
{
    const auto found = std::ranges::find(draft.records, ref, &assets::asset_record::reference);
    if (found == draft.records.end())
    {
        throw std::invalid_argument("Unknown or incorrectly typed editor asset");
    }

    return *found;
}

/// @brief Validate names and reserve the result before any asynchronous acquisition.
void asset_repository::begin(asset_transfer request)
{
    const diagnostics::operation_scope scope{sinks_, {"editor.assets.prepare", "editor.assets"}};
    if (pending_)
    {
        throw std::logic_error("An asset preparation is already pending");
    }

    if (request.target.root != assets::format_type(request.type))
    {
        throw std::invalid_argument("Asset source root must match its type");
    }

    auto draft = current_.document();
    draft.next_local = watermark_;
    if (request.asset)
    {
        if (request.asset->type != request.type)
        {
            throw std::invalid_argument("Relink cannot change asset type");
        }

        record(draft, *request.asset).source = request.target;
    }
    else
    {
        if (draft.records.size() == 1024)
        {
            throw std::length_error("Editor asset record capacity exhausted");
        }

        draft.records.push_back({{assets::allocate_id(draft), request.type}, request.target});
    }

    (void)assets::catalog::compile(std::move(draft), sinks_);
    const auto target = storage_->locate(request.target);
    if (request.copy && storage_->exists(request.target))
    {
        throw std::invalid_argument("Asset destination is occupied");
    }

    if (!request.copy)
    {
        request.source = target;
    }

    auto read = storage_->read(request.source, request.type);
    if (!read)
    {
        throw std::runtime_error("Asset storage did not return an acquisition");
    }

    pending_.emplace(pending{std::move(request), std::move(read), generation_});
}

/// @brief Invalidate delivery before asking storage to cancel.
void asset_repository::cancel()
{
    if (generation_ == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("Asset publication generations exhausted");
    }

    ++generation_;
    if (pending_)
    {
        pending_->read->cancel();
    }
}

/// @brief The owner must collect even a canceled or failed ready result.
bool asset_repository::busy() const noexcept
{
    return pending_.has_value();
}

/// @brief Validate complete worker output before persisting a source and its metadata.
bool asset_repository::poll()
{
    if (!pending_ || !pending_->read->ready())
    {
        return false;
    }

    auto accepted = std::move(*pending_);
    pending_.reset();
    if (accepted.generation != generation_)
    {
        return false;
    }

    const diagnostics::operation_scope scope{sinks_, {"editor.assets.publish", "editor.assets"}};
    auto payload = accepted.read->take();
    if (!payload.bytes || payload.bytes->size() > std::size_t{16} * 1024 * 1024)
    {
        throw std::length_error("Asset preparation exceeds the 16 MiB buffer budget");
    }

    if (payload.digest.size() != 64)
    {
        throw std::invalid_argument("Prepared assets require a SHA-256 fingerprint");
    }

    std::ranges::sort(payload.dependencies);
    if (accepted.request.asset && !accepted.request.copy)
    {
        const auto& records = current_.document().records;
        const auto& previous =
            *std::ranges::find(records, *accepted.request.asset, &assets::asset_record::reference);
        if (previous.source == accepted.request.target && previous.source_hash == payload.digest &&
            previous.byte_size == payload.bytes->size() &&
            previous.dependencies == payload.dependencies)
        {
            return false;
        }
    }

    auto draft = current_.document();
    draft.next_local = watermark_;
    auto ref = accepted.request.asset;
    if (!ref)
    {
        ref = assets::asset_ref{assets::allocate_id(draft), accepted.request.type};
        draft.records.push_back({*ref, accepted.request.target});
    }
    else
    {
        invalidate(draft, *ref);
    }

    auto& item = record(draft, *ref);
    item.source = accepted.request.target;
    item.byte_size = payload.bytes->size();
    item.source_hash = std::move(payload.digest);
    item.dependencies = std::move(payload.dependencies);
    const auto validated = assets::catalog::compile(draft, sinks_);
    (void)validated;
    bool created = false;
    try
    {
        if (accepted.request.copy)
        {
            storage_->create(item.source, *payload.bytes);
            created = true;
        }

        commit(std::move(draft));
    }
    catch (...)
    {
        const auto failure = std::current_exception();
        if (created)
        {
            storage_->rollback(accepted.request.target);
        }

        std::rethrow_exception(failure);
    }

    return true;
}
} // namespace atlas::editor
