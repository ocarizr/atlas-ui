/** @file @brief Shared decoded-byte leases and owner-affine request bookkeeping. */
#pragma once
#include <atlas/ui_images/image_cache.hpp>
#include <atomic>
#include <thread>

namespace atlas::ui_images::detail
{
struct decoded_budget
{
    std::size_t limit;                   ///< Immutable aggregate reservation ceiling.
    std::atomic<std::size_t> current{0}; ///< Pending and retained pixel charges.
    std::atomic<std::size_t> peak{0};    ///< Successful reservation high-water mark.
}; ///< Shared with accepted callbacks and old immutable frames.

class decoded_lease
{
  public:
    /// @brief Atomically reserve decoded capacity before any work is admitted.
    decoded_lease(std::shared_ptr<decoded_budget> budget, std::size_t bytes);
    ///< Reserve with overflow-safe atomic admission before any decoding work.
    ~decoded_lease(); ///< Return the remaining charge only after all holders release it.
    void shrink(std::size_t actual); ///< Worker-only reduction after validated output allocation.
  private:
    std::shared_ptr<decoded_budget> budget_; ///< Shared ledger, never the cache facade.
    std::size_t bytes_ = 0; ///< Mutated only by the decoding owner before publication.
}; ///< One reservation per request, shared rather than copied.

class leased_decoder final : public renderer_content::image_decoder
{
  public:
    leased_decoder(std::shared_ptr<const renderer_content::image_decoder> decoder,
                   std::shared_ptr<decoded_lease> lease)
        : decoder_(std::move(decoder)), lease_(std::move(lease))
    {
    } ///< Callback and immutable pixel aliases both retain the lease.

    /// @brief Decode and retain the validated output through an accounting alias.
    std::shared_ptr<const renderer_content::image_data>
    decode(std::span<const std::uint8_t> encoded, renderer_content::image_limits limits,
           std::shared_ptr<std::pmr::memory_resource> memory, std::stop_token stop) const override;
    ///< Delegate buffer-only decoding on the streamer's ready job, then validate and alias.
  private:
    std::shared_ptr<const renderer_content::image_decoder> decoder_; ///< Reentrant injected codec.
    std::shared_ptr<decoded_lease> lease_; ///< No raw pointer into a mutable owner.
}; ///< No direct storage access or nested worker wait.

struct cache_entry
{
    image_entry value;                                      ///< Owner-visible current state.
    std::optional<renderer_content::image_request> request; ///< Consumed only when ready.
}; ///< Move-only future plus shared immutable pixels.

class image_frame_data
{
  public:
    explicit image_frame_data(std::shared_ptr<std::pmr::memory_resource> resource)
        : memory(std::move(resource)), values(0, memory.get())
    {
    } ///< Potentially throwing Debug proxy construction stays in place.

    std::shared_ptr<std::pmr::memory_resource> memory; ///< Outlives PMR metadata destruction.
    std::pmr::vector<image_entry> values;              ///< Bounded immutable snapshot metadata.
}; ///< Pixel aliases retain their independent reservation lifetimes.
} // namespace atlas::ui_images::detail

namespace atlas::ui_images
{
class image_cache::impl
{
  public:
    impl(streaming::asset_streamer& source,
         std::shared_ptr<const renderer_content::image_decoder> codec, image_cache_limits limits,
         std::shared_ptr<std::pmr::memory_resource> resource,
         diagnostics::services services); ///< Reserve owner entries before admitting any jobs.
    void check() const; ///< Reject worker, foreign-thread and diagnostic callback reentry.
    detail::cache_entry& require(std::uint64_t key); ///< Reject foreign/released identities.
    streaming::asset_streamer& streamer;             ///< Must outlive accepted ready jobs.
    std::shared_ptr<const renderer_content::image_decoder> decoder; ///< Reentrant dependency.
    image_cache_limits limits; ///< Bounded live entries and decoded-byte admission.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Thread-safe pixel/snapshot policy.
    diagnostics::services destinations;                ///< Optional injected diagnostics.
    std::shared_ptr<detail::decoded_budget> budget;    ///< Outlives this facade when necessary.
    std::pmr::vector<detail::cache_entry> entries;     ///< Sorted monotonically increasing keys.
    const std::thread::id owner = std::this_thread::get_id(); ///< Sole mutable coordinator.
    bool busy = false; ///< Protect synchronous diagnostic callbacks.
}; ///< No worker callback borrows this object.
} // namespace atlas::ui_images
