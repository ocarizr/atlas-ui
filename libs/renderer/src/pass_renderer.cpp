/** @file @brief Pipeline admission, retirement and atomic frame submission. */
#include "pass_renderer_state.hpp"

namespace atlas::renderer
{
/// @brief Reject foreign and retired pipeline identities before native use.
pipeline_entry& pass_renderer::impl::get(pipeline_handle handle)
{
    resources->check_access();
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
    resources.impl_->check_access();
    limits.validate();
    impl_ = std::make_unique<impl>(resources.impl_, limits);
}

/// @brief Drain while all pipeline and resource metadata are still alive.
pass_renderer::~pass_renderer() = default;

/// @brief Compile only after capacity admission; failed compilation publishes no handle.
pipeline_handle pass_renderer::create_pipeline(const pipeline_description& description)
{
    impl_->resources->check_access();
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
    impl_->resources->check_access();
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
completion_token pass_renderer::impl::submit(const frame_plan& frame, recording_executor* executor,
                                             recording_options options)
{
    resources->check_access();
    const diagnostics::operation_scope scope{resources->destinations,
                                             {"pass.submit", "renderer.cpu"}};
    std::vector<scheduled_resource> states;
    std::vector<std::uint32_t> descriptors;
    options.validate();
    if (options.max_batches > limits.max_recording_batches)
    {
        throw std::invalid_argument("recording policy exceeds renderer batch capacity");
    }

    auto resolved = resolve(frame, states, descriptors);
    if (software && !options.allow_software_parallel)
    {
        options.max_batches = 1;
    }

    submission_guard transaction{*this};
    const auto fence = backend->submit(resolved, executor, options, resources->destinations);
    for (std::size_t index = 0; index < states.size(); ++index)
    {
        if (!states[index].touched)
        {
            continue;
        }

        auto& entry = resources->entries[index];
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
        resources->descriptors[index].last_use = fence;
    }

    for (const auto& pass : frame.passes())
    {
        for (const auto& draw : pass.draws)
        {
            pipelines[draw.pipeline.index].last_use = fence;
        }
    }

    return {resources->device->identity, fence};
}

/// @brief Preserve the sequential path when no executor is supplied.
completion_token pass_renderer::submit(const frame_plan& frame)
{
    return impl_->submit(frame, nullptr, {4096, 1});
}

/// @brief Borrow the application's executor only through this joined submission.
completion_token pass_renderer::submit(const frame_plan& frame, recording_executor& executor,
                                       recording_options options)
{
    return impl_->submit(frame, &executor, options);
}

/// @brief Observe recording without entering native APIs from another thread.
recording_statistics pass_renderer::last_recording() const
{
    impl_->resources->check_access();
    return impl_->backend->last_recording();
}
} // namespace atlas::renderer
