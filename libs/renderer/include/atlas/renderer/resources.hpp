/**
 * @file
 * @brief Typed GPU resources, bounded transfers and fence-based retirement.
 */
#pragma once
#include <atlas/renderer/renderer.hpp>
#include <cstddef>
#include <span>

namespace atlas::renderer
{
/// @brief Distinct compile-time identity for GPU buffers.
struct buffer_tag;
/// @brief Distinct compile-time identity for GPU textures.
struct texture_tag;
/// @brief Distinct compile-time identity for shader-resource descriptors.
struct descriptor_tag;

/// @brief Opaque store-local identity; stale generations and foreign owners are rejected.
template <class Tag> struct resource_handle
{
    std::uint64_t owner = 0;      ///< Resource-store provenance.
    std::uint32_t index = 0;      ///< Internal slot, not a native descriptor address.
    std::uint64_t generation = 0; ///< Slot lifetime, advanced only after safe reclamation.
    bool operator==(const resource_handle&) const = default; ///< Compare complete identity.
};

using buffer_handle = resource_handle<buffer_tag>;         ///< Typed GPU buffer identity.
using texture_handle = resource_handle<texture_tag>;       ///< Typed 2D texture identity.
using descriptor_handle = resource_handle<descriptor_tag>; ///< Typed texture SRV identity.

/// @brief Device-wide queue completion value; unrelated to CPU job completion.
struct completion_token
{
    std::uint64_t owner = 0; ///< Creating graphics-device identity.
    std::uint64_t value = 0; ///< Monotonic direct-queue fence value.
};
/// @brief Explicit scheduled resource state on the single queue.
enum class resource_state : std::uint8_t
{
    /// @brief Resource is scheduled as an upload destination.
    copy_destination,
    /// @brief Resource is scheduled for generic buffer or texture shader reads.
    shader_read,
    /// @brief Resource is scheduled as a copy source.
    copy_source
};

/// @brief Byte-addressed default-heap GPU buffer, fully initialized by one exact upload.
struct buffer_description
{
    std::size_t size = 0;  ///< Requested byte count, excluding native allocation alignment.
    void validate() const; ///< Reject empty resources.
};

/// @brief Single-mip RGBA8 UNORM 2D texture; other formats follow in later phases.
struct texture_description
{
    std::uint32_t width = 0;  ///< Pixel width in [1, 16384].
    std::uint32_t height = 0; ///< Pixel height in [1, 16384].
    void validate() const;    ///< Reject empty/oversized extents.
};

/// @brief Hard admission limits include retired allocations and in-flight transfer staging.
struct resource_limits
{
    std::uint32_t max_resources = 256;   ///< Shared buffer/texture slots.
    std::uint32_t max_descriptors = 256; ///< Shader-visible texture SRV slots.
    std::uint32_t max_transfers = 8;     ///< Pending copy/transition command batches.
    std::uint64_t max_allocation_bytes =
        256ULL * 1024 * 1024; ///< Sum of aligned resource allocation requirements.
    std::uint64_t max_staging_bytes =
        16ULL * 1024 * 1024; ///< Sum of in-flight upload footprint bytes.
    void validate() const;   ///< Reject empty capacities and unrepresentable configurations.
};

/// @brief Snapshot separates logical payload, charged allocation ranges and allocator-reserved
/// heaps.
struct resource_statistics
{
    std::size_t live_resources = 0;      ///< Live buffer/texture handles.
    std::size_t retired_resources = 0;   ///< Invalidated handles still retaining allocations.
    std::size_t live_descriptors = 0;    ///< Live SRV handles.
    std::size_t retired_descriptors = 0; ///< SRV slots waiting for GPU completion.
    std::uint64_t allocation_bytes =
        0; ///< Charged aligned allocation ranges, including retired resources.
    std::uint64_t staging_bytes = 0;   ///< Pending upload footprints.
    std::size_t pending_transfers = 0; ///< Retained command allocators/lists and optional staging.
    std::uint64_t reserved_heap_bytes =
        0; ///< Device-wide D3D12MA heap reservation; not the admission quota.
};

/// @brief SDK-free resource registry; all calls/destruction belong to the device coordinator.
class resource_store
{
  public:
    explicit resource_store(device& graphics,
                            resource_limits limits = {}); ///< Inherit PMR and diagnostics; retain
                                                          ///< native device lifetime.
    ~resource_store(); ///< Drain before releasing any live or retired allocations/descriptors.
    resource_store(const resource_store&) = delete; ///< One coordinator owns each registry.
    resource_store& operator=(const resource_store&) = delete; ///< Registry identity is unique.
    buffer_handle
    create_buffer(buffer_description description); ///< Allocate a fresh uninitialized buffer.
    texture_handle create_texture(
        texture_description description); ///< Allocate a fresh uninitialized RGBA texture.
    descriptor_handle
    create_view(texture_handle texture); ///< Allocate a texture SRV retaining its resource.
    completion_token upload(
        buffer_handle buffer,
        std::span<const std::byte> bytes); ///< Snapshot an exact payload into retained GPU staging.
    completion_token upload(texture_handle texture,
                            std::span<const std::byte>
                                rgba); ///< Pack exact tight RGBA rows using native copy footprints.
    completion_token
    transition(buffer_handle buffer,
               resource_state target); ///< Schedule an explicit resource-state transition.
    completion_token
    transition(texture_handle texture,
               resource_state target); ///< Schedule an explicit resource-state transition.
    void mark_used(buffer_handle buffer,
                   completion_token after); ///< Extend lifetime through a known device submission.
    void mark_used(texture_handle texture,
                   completion_token after); ///< Extend lifetime through a known device submission.
    void mark_used(descriptor_handle descriptor,
                   completion_token after); ///< Extend both view and underlying texture lifetime.
    void release(buffer_handle buffer,
                 completion_token after =
                     {}); ///< Invalidate immediately; reclaim only after GPU completion.
    void
    release(texture_handle texture,
            completion_token after = {}); ///< Views keep retired texture slots/allocations alive.
    void release(descriptor_handle descriptor,
                 completion_token after = {}); ///< Invalidate immediately; defer descriptor reuse.
    bool ready(buffer_handle buffer) const; ///< Whether a full initialization upload has completed.
    bool
    ready(texture_handle texture) const; ///< Whether a full initialization upload has completed.
    resource_state
    state(buffer_handle buffer) const; ///< Query scheduled state, not GPU completion.
    resource_state
    state(texture_handle texture) const;          ///< Query scheduled state, not GPU completion.
    bool completed(completion_token token) const; ///< Nonblocking fence observation.
    void
    wait(completion_token token); ///< Bounded coordinator-only wait, then reclaim completed work.
    void collect(); ///< Reclaim completed staging, retired views and unreferenced retired resources
                    ///< without waiting.
    std::vector<std::byte> readback(
        buffer_handle buffer); ///< Blocking diagnostic exact-byte readback of initialized data.
    std::vector<std::byte>
    readback(texture_handle texture); ///< Blocking diagnostic tight RGBA readback.
    resource_statistics
    statistics() const; ///< Observe bounded occupancy and native heap reservation.
  private:
    class impl;                  ///< Private registry and selected transfer backend.
    std::unique_ptr<impl> impl_; ///< Owned coordinator state.
};
} // namespace atlas::renderer
