/** @file @brief Immutable frame image resolution with automatic last-use fence recording. */
#include "residency_state.hpp"
#include <algorithm>

namespace atlas::ui_images
{
/// @brief Reject invalid budgets before any native resource is allocated.
image_renderer::image_renderer(renderer::resource_store& resources, renderer::pass_renderer& passes,
                               residency_limits limits,
                               std::shared_ptr<std::pmr::memory_resource> memory,
                               diagnostics::services destinations)
{
    jobs::require_coordinator_thread();
    if (limits.max_images == 0 || limits.max_images > 4096 || limits.max_resident_bytes < 4 ||
        limits.max_uploads_per_frame == 0 || limits.max_uploads_per_frame > 4096)
    {
        throw std::invalid_argument("invalid UI image residency limits");
    }

    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](std::pmr::memory_resource*)
                  {
                  }};
    }

    impl_ = std::make_unique<impl>(resources, passes, limits, std::move(memory),
                                   std::move(destinations));
}

/// @brief Handle retirement is nonblocking; native resources retain their last submitted fences.
image_renderer::~image_renderer()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy)
    {
        std::terminate();
    }

    impl_->busy = true;
}

/// @brief Explicit collection never waits for pending uploads or frames.
void image_renderer::collect()
{
    impl_->check();
    impl_->busy = true;
    try
    {
        impl_->collect();
        impl_->busy = false;
    }
    catch (...)
    {
        impl_->busy = false;
        throw;
    }
}

/// @brief The fallback is a real resident GPU texture, not an unresolved descriptor.
bool image_renderer::ready()
{
    impl_->check();
    impl_->busy = true;
    try
    {
        const auto result = impl_->fallback->ready();
        impl_->busy = false;
        return result;
    }
    catch (...)
    {
        impl_->busy = false;
        throw;
    }
}

/// @brief Every successful submission records resource use before another frame can evict it.
renderer::completion_token image_renderer::submit(const frame_payload& frame,
                                                  renderer::texture_handle target,
                                                  renderer::surface_extent extent,
                                                  renderer::recording_executor* recorder)
{
    impl_->check();
    impl_->busy = true;

    struct reset
    {
        bool& busy; ///< Owner flag retained through native failure cleanup.

        ~reset()
        {
            busy = false;
        } ///< Restore on every exit.
    } guard{impl_->busy}; ///< No callbacks can mutate residency during resolution.

    const diagnostics::operation_scope scope{impl_->destinations,
                                             {"ui.images.gpu.submit", "ui.cpu"}};
    if (!impl_->fallback->ready())
    {
        throw std::logic_error("UI image placeholder upload is pending");
    }

    impl_->prepare(frame);
    std::pmr::vector<ui_renderer::texture_binding> bindings(0, impl_->memory.get());
    bindings.reserve(impl_->wanted.size());
    std::size_t drawn = 0;
    for (const auto key : impl_->wanted)
    {
        auto* entry = impl_->find(key);
        const bool resident = entry && entry->wanted && entry->view.owner != 0;
        if (resident)
        {
            ++drawn;
        }

        const auto view = resident ? entry->view : impl_->fallback->binding().view;
        bindings.push_back({key, view});
    }

    const auto completion = impl_->painter.submit(frame.paint, target, extent, bindings, recorder);
    for (auto& entry : impl_->entries)
    {
        if (entry.wanted && entry.view.owner != 0)
        {
            entry.last_use = completion;
        }
    }

    impl_->stats.drawn_images = drawn;
    return completion;
}

/// @brief Accounting includes retiring owners until actual queue completion permits reclamation.
residency_statistics image_renderer::statistics() const
{
    impl_->check();
    auto result = impl_->stats;
    result.images = impl_->entries.size();
    result.failed =
        static_cast<std::size_t>(std::count_if(impl_->entries.begin(), impl_->entries.end(),
                                               [](const auto& entry)
                                               {
                                                   return entry.failed;
                                               }));
    return result;
}
} // namespace atlas::ui_images
