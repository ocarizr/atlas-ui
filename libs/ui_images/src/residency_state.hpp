/** @file @brief GPU-coordinator upload ownership and bounded resident accounting. */
#pragma once
#include <atlas/renderer_content/gpu_content.hpp>
#include <atlas/ui_images/image_renderer.hpp>
#include <thread>

namespace atlas::ui_images
{
struct resident_image
{
    std::uint64_t key;                                        ///< Stable CPU image identity.
    std::unique_ptr<renderer_content::texture_upload> upload; ///< Retain through upload/last draw.
    std::size_t bytes = 0;               ///< Charged logical RGBA payload until final retirement.
    renderer::completion_token last_use; ///< Last submitted frame that referenced this view.
    bool wanted = false;                 ///< Decoded and referenced by the latest paint packet.
    bool failed = false; ///< Permanent upload failure; a new request key permits retry.
    renderer::descriptor_handle view{}; ///< Published once the upload completes.
}; ///< No native SDK types and no worker-owned graphics lifetime.

class image_renderer::impl
{
  public:
    /// @brief Reserve metadata and initialize a bounded placeholder.
    impl(renderer::resource_store& resources, renderer::pass_renderer& passes,
         residency_limits limits, std::shared_ptr<std::pmr::memory_resource> memory,
         diagnostics::services destinations);
    ///< Reserve metadata and initialize the bounded fallback before accepting submissions.
    void check() const; ///< Thread check precedes mutable-state access.
    void collect();     ///< Retire only after both upload and last-use fences complete.
    void prepare(const frame_payload& frame); ///< Resolve desired keys and admit bounded uploads.
    resident_image* find(std::uint64_t key);  ///< Null if no active/retiring upload exists.
    renderer::resource_store& resources;      ///< Borrowed GPU-coordinator service.
    residency_limits limits;                  ///< Explicit logical budgets.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< CPU preparation allocation policy.
    diagnostics::services destinations;                ///< Optional shared diagnostics.
    ui_renderer::paint_renderer painter; ///< Generic adjacent batching and safe GPU upload.
    std::unique_ptr<renderer_content::texture_upload> fallback; ///< Constant four-byte gray pixel.
    std::pmr::vector<resident_image> entries; ///< Includes retiring and failed entries.
    std::pmr::vector<std::uint64_t> wanted; ///< Unique nonzero keys from the current paint packet.
    residency_statistics stats; ///< Logical accounting, separate from native allocation charge.
    const std::thread::id owner = std::this_thread::get_id(); ///< Sole GPU coordinator.
    bool busy = false; ///< Synchronous diagnostics cannot reenter mutation.
}; ///< No storage, decoder or pool ownership.
} // namespace atlas::ui_images
