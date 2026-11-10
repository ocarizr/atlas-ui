/**
 * @file
 * @brief Typed catalog mounts and generation-stamped reads over the existing streamer.
 */
#pragma once
#include <atlas/assets/catalog.hpp>
#include <atlas/streaming/asset_streamer.hpp>
#include <span>

namespace atlas::streaming
{
/// @brief Trusted configuration binding for a catalog's logical source root.
struct source_root
{
    std::string key;                 ///< Case-sensitive catalog root key.
    std::filesystem::path directory; ///< Existing absolute directory authorized by the host.
};

/// @brief Route a logical asset revision to a raw extent of a registered backing source.
struct asset_route
{
    assets::asset_ref reference; ///< Exact typed asset reference.
    std::uint64_t revision{};    ///< Must match the immutable catalog record.
    std::string backing;         ///< Logical key in this mount's low-level streamer registrations.
    std::uint64_t offset{};      ///< Backing-relative start of the catalog's logical extent.
};

/// @brief Candidate mount data built by loose-source or package bootstrap infrastructure.
struct storage_mount
{
    storage_mount() = default; ///< Start with no backing sources or routes.
    storage_mount(asset_catalog backing, std::vector<asset_route> entries); ///< Own prepared data.
    storage_mount(const storage_mount&) = default;            ///< Copy candidate metadata.
    storage_mount(storage_mount&&) noexcept(false) = default; ///< Transfer candidate metadata.
    storage_mount& operator=(const storage_mount&) = default; ///< Copy candidate metadata.
    storage_mount&
    operator=(storage_mount&&) noexcept(false) = default; ///< Transfer candidate data.

    asset_catalog sources;           ///< Authorized loose sources or pinned immutable memory.
    std::vector<asset_route> routes; ///< Exactly one route per catalog record.
};

/// @brief Validate canonical containment and logical lengths before registering loose files.
[[nodiscard]] storage_mount prepare_loose_mount(const assets::catalog& catalog,
                                                std::span<const source_root> roots,
                                                const diagnostics::services& sinks = {});

/// @brief Exact range relative to a typed asset, independent of physical backing and offset.
struct typed_read_request
{
    assets::asset_ref reference;       ///< Identity and expected type.
    std::uint64_t offset{};            ///< Asset-relative byte offset.
    std::size_t size{};                ///< Requested byte count.
    std::uint64_t expected_revision{}; ///< Zero accepts the mounted revision.
};

/// @brief Process-local publication token; never serialize it into authoring data.
struct read_stamp
{
    assets::asset_ref reference;      ///< Exact mounted asset.
    std::uint64_t revision{};         ///< Mounted authored content revision.
    std::uint64_t mount_generation{}; ///< Distinguishes replacement mounts even at equal revision.
};

/// @brief Ready callback owns its result and carries the generation to the publication owner.
using typed_read_completion = std::function<void(read_stamp, read_result)>;

/// @brief Immutable validated mapping and existing asynchronous storage ownership for one mount.
class catalog_streamer
{
  public:
    /// @brief Validate the entire prepared mount before starting the storage worker.
    catalog_streamer(jobs::job_system& jobs, const assets::catalog& catalog, storage_mount storage,
                     streamer_limits limits = {},
                     std::shared_ptr<std::pmr::memory_resource> buffers = {},
                     const diagnostics::services& sinks = {});
    ~catalog_streamer(); ///< Drain on the creating coordinator before releasing the job pool.
    catalog_streamer(const catalog_streamer&) = delete;            ///< One storage owner.
    catalog_streamer& operator=(const catalog_streamer&) = delete; ///< Replace mounts explicitly.

    /// @brief Resolve typed metadata without exposing a path or a backing key.
    [[nodiscard]] assets::asset_info describe(assets::asset_ref reference) const;
    /// @brief Validate logical range and revision before admitting the underlying read.
    std::future<void> read(typed_read_request request, typed_read_completion completion,
                           read_options options = {});
    /// @brief Check the stamp against the current mount on the resource publication owner.
    [[nodiscard]] bool accepts(read_stamp stamp) const noexcept;
    /// @brief Stop admission and drain accepted callbacks on the coordinator.
    void shutdown();

  private:
    struct impl;                 ///< Validated immutable routes and native storage owner.
    std::unique_ptr<impl> impl_; ///< Private mount implementation.
};
} // namespace atlas::streaming
