/**
 * @file
 * @brief Ordered UI packets adapted to the generic renderer on its coordinator thread.
 */
#pragma once
#include <atlas/render_host/render_loop.hpp>
#include <atlas/renderer/passes.hpp>
#include <atlas/ui/paint_engine.hpp>

namespace atlas::ui_renderer
{
class frame_payload final : public render_host::frame_payload
{
  public:
    explicit frame_payload(ui::paint_snapshot value) : paint(std::move(value))
    {
    } ///< Retain immutable CPU geometry across the main/render handoff.

    const ui::paint_snapshot paint; ///< Owns no thread-affine graphics objects.
}; ///< Both handoff threads may safely destroy this packet.

struct texture_binding
{
    std::uint64_t key;                ///< Positive application-assigned key from paint_style.
    renderer::descriptor_handle view; ///< Resident store-local SRV; never loaded here.
}; ///< Consumer owns texture/view lifetime until submission; renderer then fence-retains it.

class paint_renderer
{
  public:
    paint_renderer(renderer::resource_store& resources, renderer::pass_renderer& passes,
                   std::shared_ptr<std::pmr::memory_resource> memory = {},
                   diagnostics::services destinations = {}); ///< Create two reusable pipelines.
    ~paint_renderer(); ///< Release pipeline handles on the creating GPU coordinator.
    paint_renderer(const paint_renderer&) = delete;            ///< Unique pipeline ownership.
    paint_renderer& operator=(const paint_renderer&) = delete; ///< Never duplicate release.
    /// @brief Validate resident keys, batch adjacent compatible draws, upload and submit.
    renderer::completion_token submit(const ui::paint_snapshot& snapshot,
                                      renderer::texture_handle target,
                                      renderer::surface_extent extent,
                                      std::span<const texture_binding> textures = {},
                                      renderer::recording_executor* recorder = nullptr);
    ///< Validate all texture keys before upload, batch adjacent compatible draws, then submit.
    std::size_t last_draws() const; ///< Successful submitted batches, not source quad count.
  private:
    class impl;                  ///< Borrowed services plus thread-affine pipeline identities.
    std::unique_ptr<impl> impl_; ///< No storage reads or private worker pool.
}; ///< Rectangular masks use outward-rounded physical scissor boundaries.
} // namespace atlas::ui_renderer
