/**
 * @file
 * @brief Nonblocking UI image acquisition and allocator-retaining immutable frame metadata.
 */
#pragma once
#include <atlas/renderer_content/loader.hpp>
#include <atlas/ui/image_fit.hpp>

namespace atlas::ui_images
{
/// @brief CPU acquisition state, separate from GPU readiness.
enum class image_status : std::uint8_t
{
    pending,  ///< Storage or ready-job decode has not completed.
    decoded,  ///< Valid immutable pixels and intrinsic dimensions are available.
    failed,   ///< Original acquisition/decode exception is retained.
    cancelled ///< Explicit cancellation suppresses publication, even after decode completion.
}; ///< CPU preparation state; GPU readiness is a separate coordinator concern.

struct image_entry
{
    std::uint64_t key = 0; ///< Opaque process-unique positive paint texture key.
    image_status status = image_status::pending; ///< Captured state at this boundary.
    std::shared_ptr<const renderer_content::image_data> pixels; ///< Budget-charged shared storage.
    std::exception_ptr error; ///< Set only for failed acquisition/preparation.
}; ///< Immutable snapshots copy metadata, never decoded pixel buffers.

namespace detail
{
class image_frame_data; ///< Shared PMR storage with no live cache or native ownership.
}

class image_frame
{
  public:
    image_frame() = default;                                   ///< Empty frame.
    std::span<const image_entry> entries() const noexcept;     ///< Stable snapshot values.
    const image_entry* find(std::uint64_t key) const noexcept; ///< Null for absent keys.
  private:
    friend class image_cache; ///< Only the owner publishes complete metadata.
    std::shared_ptr<const detail::image_frame_data> data_; ///< Retain allocation policy.
}; ///< Safe synchronized main/render handoff; old frames retain their own decoded leases.

struct image_cache_limits
{
    std::size_t max_images = 64; ///< Live request entries, at most 4096.
    std::size_t max_decoded_bytes =
        64ULL * 1024 * 1024; ///< Pending reservations plus retained pixels.
}; ///< Encoded bytes and codec scratch remain governed by streamer/decoder contracts.

struct image_cache_statistics
{
    std::size_t images = 0;         ///< Current owner-visible request entries.
    std::size_t reserved_bytes = 0; ///< Includes accepted jobs and old snapshots after release.
    std::size_t peak_reserved_bytes = 0; ///< Requested-byte high-water mark, not process RSS.
}; ///< The budget ledger outlives the cache while accepted work/frames retain leases.

class image_cache
{
  public:
    image_cache(streaming::asset_streamer& streamer,
                std::shared_ptr<const renderer_content::image_decoder> decoder,
                image_cache_limits limits = {},
                std::shared_ptr<std::pmr::memory_resource> memory = {},
                diagnostics::services destinations = {}); ///< Reuse streamer ready jobs.
    ~image_cache(); ///< Cancel accepted requests without worker waits; streamer must drain later.
    image_cache(const image_cache&) = delete; ///< One coordinator owns mutable request state.
    image_cache& operator=(const image_cache&) = delete; ///< Never duplicate lifecycle ownership.
    /// @brief Reserve a decoded ceiling before admission and return a distinct identity.
    std::uint64_t request(streaming::read_request source,
                          renderer_content::image_limits limits = {4096, 16ULL * 1024 * 1024});
    ///< Reserve the per-image ceiling before admission; repeated requests are explicit identities.
    void poll(); ///< Consume only ready completions; original failures become observable state.
    void cancel(std::uint64_t key); ///< Suppress publication and request real storage cancellation.
    void release(std::uint64_t key); ///< Cancel/remove; retained frames/jobs keep budget charges.
    image_frame snapshot() const;    ///< Capture current states without implicitly polling.
    image_cache_statistics statistics() const; ///< Owner-thread ledger observation.
  private:
    class impl; ///< Checked owner, bounded entries and shared decoded-byte accounting.
    std::unique_ptr<impl> impl_; ///< No file access, private scheduler or GPU objects.
}; ///< Explicit cache lifetime/admission; no automatic eviction of caller-owned keys.

/// @brief Resolve decoded dimensions or an explicit placeholder size.
ui::layout_extent intrinsic_size(const image_entry& image,
                                 ui::layout_extent placeholder = {32, 32});
///< Use decoded dimensions or an explicit finite nonnegative placeholder size.
} // namespace atlas::ui_images
