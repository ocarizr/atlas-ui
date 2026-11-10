/** @file @brief Validate and publish one immutable typed asset catalog. */
#include "catalog_limits.hpp"
#include <algorithm>
#include <atlas/assets/catalog.hpp>
#include <set>
#include <stdexcept>

namespace atlas::assets
{
namespace
{
/// @brief Bound authoring keys and portable locator components to a stable alphabet.
bool token(std::string_view value, std::size_t limit)
{
    return !value.empty() && value.size() <= limit &&
           value.find_first_not_of(
               "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") ==
               std::string_view::npos;
}

/// @brief Portable paths use ASCII, so collision checks are independent of locale.
std::string fold_ascii(std::string_view value)
{
    std::string folded{value};
    std::ranges::transform(folded, folded.begin(),
                           [](char letter)
                           {
                               return letter >= 'A' && letter <= 'Z'
                                          ? static_cast<char>(letter - 'A' + 'a')
                                          : letter;
                           });
    return folded;
}

/// @brief Block DOS aliases even when followed by an extension.
bool reserved_device(std::string_view segment)
{
    const auto stem = segment.substr(0, segment.find('.'));
    return stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
           (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
            stem.back() >= '0' && stem.back() <= '9');
}

/// @brief Reject alternate separators, dot segments, ADS and device paths before mounting.
std::string validate_path(std::string_view path)
{
    if (path.empty() || path.size() > 512)
    {
        throw std::invalid_argument("Invalid asset source path length");
    }

    auto folded = fold_ascii(path);
    std::string_view tail = folded;
    while (true)
    {
        const auto slash = tail.find('/');
        const auto segment = tail.substr(0, slash);
        if (!token(segment, 255) || segment.ends_with('.') || reserved_device(segment))
        {
            throw std::invalid_argument("Invalid asset source path: " + std::string{path});
        }

        if (slash == std::string_view::npos)
        {
            return folded;
        }

        tail.remove_prefix(slash + 1);
    }
}

/// @brief Locate a persistent ID in the canonical sorted record array.
auto find_record(const std::vector<asset_record>& records, asset_id id)
{
    return std::ranges::lower_bound(records, id, {},
                                    [](const auto& record)
                                    {
                                        return record.reference.id;
                                    });
}

/// @brief Distinguish absence from a type mismatch without resolving a physical source.
const asset_record& require_record(const std::vector<asset_record>& records, asset_ref reference)
{
    const auto match = find_record(records, reference.id);
    if (match == records.end() || match->reference.id != reference.id)
    {
        throw std::out_of_range("Missing asset: " + format_id(reference.id));
    }

    if (match->reference.type != reference.type)
    {
        throw std::invalid_argument("Asset type mismatch for " + format_id(reference.id) +
                                    ": expected " + std::string{format_type(reference.type)} +
                                    ", found " + std::string{format_type(match->reference.type)});
    }

    return *match;
}

/// @brief Canonicalize per-record edges and reject duplicate IDs or role names.
void canonicalize_links(asset_record& record)
{
    std::ranges::sort(record.dependencies);
    if (std::ranges::adjacent_find(record.dependencies, {}, &asset_ref::id) !=
        record.dependencies.end())
    {
        throw std::invalid_argument("Duplicate asset dependency in " +
                                    format_id(record.reference.id));
    }

    std::ranges::sort(record.products, {}, &derived_product::role);
    std::string_view prior;
    for (const auto& product : record.products)
    {
        if (!token(product.role, 64) || product.role == prior ||
            product.target.id == record.reference.id)
        {
            throw std::invalid_argument("Invalid or duplicate derived product in " +
                                        format_id(record.reference.id));
        }

        prior = product.role;
    }
}

/// @brief Validate namespace ownership, source uniqueness and bounded metadata before graph work.
void canonicalize_records(catalog_document& draft)
{
    (void)format_namespace(draft.space);
    if (!draft.next_local || draft.records.size() > detail::max_records)
    {
        throw std::invalid_argument("Invalid asset catalog watermark or record capacity");
    }

    auto& records = draft.records;
    std::ranges::sort(records, {},
                      [](const auto& record)
                      {
                          return record.reference.id;
                      });
    std::set<std::pair<std::string, std::string>> locations;
    std::uint64_t previous = 0;
    std::size_t links = 0;
    for (auto& record : records)
    {
        const auto id = record.reference.id;
        if (id.space != draft.space || !id.local || id.local >= draft.next_local ||
            id.local == previous)
        {
            throw std::invalid_argument("Foreign, duplicate or unallocated asset identity");
        }

        previous = id.local;
        (void)format_type(record.reference.type);
        if (!record.revision || !record.importer_version || !record.settings_version ||
            !token(record.source.root, 64) || !token(record.importer, 64))
        {
            throw std::invalid_argument("Invalid revision, root or importer in " + format_id(id));
        }

        if (!locations.emplace(record.source.root, validate_path(record.source.path)).second)
        {
            throw std::invalid_argument("Duplicate asset source locator: " + record.source.path);
        }

        if (record.dependencies.size() > detail::max_record_links ||
            record.products.size() > detail::max_record_links - record.dependencies.size())
        {
            throw std::invalid_argument("Asset record link limit exceeded");
        }

        links += record.dependencies.size() + record.products.size();
        if (links > detail::max_catalog_links)
        {
            throw std::invalid_argument("Asset catalog link limit exceeded");
        }

        canonicalize_links(record);
    }
}

/// @brief Validate each edge with its owner in the diagnostic, including missing targets.
const asset_record& require_link(const std::vector<asset_record>& records, asset_ref link,
                                 asset_id owner)
{
    try
    {
        return require_record(records, link);
    }
    catch (const std::logic_error& error)
    {
        throw std::invalid_argument("Asset " + format_id(owner) + ": " + error.what());
    }
}

/// @brief Prove dependency closure with an iterative topological pass, even for deep graphs.
void validate_graph(const std::vector<asset_record>& records)
{
    std::vector<std::size_t> incoming(records.size());
    for (const auto& record : records)
    {
        for (const auto& link : record.dependencies)
        {
            const auto& target = require_link(records, link, record.reference.id);
            ++incoming[static_cast<std::size_t>(&target - records.data())];
        }

        for (const auto& product : record.products)
        {
            (void)require_link(records, product.target, record.reference.id);
        }
    }

    std::vector<std::size_t> ready;
    ready.reserve(records.size());
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        if (!incoming[i])
        {
            ready.push_back(i);
        }
    }

    for (std::size_t i = 0; i < ready.size(); ++i)
    {
        for (const auto& link : records[ready[i]].dependencies)
        {
            const auto index =
                static_cast<std::size_t>(find_record(records, link.id) - records.begin());
            if (--incoming[index] == 0)
            {
                ready.push_back(index);
            }
        }
    }

    if (ready.size() != records.size())
    {
        const auto blocked = std::ranges::find_if(incoming,
                                                  [](auto count)
                                                  {
                                                      return count != 0;
                                                  });
        const auto index = static_cast<std::size_t>(blocked - incoming.begin());
        throw std::invalid_argument("Asset dependency cycle involving " +
                                    format_id(records[index].reference.id));
    }
}
} // namespace

/// @brief Only compilation creates an immutable snapshot.
catalog::catalog(std::shared_ptr<const catalog_document> snapshot) : snapshot_(std::move(snapshot))
{
}

/// @brief Canonicalize and validate all metadata before publishing any snapshot.
catalog catalog::compile(catalog_document draft, const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"catalog.compile", "assets.cpu"}};
    canonicalize_records(draft);
    validate_graph(draft.records);
    return catalog{std::make_shared<const catalog_document>(std::move(draft))};
}

/// @brief Resolve metadata without exposing the authoring locator to a consumer read.
asset_info catalog::resolve(asset_ref reference) const
{
    const auto& record = require_record(snapshot_->records, reference);
    return {record.reference, record.revision, record.byte_size};
}

/// @brief Borrow canonical authoring metadata through the snapshot lifetime.
const catalog_document& catalog::document() const noexcept
{
    return *snapshot_;
}
} // namespace atlas::assets
