/** @file @brief Public resource operations and bounded fence-based reclamation. */
#include "resource_store_state.hpp"

namespace atlas::renderer
{
/// @brief Reject invalid descriptions or capacities before native mutation.
void buffer_description::validate() const
{
    if (size == 0)
    {
        throw std::invalid_argument("GPU buffer size must be positive");
    }
}

/// @brief Reject invalid descriptions or capacities before native mutation.
void texture_description::validate() const
{
    if (width == 0 || height == 0 || width > 16384 || height > 16384 ||
        (role != texture_role::sampled && role != texture_role::color_target &&
         role != texture_role::depth_target))
    {
        throw std::invalid_argument("RGBA texture extent must be in [1, 16384]");
    }
}

/// @brief Reject invalid descriptions or capacities before native mutation.
void resource_limits::validate() const
{
    if (max_resources == 0 || max_resources > 65536 || max_descriptors == 0 ||
        max_descriptors > 65536 || max_transfers == 0 || max_transfers > 4096 ||
        max_allocation_bytes == 0 || max_staging_bytes == 0)
    {
        throw std::invalid_argument("GPU resource limits must be positive and bounded");
    }
}

/// @brief Initialize bounded GPU ownership on the existing device coordinator.
resource_store::resource_store(device& graphics, resource_limits limits)
{
    const diagnostics::operation_scope scope{graphics.diagnostics_,
                                             {"resource_store.initialize", "renderer.cpu"}};
    graphics.backend_->check_owner();
    limits.validate();
    impl_ = std::make_shared<impl>(graphics.backend_, limits, graphics.diagnostics_);
    diagnostics::write(impl_->destinations, diagnostics::log_level::info, "renderer.resources",
                       "GPU resource store initialized");
}

/// @brief Drain retained GPU work before releasing native ownership on the coordinator.
resource_store::~resource_store() = default;

/// @brief Allocate an uninitialized default-heap buffer and publish its typed identity.
buffer_handle resource_store::create_buffer(buffer_description description)
{
    return impl_->create<buffer_tag>(description);
}

/// @brief Allocate an uninitialized RGBA texture and publish its typed identity.
texture_handle resource_store::create_texture(texture_description description)
{
    return impl_->create<texture_tag>(description);
}

/// @brief Reserve a descriptor retaining its parent allocation until safe reclamation.
descriptor_handle resource_store::create_view(texture_handle texture)
{
    auto& parent = impl_->get(texture);
    if (parent.description.role == texture_role::depth_target)
    {
        throw std::invalid_argument("depth targets do not support shader views");
    }

    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"resource.create_view", "renderer.cpu"}};
    const auto found = std::find_if(
        impl_->descriptors.begin(), impl_->descriptors.end(),
        [](const auto& value)
        {
            return !value.occupied && value.generation != std::numeric_limits<std::uint64_t>::max();
        });
    if (found == impl_->descriptors.end())
    {
        throw std::overflow_error("GPU descriptor slots exhausted; collect retired work");
    }

    const auto index = static_cast<std::uint32_t>(found - impl_->descriptors.begin());
    impl_->backend->view(parent.allocation, index);
    found->occupied = true;
    found->live = true;
    found->resource = texture.index;
    found->last_use = parent.last_use;
    ++parent.views;
    return {impl_->identity, index, found->generation};
}

/// @brief Copy exact source bytes into retained staging and publish the protecting queue fence.
completion_token resource_store::upload(buffer_handle value, std::span<const std::byte> bytes)
{
    return impl_->upload(value, bytes);
}

/// @brief Copy exact source bytes into retained staging and publish the protecting queue fence.
completion_token resource_store::upload(texture_handle value, std::span<const std::byte> bytes)
{
    return impl_->upload(value, bytes);
}

/// @brief Schedule an explicit native state change and extend the resource lifetime.
completion_token resource_store::transition(buffer_handle value, resource_state target)
{
    return impl_->transition(value, target);
}

/// @brief Schedule an explicit native state change and extend the resource lifetime.
completion_token resource_store::transition(texture_handle value, resource_state target)
{
    return impl_->transition(value, target);
}

/// @brief Protect referenced allocations and descriptors through a known device submission.
void resource_store::mark_used(buffer_handle value, completion_token after)
{
    impl_->mark(value, after);
}

/// @brief Protect referenced allocations and descriptors through a known device submission.
void resource_store::mark_used(texture_handle value, completion_token after)
{
    impl_->mark(value, after);
}

/// @brief Protect referenced allocations and descriptors through a known device submission.
void resource_store::mark_used(descriptor_handle value, completion_token after)
{
    auto& view = impl_->get(value);
    const auto fence = impl_->token(after);
    view.last_use = std::max(view.last_use, fence);
    auto& parent = impl_->entries[view.resource];
    parent.last_use = std::max(parent.last_use, fence);
}

/// @brief Invalidate the public identity immediately while retaining storage through GPU
/// completion.
void resource_store::release(buffer_handle value, completion_token after)
{
    impl_->release(value, after);
}

/// @brief Invalidate the public identity immediately while retaining storage through GPU
/// completion.
void resource_store::release(texture_handle value, completion_token after)
{
    impl_->release(value, after);
}

/// @brief Invalidate the public identity immediately while retaining storage through GPU
/// completion.
void resource_store::release(descriptor_handle value, completion_token after)
{
    mark_used(value, after);
    auto& view = impl_->get(value);
    view.last_use = std::max(view.last_use, impl_->entries[view.resource].last_use);
    view.live = false;
}

/// @brief Report initialized data only after its protecting queue value has completed.
bool resource_store::ready(buffer_handle value) const
{
    const auto& entry = impl_->get(value);
    const auto complete = impl_->backend->completed();
    return entry.initialized && complete >= entry.initialization;
}

/// @brief Report initialized data only after its protecting queue value has completed.
bool resource_store::ready(texture_handle value) const
{
    const auto& entry = impl_->get(value);
    const auto complete = impl_->backend->completed();
    return entry.initialized && complete >= entry.initialization;
}

/// @brief Observe the scheduled state without implying completion of GPU execution.
resource_state resource_store::state(buffer_handle value) const
{
    return impl_->get(value).state;
}

/// @brief Observe the scheduled state without implying completion of GPU execution.
resource_state resource_store::state(texture_handle value) const
{
    return impl_->get(value).state;
}

/// @brief Observe queue completion and reject device-removal sentinels.
bool resource_store::completed(completion_token value) const
{
    return impl_->backend->completed() >= impl_->token(value);
}

/// @brief Wait only on the coordinator with the device diagnostic timeout.
void resource_store::wait(completion_token value)
{
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"resource.wait", "renderer.cpu_wait"}};
    impl_->backend->wait(impl_->token(value));
    collect();
}

/// @brief Reclaim completed staging and retired slots without blocking for GPU completion.
void resource_store::collect()
{
    impl_->check_access();
    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"resource.collect", "renderer.cpu"}};
    const auto complete = impl_->backend->completed();
    impl_->backend->collect(complete);
    for (auto& view : impl_->descriptors)
    {
        if (view.occupied && !view.live && view.last_use <= complete)
        {
            --impl_->entries[view.resource].views;
            const auto generation = view.generation + 1;
            view = {};
            view.generation = generation;
        }
    }

    for (auto& entry : impl_->entries)
    {
        if (entry.allocation && !entry.live && entry.views == 0 && entry.last_use <= complete)
        {
            impl_->allocation_bytes -= entry.charge;
            const auto generation = entry.generation + 1;
            entry = {};
            entry.generation = generation;
        }
    }
}

/// @brief Copy initialized GPU data to CPU bytes and restore the scheduled resource state.
std::vector<std::byte> resource_store::readback(buffer_handle value)
{
    return impl_->readback(value);
}

/// @brief Copy initialized GPU data to CPU bytes and restore the scheduled resource state.
std::vector<std::byte> resource_store::readback(texture_handle value)
{
    return impl_->readback(value);
}

/// @brief Report occupied allocation ranges and staging separately from allocator heap reservation.
resource_statistics resource_store::statistics() const
{
    impl_->check_access();
    auto result = impl_->backend->statistics();
    result.allocation_bytes = impl_->allocation_bytes;
    for (const auto& entry : impl_->entries)
    {
        result.live_resources += static_cast<std::size_t>(entry.live);
        result.retired_resources += static_cast<std::size_t>(entry.allocation && !entry.live);
    }

    for (const auto& view : impl_->descriptors)
    {
        result.live_descriptors += static_cast<std::size_t>(view.live);
        result.retired_descriptors += static_cast<std::size_t>(view.occupied && !view.live);
    }

    return result;
}
} // namespace atlas::renderer
