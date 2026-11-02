/** @file @brief Fixed-capacity generational registries and lifetime accounting. */
#pragma once
#include "resource_backend.hpp"
#include <algorithm>
#include <limits>
#include <type_traits>

namespace atlas::renderer
{
/// @brief One fixed slot remains occupied until GPU and descriptor references retire.
struct resource_entry
{
    std::shared_ptr<detail::gpu_resource> allocation; ///< Native owner, null only in a free slot.
    std::uint64_t generation = 1;                     ///< Exhausted generations are never reused.
    std::uint64_t charge = 0;                         ///< Native aligned allocation size.
    std::uint64_t last_use = 0;                       ///< Latest protecting fence.
    std::uint64_t initialization =
        0;                   ///< Latest full-upload fence; later read uses do not undo readiness.
    std::size_t payload = 0; ///< Required full-upload byte count.
    std::uint32_t views = 0; ///< Descriptor slots retaining this texture.
    resource_state state = resource_state::copy_destination; ///< Scheduled state.
    bool texture = false;     ///< Distinguishes buffer and texture handles.
    bool live = false;        ///< Admission-visible lifetime.
    bool initialized = false; ///< A full initialization upload has been submitted.
};

/// @brief SRV identity retains its parent slot until the descriptor's last GPU use completes.
struct descriptor_entry
{
    std::uint64_t generation = 1; ///< Slot identity.
    std::uint64_t last_use = 0;   ///< Fence protecting descriptor reuse.
    std::uint32_t resource = 0;   ///< Stable parent slot while occupied.
    bool occupied = false;        ///< Native descriptor slot is reserved.
    bool live = false;            ///< Public descriptor handle is valid.
};

/// @brief Store state is exclusively owned by the device coordinator.
class resource_store::impl
{
  public:
    /// @brief Reserve fixed registry capacity and retain the selected device, allocator and
    /// diagnostics.
    impl(std::shared_ptr<detail::device_backend> graphics, resource_limits budgets,
         diagnostics::services output)
        : device(std::move(graphics)), backend(detail::make_resource_backend(device, budgets)),
          limits(budgets), destinations(std::move(output)), entries(backend->memory()),
          descriptors(backend->memory())
    {
        entries.resize(limits.max_resources);
        descriptors.resize(limits.max_descriptors);
    }

    ~impl()
    {
        const diagnostics::operation_scope scope{destinations,
                                                 {"resource_store.shutdown", "renderer.cpu_wait"}};
        backend->drain();
        diagnostics::write(destinations, diagnostics::log_level::info, "renderer.resources",
                           "GPU resource store drained");
    } ///< Drain before member destruction releases any allocation.

    /// @brief Validate owner, kind, generation and live state before resolving a slot.
    template <class Tag> resource_entry& get(resource_handle<Tag> handle) const
    {
        device->check_owner();
        constexpr bool texture = std::is_same_v<Tag, texture_tag>;
        if (handle.owner != identity || handle.index >= entries.size())
        {
            throw std::invalid_argument("foreign or invalid resource handle");
        }

        auto& entry = entries[handle.index];
        if (!entry.live || entry.generation != handle.generation || entry.texture != texture)
        {
            throw std::invalid_argument("stale or mismatched resource handle");
        }

        return entry;
    }

    /// @brief Validate owner, kind, generation and live state before resolving a slot.
    descriptor_entry& get(descriptor_handle handle) const
    {
        device->check_owner();
        if (handle.owner != identity || handle.index >= descriptors.size())
        {
            throw std::invalid_argument("foreign descriptor handle");
        }

        auto& entry = descriptors[handle.index];
        if (!entry.live || entry.generation != handle.generation)
        {
            throw std::invalid_argument("stale descriptor handle");
        }

        return entry;
    }

    /// @brief Accept only empty or already-issued completion values from this device.
    std::uint64_t token(completion_token value) const
    {
        device->check_owner();
        if (value.owner == 0 && value.value == 0)
        {
            return 0;
        }

        if (value.owner != device->identity || value.value == 0 || value.value > backend->issued())
        {
            throw std::invalid_argument("foreign or unissued GPU completion token");
        }

        return value.value;
    }

    /// @brief Allocate only after slot and aligned-byte admission checks have succeeded.
    template <class Tag, class Description> resource_handle<Tag> create(Description description)
    {
        device->check_owner();
        description.validate();
        const diagnostics::operation_scope scope{destinations, {"resource.create", "renderer.cpu"}};
        const auto found =
            std::find_if(entries.begin(), entries.end(),
                         [](const auto& entry)
                         {
                             return !entry.allocation &&
                                    entry.generation != std::numeric_limits<std::uint64_t>::max();
                         });
        if (found == entries.end())
        {
            throw std::overflow_error("GPU resource slots exhausted; collect retired work");
        }

        const auto charge = backend->requirements(description);
        if (charge > limits.max_allocation_bytes - allocation_bytes)
        {
            throw std::length_error("GPU allocation budget exhausted");
        }

        auto allocation = backend->create(description);
        found->allocation = std::move(allocation);
        found->charge = charge;
        if constexpr (std::is_same_v<Tag, texture_tag>)
        {
            found->payload = static_cast<std::size_t>(description.width) * description.height * 4;
            found->texture = true;
        }
        else
        {
            found->payload = description.size;
            found->texture = false;
        }

        found->live = true;
        found->initialized = false;
        found->last_use = 0;
        found->views = 0;
        found->state = resource_state::copy_destination;
        allocation_bytes += charge;
        return {identity, static_cast<std::uint32_t>(found - entries.begin()), found->generation};
    }

    /// @brief Copy exact source bytes into retained staging and publish the protecting queue fence.
    template <class Tag>
    completion_token upload(resource_handle<Tag> handle, std::span<const std::byte> bytes)
    {
        auto& entry = get(handle);
        const diagnostics::operation_scope scope{destinations, {"resource.upload", "renderer.cpu"}};
        if (bytes.size() != entry.payload)
        {
            throw std::invalid_argument("upload must initialize the exact full resource payload");
        }

        const auto fence = backend->upload(entry.allocation, entry.state, bytes);
        entry.last_use = fence;
        entry.initialized = true;
        entry.initialization = fence;
        entry.state = resource_state::shader_read;
        return {device->identity, fence};
    }

    /// @brief Schedule an explicit native state change and extend the resource lifetime.
    template <class Tag>
    completion_token transition(resource_handle<Tag> handle, resource_state target)
    {
        auto& entry = get(handle);
        if (target != resource_state::copy_source && target != resource_state::copy_destination &&
            target != resource_state::shader_read)
        {
            throw std::invalid_argument("unsupported resource state");
        }

        const diagnostics::operation_scope scope{destinations,
                                                 {"resource.transition", "renderer.cpu"}};
        const auto fence = backend->transition(entry.allocation, entry.state, target);
        entry.last_use = fence;
        entry.state = target;
        return {device->identity, fence};
    }

    /// @brief Extend the protecting queue value without changing the resource identity.
    template <class Tag> void mark(resource_handle<Tag> handle, completion_token after)
    {
        auto& entry = get(handle);
        entry.last_use = std::max(entry.last_use, token(after));
    }

    /// @brief Invalidate the public identity immediately while retaining storage through GPU
    /// completion.
    template <class Tag> void release(resource_handle<Tag> handle, completion_token after)
    {
        mark(handle, after);
        get(handle).live = false;
    }

    /// @brief Copy initialized GPU data to CPU bytes and restore the scheduled resource state.
    template <class Tag> std::vector<std::byte> readback(resource_handle<Tag> handle)
    {
        auto& entry = get(handle);
        if (!entry.initialized)
        {
            throw std::logic_error("resource must be fully initialized before readback");
        }

        const diagnostics::operation_scope scope{destinations,
                                                 {"resource.readback", "renderer.cpu_wait"}};
        return backend->readback(entry.allocation, entry.state);
    }

    std::shared_ptr<detail::device_backend> device;    ///< Pins queue provenance and native owner.
    std::shared_ptr<detail::resource_backend> backend; ///< Destroyed after registry entries.
    resource_limits limits;                            ///< Fixed capacities and charged byte quota.
    diagnostics::services destinations;                ///< Retained library diagnostics.
    std::uint64_t identity = detail::next_identity();  ///< Store-local handle provenance.
    mutable std::pmr::vector<resource_entry>
        entries; ///< Fixed slots, stable while descriptors reference parents.
    mutable std::pmr::vector<descriptor_entry> descriptors; ///< Fixed SRV slots.
    std::uint64_t allocation_bytes = 0; ///< Includes retired GPU allocation ranges.
};
} // namespace atlas::renderer
