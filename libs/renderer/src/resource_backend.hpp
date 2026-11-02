/** @file @brief SDK-free GPU allocation and transfer adapter selected by platform. */
#pragma once
#include "renderer_backend.hpp"
#include <atlas/renderer/resources.hpp>

namespace atlas::renderer::detail
{
/// @brief Opaque allocation, retained while referenced by registries or in-flight commands.
class gpu_resource
{
  public:
    virtual ~gpu_resource() = default; ///< Release native allocation after its last safe owner.
};

/// @brief Platform transfer boundary; no storage access or worker-owned GPU waits.
class resource_backend
{
  public:
    virtual ~resource_backend() = default; ///< Release only after drain.
    virtual std::uint64_t requirements(
        buffer_description description) const = 0; ///< Native aligned buffer allocation size.
    virtual std::uint64_t requirements(
        texture_description description) const = 0; ///< Native aligned texture allocation size.
    virtual std::shared_ptr<gpu_resource>
    create(buffer_description description) = 0; ///< Allocate default-heap buffer.
    virtual std::shared_ptr<gpu_resource>
    create(texture_description description) = 0; ///< Allocate default-heap texture.
    virtual void view(const std::shared_ptr<gpu_resource>& resource,
                      std::uint32_t index) = 0; ///< Create texture SRV at a reserved slot.
    virtual std::uint64_t
    upload(const std::shared_ptr<gpu_resource>& resource, resource_state before,
           std::span<const std::byte> bytes) = 0; ///< Retain staging and submit full upload.
    virtual std::uint64_t transition(const std::shared_ptr<gpu_resource>& resource,
                                     resource_state before,
                                     resource_state after) = 0; ///< Submit explicit barrier.
    virtual std::vector<std::byte> readback(
        const std::shared_ptr<gpu_resource>& resource,
        resource_state before) = 0; ///< Blocking diagnostic transfer restoring scheduled state.
    virtual std::uint64_t completed() const = 0; ///< Observe device removal and queue completion.
    virtual std::uint64_t issued() const = 0;    ///< Last known device-wide submission.
    virtual void wait(std::uint64_t value) = 0;  ///< Coordinator-only bounded wait.
    virtual void
    collect(std::uint64_t completed) = 0; ///< Drop only completed retained command batches.
    virtual resource_statistics
    statistics() const = 0; ///< Staging occupancy and allocator-reserved bytes.
    virtual std::pmr::memory_resource* memory() const = 0; ///< Borrow retained device CPU resource.
    virtual void drain() noexcept = 0; ///< Complete native work before registry destruction.
};

std::shared_ptr<resource_backend> make_resource_backend(
    std::shared_ptr<device_backend> device,
    resource_limits limits); ///< Construct the selected adapter or report unavailable graphics.
} // namespace atlas::renderer::detail
