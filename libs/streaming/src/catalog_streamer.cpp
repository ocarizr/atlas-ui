/** @file @brief Validate mount routes and forward stamped typed reads to the existing streamer. */
#include "mount_file.hpp"
#include <atlas/streaming/catalog_streamer.hpp>
#include <atomic>
#include <limits>
#include <map>
#include <stdexcept>

namespace atlas::streaming
{
/// @brief Take ownership of the caller's prepared storage data.
storage_mount::storage_mount(asset_catalog backing, std::vector<asset_route> entries)
    : sources{std::move(backing)}, routes{std::move(entries)}
{
}

namespace
{
/// @brief Never reuse a runtime mount identity, even when content metadata is unchanged.
std::uint64_t next_mount_generation()
{
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load(std::memory_order_relaxed);
    do
    {
        if (value == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("Asset mount generation exhausted");
        }
    } while (!next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed));

    return value;
}

/// @brief Validate the registered backing once, before starting asynchronous acquisition.
std::uint64_t backing_extent(const asset_source& source, std::string_view key)
{
    if (const auto* memory = std::get_if<memory_asset>(&source))
    {
        if (!memory->bytes)
        {
            throw std::invalid_argument("Null memory backing for " + std::string{key});
        }

        return memory->bytes->size();
    }

    const auto& path = std::get<loose_file_asset>(source).path;
    std::error_code error;
    if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error) || error)
    {
        throw std::invalid_argument("Missing or unauthorized file backing for " + std::string{key});
    }

    const auto size = detail::mount_file_extent(path);
    if (!size)
    {
        throw std::invalid_argument("Cannot read file backing length for " + std::string{key});
    }

    return *size;
}

/// @brief A complete prepared mount has one checked route per record and bounded backing metadata.
auto validate_routes(const assets::catalog& catalog, storage_mount& mount)
{
    if (mount.routes.size() != catalog.document().records.size() || mount.sources.size() > 4096)
    {
        throw std::invalid_argument("Incomplete or oversized asset mount");
    }

    std::map<assets::asset_id, asset_route> routes;
    std::map<std::string, std::uint64_t, std::less<>> extents;
    for (auto& route : mount.routes)
    {
        const auto info = catalog.resolve(route.reference);
        const auto id = route.reference.id;
        const auto source = mount.sources.find(route.backing);
        if (info.revision != route.revision || source == mount.sources.end() ||
            route.backing.empty() || route.backing.size() > 128 ||
            route.backing.find('\0') != std::string::npos)
        {
            throw std::invalid_argument("Stale or missing asset route: " + assets::format_id(id));
        }

        auto extent = extents.find(route.backing);
        if (extent == extents.end())
        {
            extent =
                extents.emplace(route.backing, backing_extent(source->second, route.backing)).first;
        }

        if (route.offset > extent->second || info.byte_size > extent->second - route.offset)
        {
            throw std::out_of_range("Asset route exceeds backing extent: " + assets::format_id(id));
        }

        if (!routes.emplace(id, std::move(route)).second)
        {
            throw std::invalid_argument("Duplicate asset route: " + assets::format_id(id));
        }
    }

    if (extents.size() != mount.sources.size())
    {
        throw std::invalid_argument("Unused backing source in asset mount");
    }

    return routes;
}
} // namespace

/// @brief Immutable route data is fully checked before the storage owner is constructed.
struct catalog_streamer::impl
{
    /// @brief Construct the reader only after validating every mounted logical range.
    impl(jobs::job_system& jobs, const assets::catalog& catalog, storage_mount&& mount,
         streamer_limits limits, std::shared_ptr<std::pmr::memory_resource> buffers,
         const diagnostics::services& sinks)
        : metadata{catalog}, routes{validate_routes(catalog, mount)},
          generation{next_mount_generation()},
          reader{jobs, std::move(mount.sources), limits, std::move(buffers), sinks}
    {
    }

    const assets::catalog metadata; ///< Immutable authoring snapshot kept through the mount.
    const std::map<assets::asset_id, asset_route> routes; ///< Validated raw backing extents.
    const std::uint64_t generation; ///< Unique process-local publication identity.
    std::atomic_bool closed{};      ///< Shutdown retires the publication identity.
    asset_streamer reader; ///< Existing bounded memory/IOCP backend and coordinator lifecycle.
};

/// @brief Initialize one mount without changing the string-keyed streamer API.
catalog_streamer::catalog_streamer(jobs::job_system& jobs, const assets::catalog& catalog,
                                   storage_mount storage, streamer_limits limits,
                                   std::shared_ptr<std::pmr::memory_resource> buffers,
                                   const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"catalog.mount", "streaming.setup"}};
    impl_ = std::make_unique<impl>(jobs, catalog, std::move(storage), limits, std::move(buffers),
                                   sinks);
}

/// @brief The storage worker drains before the immutable route metadata is destroyed.
catalog_streamer::~catalog_streamer() = default;

/// @brief Return only metadata needed by the typed consumer.
assets::asset_info catalog_streamer::describe(assets::asset_ref reference) const
{
    return impl_->metadata.resolve(reference);
}

/// @brief Check the logical extent before translating to the raw backing offset.
std::future<void> catalog_streamer::read(typed_read_request request,
                                         typed_read_completion completion, read_options options)
{
    const auto info = describe(request.reference);
    if (!completion || (request.expected_revision && request.expected_revision != info.revision))
    {
        throw std::invalid_argument("Missing typed read callback or stale revision: " +
                                    assets::format_id(request.reference.id));
    }

    if (request.offset > info.byte_size || request.size > info.byte_size - request.offset)
    {
        throw std::out_of_range("Typed read exceeds logical asset extent: " +
                                assets::format_id(request.reference.id));
    }

    const auto& route = impl_->routes.at(request.reference.id);
    const read_stamp stamp{info.reference, info.revision, impl_->generation};
    return impl_->reader.read(
        {route.backing, route.offset + request.offset, request.size},
        [stamp, complete = std::move(completion)](read_result result)
        {
            complete(stamp, std::move(result));
        },
        std::move(options));
}

/// @brief The current owner rejects completions from any retired mount or revision.
bool catalog_streamer::accepts(read_stamp stamp) const noexcept
{
    if (impl_->closed.load() || stamp.mount_generation != impl_->generation)
    {
        return false;
    }

    const auto route = impl_->routes.find(stamp.reference.id);
    return route != impl_->routes.end() && route->second.reference == stamp.reference &&
           route->second.revision == stamp.revision;
}

/// @brief Retire publication after the owner successfully drains the native reader.
void catalog_streamer::shutdown()
{
    impl_->reader.shutdown();
    impl_->closed.store(true);
}
} // namespace atlas::streaming
