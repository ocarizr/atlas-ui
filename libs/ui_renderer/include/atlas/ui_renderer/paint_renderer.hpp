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

/// @brief Borrow an ordered list combining regular UI geometry and externally prepared quads.
struct paint_list
{
    ui::layout_rect viewport;              ///< Logical projection and clip extent.
    std::span<const ui::paint_quad> quads; ///< Back-to-front painter order.
    float cosmetic_phase = 0;              ///< Shared loop phase supplied by the owner in [0, 1].
};

/// @brief Prepare one trusted pixel program for a specific material ABI and runtime key.
struct material_shader
{
    std::uint64_t key;      ///< Nonzero key copied into paint packets.
    ui::material_kind kind; ///< Select the fixed shape or texture parameter layout.
    std::string_view hlsl;  ///< Borrowed bytes defining atlas_material(float4, float2).
};

class paint_renderer
{
  public:
    /// @brief Create reusable pipelines for the selected attachment format.
    paint_renderer(renderer::resource_store& resources, renderer::pass_renderer& passes,
                   std::shared_ptr<std::pmr::memory_resource> memory = {},
                   diagnostics::services destinations = {},
                   renderer::texture_format color_format = renderer::texture_format::rgba8_unorm);
    ~paint_renderer(); ///< Release pipeline handles on the creating GPU coordinator.
    paint_renderer(const paint_renderer&) = delete;            ///< Unique pipeline ownership.
    paint_renderer& operator=(const paint_renderer&) = delete; ///< Never duplicate release.
    void register_shader(material_shader source); ///< Validate and publish a fresh immutable key.
    void release_shader(std::uint64_t key); ///< Invalidate the key and fence-retire its pipeline.
    /// @brief Validate resident keys, batch adjacent compatible draws, upload and submit.
    renderer::completion_token submit(const ui::paint_snapshot& snapshot,
                                      renderer::texture_handle target,
                                      renderer::surface_extent extent,
                                      std::span<const texture_binding> textures = {},
                                      renderer::recording_executor* recorder = nullptr);
    /// @brief Submit custom geometry through the same ordered quad and fence path.
    renderer::completion_token submit_quads(paint_list list, renderer::texture_handle target,
                                            renderer::surface_extent extent,
                                            std::span<const texture_binding> textures = {},
                                            renderer::recording_executor* recorder = nullptr);
    std::size_t last_draws() const;        ///< Successful submitted batches, not source quad count.
    std::size_t last_vertex_bytes() const; ///< Vertex bytes uploaded by the last successful submit.
  private:
    class impl;                  ///< Borrowed services plus thread-affine pipeline identities.
    std::unique_ptr<impl> impl_; ///< No storage reads or private worker pool.
}; ///< Rectangular masks use outward-rounded physical scissor boundaries.
} // namespace atlas::ui_renderer
