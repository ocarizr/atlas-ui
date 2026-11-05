/** @file @brief Bounded upload admission and fence-safe reclamation of unwanted image owners. */
#include "residency_state.hpp"
#include <algorithm>

namespace atlas::ui_images
{
/// @brief The fallback and all upload owners use the caller's existing graphics domain.
image_renderer::impl::impl(renderer::resource_store& store, renderer::pass_renderer& passes,
                           residency_limits policy,
                           std::shared_ptr<std::pmr::memory_resource> resource,
                           diagnostics::services services)
    : resources(store), limits(policy), memory(std::move(resource)),
      destinations(std::move(services)), painter(resources, passes, memory, destinations),
      entries(0, memory.get()), wanted(0, memory.get())
{
    entries.reserve(limits.max_images);
    wanted.reserve(limits.max_images);
    auto image = std::make_shared<renderer_content::image_data>(memory);
    image->width = image->height = 1;
    image->pixels = {std::byte{128}, std::byte{128}, std::byte{128}, std::byte{255}};
    fallback = std::make_unique<renderer_content::texture_upload>(resources, std::move(image),
                                                                  destinations);
    stats.resident_bytes = stats.peak_resident_bytes = 4;
}

/// @brief Wrong-thread callers never touch the native store or mutable admission metadata.
void image_renderer::impl::check() const
{
    jobs::require_coordinator_thread();
    if (owner != std::this_thread::get_id() || busy)
    {
        throw std::logic_error("UI image renderer requires its non-reentrant GPU owner");
    }
}

/// @brief Bounded linear metadata search avoids per-widget GPU ownership/control blocks.
resident_image* image_renderer::impl::find(std::uint64_t key)
{
    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [key](const auto& value)
                                 {
                                     return value.key == key;
                                 });
    return it == entries.end() ? nullptr : &*it;
}

/// @brief Retiring logical budget cannot be reused while an upload or sampled frame is in flight.
void image_renderer::impl::collect()
{
    const diagnostics::operation_scope scope{destinations, {"ui.images.gpu.collect", "ui.cpu"}};
    for (auto it = entries.begin(); it != entries.end();)
    {
        if (it->upload && it->view.owner == 0 && it->upload->ready())
        {
            it->view = it->upload->binding().view;
        }

        const bool initialized = !it->upload || it->view.owner != 0;
        if (!it->wanted && initialized &&
            (it->last_use.value == 0 || resources.completed(it->last_use)))
        {
            stats.resident_bytes -= it->bytes;
            it = entries.erase(it);
        }
        else
        {
            ++it;
        }
    }

    resources.collect();
}

/// @brief Unknown/pending/failed CPU keys need no upload: submission resolves them to fallback.
void image_renderer::impl::prepare(const frame_payload& frame)
{
    const diagnostics::operation_scope scope{destinations, {"ui.images.gpu.prepare", "ui.cpu"}};
    wanted.clear();
    for (const auto& quad : frame.paint.quads())
    {
        if (quad.texture != 0)
        {
            wanted.push_back(quad.texture);
        }
    }

    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
    for (auto& entry : entries)
    {
        const auto* source = frame.images.find(entry.key);
        entry.wanted = source && source->status == image_status::decoded && source->pixels &&
                       std::binary_search(wanted.begin(), wanted.end(), entry.key);
    }

    collect();
    stats.deferred = 0;
    std::size_t admitted = 0;
    for (const auto key : wanted)
    {
        const auto* source = frame.images.find(key);
        if (find(key) || !source || source->status != image_status::decoded || !source->pixels)
        {
            continue;
        }

        const auto bytes = source->pixels->pixels.size();
        if (entries.size() == limits.max_images || admitted == limits.max_uploads_per_frame ||
            bytes > limits.max_resident_bytes - stats.resident_bytes)
        {
            ++stats.deferred;
            continue;
        }

        ++admitted;
        try
        {
            auto upload = std::make_unique<renderer_content::texture_upload>(
                resources, source->pixels, destinations);
            entries.push_back({key, std::move(upload), bytes, {}, true, false});
            stats.resident_bytes += bytes;
            stats.peak_resident_bytes = std::max(stats.peak_resident_bytes, stats.resident_bytes);
        }
        catch (const renderer::device_lost&)
        {
            throw;
        }
        catch (const std::overflow_error&)
        {
            ++stats.deferred;
        }
        catch (const std::exception& error)
        {
            entries.push_back({key, {}, 0, {}, true, true});
            diagnostics::write(destinations, diagnostics::log_level::warning, "ui.images",
                               error.what());
        }
        catch (...)
        {
            entries.push_back({key, {}, 0, {}, true, true});
            diagnostics::write(destinations, diagnostics::log_level::warning, "ui.images",
                               "image GPU upload failed; placeholder retained");
        }
    }

    stats.images = entries.size();
    stats.failed = static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(),
                                                          [](const auto& entry)
                                                          {
                                                              return entry.failed;
                                                          }));
}
} // namespace atlas::ui_images
