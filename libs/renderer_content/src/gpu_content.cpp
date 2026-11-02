/** @file @brief Fenced texture ownership and exact-description pipeline reuse. */
#include <algorithm>
#include <atlas/renderer_content/gpu_content.hpp>
#include <tuple>

namespace atlas::renderer_content
{
/// @brief Validate before allocation; roll back all partial native admission on failure.
texture_upload::texture_upload(renderer::resource_store& resources,
                               std::shared_ptr<const image_data> image,
                               diagnostics::services destinations)
    : resources_(resources), image_(std::move(image)), destinations_(std::move(destinations))
{
    const diagnostics::operation_scope scope{destinations_,
                                             {"content.texture.upload", "content.cpu"}};
    if (!image_)
    {
        throw std::invalid_argument("texture upload needs decoded pixels");
    }

    image_->validate();
    try
    {
        binding_.texture = resources_.create_texture({image_->width, image_->height});
        completion_ = resources_.upload(binding_.texture, image_->pixels);
        binding_.view = resources_.create_view(binding_.texture);
    }
    catch (...)
    {
        cancel();
        throw;
    }
}

/// @brief Destruction must obey the borrowed registry's coordinator/lifetime contract.
texture_upload::~texture_upload()
{
    try
    {
        cancel();
    }
    catch (...)
    {
        std::terminate();
    }
}

/// @brief Poll actual GPU initialization before releasing retained decoded CPU pixels.
bool texture_upload::ready()
{
    (void)resources_.statistics();
    if (binding_.texture.owner == 0)
    {
        return false;
    }

    if (!resources_.completed(completion_))
    {
        return false;
    }

    image_.reset();
    resources_.collect();
    return true;
}

/// @brief A completed CPU request alone never exposes a usable native texture.
texture_binding texture_upload::binding()
{
    if (!ready())
    {
        throw std::logic_error("texture upload is not ready or was cancelled");
    }

    return binding_;
}

/// @brief Staging is independently retained by resource_store through the transfer fence.
void texture_upload::cancel()
{
    (void)resources_.statistics();
    const diagnostics::operation_scope scope{destinations_,
                                             {"content.texture.retire", "content.cpu"}};
    if (binding_.view.owner != 0)
    {
        resources_.release(binding_.view);
        binding_.view = {};
    }

    if (binding_.texture.owner != 0)
    {
        resources_.release(binding_.texture);
        binding_.texture = {};
    }

    image_.reset();
}

/// @brief Reserve cache entries before publishing pipelines so insertion cannot lose a handle.
pipeline_cache::pipeline_cache(renderer::pass_renderer& renderer, std::size_t capacity,
                               diagnostics::services destinations)
    : renderer_(renderer), capacity_(capacity), destinations_(std::move(destinations))
{
    (void)renderer_.last_recording();
    if (capacity == 0 || capacity > 4096)
    {
        throw std::invalid_argument("invalid pipeline cache capacity");
    }

    entries_.reserve(capacity);
}

/// @brief Release native pipeline ownership through the registry's last-use fences.
pipeline_cache::~pipeline_cache()
{
    try
    {
        clear();
    }
    catch (...)
    {
        std::terminate();
    }
}

/// @brief Exact comparisons avoid source-hash collisions and cache no failed compilation.
renderer::pipeline_handle pipeline_cache::get(const renderer::pipeline_description& description)
{
    (void)renderer_.last_recording();
    const diagnostics::operation_scope scope{destinations_,
                                             {"content.pipeline.get", "content.cpu"}};
    description.validate();
    const auto key = [](const auto& value)
    {
        return std::tie(value.program, value.blend, value.depth, value.linear_sampling,
                        value.source, value.vertex_entry, value.pixel_entry);
    };
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [&](const auto& value)
                                    {
                                        return key(value.description) == key(description);
                                    });
    if (found != entries_.end())
    {
        return found->pipeline;
    }

    if (entries_.size() == capacity_)
    {
        throw std::overflow_error("pipeline cache is full");
    }

    entry pending{description, {}};
    pending.pipeline = renderer_.create_pipeline(description);
    entries_.push_back(std::move(pending));
    return entries_.back().pipeline;
}

/// @brief Clearing invalidates borrowed handles; existing native submissions remain protected.
void pipeline_cache::clear()
{
    (void)renderer_.last_recording();
    const diagnostics::operation_scope scope{destinations_,
                                             {"content.pipeline.clear", "content.cpu"}};
    for (const auto& entry : entries_)
    {
        renderer_.release(entry.pipeline);
    }

    entries_.clear();
}

/// @brief Observe the number of cached exact descriptions on the owning coordinator.
std::size_t pipeline_cache::size() const
{
    (void)renderer_.last_recording();
    return entries_.size();
}
} // namespace atlas::renderer_content
