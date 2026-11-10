/** @file @brief Resolve trusted source-root configuration at the storage boundary. */
#include "mount_file.hpp"
#include <algorithm>
#include <atlas/streaming/catalog_streamer.hpp>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace atlas::streaming
{
/// @brief Query readable file length without copying any asset bytes.
std::optional<std::uint64_t> detail::mount_file_extent(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    const auto end = file.tellg();
    if (end < 0)
    {
        return std::nullopt;
    }

    return static_cast<std::uint64_t>(end);
}

namespace
{
/// @brief Validate and canonicalize configured roots independently of process working directory.
auto authorized_roots(std::span<const source_root> roots)
{
    std::map<std::string, std::filesystem::path, std::less<>> directories;
    if (roots.size() > 64)
    {
        throw std::invalid_argument("Asset mount root limit exceeded");
    }

    for (const auto& root : roots)
    {
        if (root.key.empty() || root.key.size() > 64 || !root.directory.is_absolute())
        {
            throw std::invalid_argument("Invalid authorized asset root: " + root.key);
        }

        std::error_code error;
        auto directory = std::filesystem::canonical(root.directory, error);
        if (error || !std::filesystem::is_directory(directory, error) || error ||
            !directories.emplace(root.key, std::move(directory)).second)
        {
            throw std::invalid_argument("Missing or duplicate authorized asset root: " + root.key);
        }
    }

    return directories;
}

/// @brief Resolve one logical source under its authorized physical root before registration.
std::filesystem::path resolve_loose(const std::filesystem::path& root,
                                    const assets::asset_record& record)
{
    std::error_code error;
    auto path = std::filesystem::canonical(root / record.source.path, error);
    const auto [parent, child] = std::mismatch(root.begin(), root.end(), path.begin(), path.end());
    if (error || parent != root.end() || child == path.end() ||
        !std::filesystem::is_regular_file(path, error) || error)
    {
        throw std::invalid_argument("Missing or unauthorized loose asset " +
                                    assets::format_id(record.reference.id) + ": " +
                                    record.source.path);
    }

    if (detail::mount_file_extent(path) != record.byte_size)
    {
        throw std::invalid_argument("Loose asset length differs from catalog metadata: " +
                                    assets::format_id(record.reference.id));
    }

    return path;
}
} // namespace

/// @brief Build a complete private mount candidate before touching the native I/O worker.
storage_mount prepare_loose_mount(const assets::catalog& catalog,
                                  std::span<const source_root> roots,
                                  const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"catalog.prepare_loose", "streaming.setup"}};
    const auto directories = authorized_roots(roots);
    std::set<std::filesystem::path> files;
    storage_mount result;
    const auto& records = catalog.document().records;
    result.sources.reserve(records.size());
    result.routes.reserve(records.size());
    for (const auto& record : records)
    {
        const auto root = directories.find(record.source.root);
        const auto key = assets::format_id(record.reference.id);
        if (root == directories.end())
        {
            throw std::invalid_argument("Unauthorized asset root " + record.source.root + " for " +
                                        key);
        }

        auto path = resolve_loose(root->second, record);
        if (!files.insert(path).second)
        {
            throw std::invalid_argument(
                "Multiple catalog entries resolve to the same loose source: " + key);
        }

        result.sources.emplace(key, loose_file_asset{std::move(path)});
        result.routes.push_back({record.reference, record.revision, key, 0});
    }

    return result;
}
} // namespace atlas::streaming
