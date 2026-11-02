/** @file @brief Pipeline admission, retirement and atomic frame submission. */
#include "pass_renderer_state.hpp"

namespace atlas::renderer
{
/// @brief Reject foreign and retired pipeline identities before native use.
pipeline_entry& pass_renderer::impl::get(pipeline_handle handle)
{
    resources->device->check_owner();
    if (handle.owner != identity || handle.index >= pipelines.size())
    {
        throw std::invalid_argument("foreign pipeline handle");
    }

    auto& entry = pipelines[handle.index];
    if (!entry.live || entry.generation != handle.generation)
    {
        throw std::invalid_argument("stale pipeline handle");
    }

    return entry;
}

/// @brief Retain the selected resource store and its coordinator.
pass_renderer::pass_renderer(resource_store& resources, pass_limits limits)
{
    resources.impl_->device->check_owner();
    limits.validate();
    impl_ = std::make_unique<impl>(resources.impl_, limits);
}

/// @brief Drain while all pipeline and resource metadata are still alive.
pass_renderer::~pass_renderer() = default;

/// @brief Compile only after capacity admission; failed compilation publishes no handle.
pipeline_handle pass_renderer::create_pipeline(const pipeline_description& description)
{
    impl_->resources->device->check_owner();
    description.validate();
    const diagnostics::operation_scope scope{impl_->resources->destinations,
                                             {"pass.create_pipeline", "renderer.cpu"}};
    collect();
    const auto found =
        std::find_if(impl_->pipelines.begin(), impl_->pipelines.end(),
                     [](const auto& entry)
                     {
                         return !entry.allocation &&
                                entry.generation != std::numeric_limits<std::uint64_t>::max();
                     });
    if (found == impl_->pipelines.end())
    {
        throw std::overflow_error("pipeline slots exhausted");
    }

    auto allocation = impl_->backend->create(description);
    found->depth = description.depth;
    found->allocation = std::move(allocation);
    found->last_use = 0;
    found->live = true;
    return {impl_->identity, static_cast<std::uint32_t>(found - impl_->pipelines.begin()),
            found->generation};
}

/// @brief Invalidate immediately while retaining the native pipeline through its fence.
void pass_renderer::release(pipeline_handle pipeline)
{
    impl_->get(pipeline).live = false;
}

/// @brief Reclaim only fence-completed pipeline slots and recording storage.
void pass_renderer::collect()
{
    impl_->resources->device->check_owner();
    impl_->backend->collect();
    const auto done = impl_->resources->backend->completed();
    for (auto& entry : impl_->pipelines)
    {
        if (entry.allocation && !entry.live && entry.last_use <= done)
        {
            entry.allocation.reset();
            if (entry.generation != std::numeric_limits<std::uint64_t>::max())
            {
                ++entry.generation;
            }
        }
    }
}

/// @brief Validate all references and prospective states before executing any command list.
completion_token pass_renderer::submit(const frame_plan& frame)
{
    impl_->resources->device->check_owner();
    const diagnostics::operation_scope scope{impl_->resources->destinations,
                                             {"pass.submit", "renderer.cpu"}};
    std::vector<scheduled_resource> states;
    std::vector<std::uint32_t> descriptors;
    auto resolved = impl_->resolve(frame, states, descriptors);
    const auto fence = impl_->backend->submit(resolved);
    for (std::size_t index = 0; index < states.size(); ++index)
    {
        if (!states[index].touched)
        {
            continue;
        }

        auto& entry = impl_->resources->entries[index];
        entry.last_use = fence;
        entry.state = states[index].state;
        entry.initialized = states[index].initialized;
        if (states[index].written)
        {
            entry.initialization = fence;
        }
    }

    for (const auto index : descriptors)
    {
        impl_->resources->descriptors[index].last_use = fence;
    }

    for (const auto& pass : frame.passes())
    {
        for (const auto& draw : pass.draws)
        {
            impl_->pipelines[draw.pipeline.index].last_use = fence;
        }
    }

    return {impl_->resources->device->identity, fence};
}
} // namespace atlas::renderer
