/**
 * @file
 * @brief GPU-coordinator image residency and automatic fence accounting for UI submissions.
 */
#pragma once
#include <atlas/ui_images/image_cache.hpp>
#include <atlas/ui_renderer/paint_renderer.hpp>

namespace atlas::ui_images
{
class frame_payload final : public render_host::frame_payload
{
  public:
    frame_payload(ui::paint_snapshot paint, image_frame images)
        : paint(std::move(paint)), images(std::move(images))
    {
    } ///< Retain immutable CPU values and decoded leases through render consumption.

    const ui::paint_snapshot paint; ///< Ordered geometry.
    const image_frame images;       ///< Image states corresponding to the submitted UI.
}; ///< No native objects are destroyed on the handoff thread.

struct residency_limits
{
    std::size_t max_images = 64; ///< Active plus retiring image slots; at most 4096.
    std::size_t max_resident_bytes =
        64ULL * 1024 * 1024;               ///< Logical RGBA payload, including fallback.
    std::size_t max_uploads_per_frame = 4; ///< Bound upload admission on the GPU coordinator.
}; ///< Native aligned allocations and staging have independent resource_store budgets.

struct residency_statistics
{
    std::size_t images = 0;         ///< Active and retiring upload owners.
    std::size_t resident_bytes = 0; ///< Charged logical payload, including in-flight retirement.
    std::size_t peak_resident_bytes = 0; ///< Highest logical payload charge.
    std::size_t deferred = 0;            ///< Current frame images waiting for quota/admission.
    std::size_t failed = 0;       ///< Current retained upload failures, rendered with the fallback.
    std::size_t drawn_images = 0; ///< Resident image bindings used by the last successful submit.
}; ///< Never a GPU heap-reservation or process-memory measurement.

class image_renderer
{
  public:
    image_renderer(renderer::resource_store& resources, renderer::pass_renderer& passes,
                   residency_limits limits = {},
                   std::shared_ptr<std::pmr::memory_resource> memory = {},
                   diagnostics::services destinations = {}); ///< Initialize one tiny gray fallback.
    ~image_renderer(); ///< Retire handles on their owning coordinator; borrowed services outlive
                       ///< it.
    image_renderer(const image_renderer&) = delete; ///< One owner tracks last-use fences.
    image_renderer& operator=(const image_renderer&) = delete; ///< Never alias residency mutation.
    void collect(); ///< Nonblocking upload polling and fence-safe release of retired entries.
    bool ready();   ///< Whether the fallback is initialized; no CPU worker or GPU fence wait.
    /// @brief Resolve image states and record every successfully submitted last-use fence.
    renderer::completion_token submit(const frame_payload& frame, renderer::texture_handle target,
                                      renderer::surface_extent extent,
                                      renderer::recording_executor* recorder = nullptr);
    ///< Resolve requested paint keys, admit bounded uploads and automatically record last use.
    residency_statistics statistics() const; ///< Owner-only state, excluding external resources.
  private:
    class impl;                  ///< Active/retiring uploads and generic paint adapter.
    std::unique_ptr<impl> impl_; ///< No storage reads or private pool.
}; ///< Missing, pending, failed, cancelled and deferred images render the resident fallback.
} // namespace atlas::ui_images
