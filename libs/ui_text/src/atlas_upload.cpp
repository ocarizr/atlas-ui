/** @file @brief Upload immutable atlas pages and retire them through resource-store fences. */
#include <algorithm>
#include <atlas/ui_text/paint.hpp>
#include <limits>

namespace atlas::ui_text
{
/// @brief Enqueue each bounded page upload and retain its descriptor for ordered submissions.
uploaded_atlas::uploaded_atlas(renderer::resource_store& resources, const glyph_atlas& atlas,
                               std::uint64_t first_texture_key, const diagnostics::services& sinks)
    : resources_{resources}
{
    const diagnostics::operation_scope scope{sinks, {"ui.text.upload", "ui.text.cpu"}};
    const auto pages = atlas.pages();
    if (!first_texture_key ||
        (!pages.empty() &&
         pages.size() - 1 > std::numeric_limits<std::uint64_t>::max() - first_texture_key))
    {
        throw std::invalid_argument("Invalid UI glyph atlas texture key range");
    }

    textures_.reserve(pages.size());
    bindings_.reserve(pages.size());
    try
    {
        for (const auto& page : pages)
        {
            const auto texture = resources_.create_texture({atlas.side(), atlas.side()});
            textures_.push_back(texture);
            resources_.upload(texture, page.rgba);
            bindings_.push_back(
                {first_texture_key + bindings_.size(), resources_.create_view(texture)});
        }
    }
    catch (...)
    {
        release();
        throw;
    }
}

/// @brief Release resident handles and keep in-flight native allocations fenced.
uploaded_atlas::~uploaded_atlas()
{
    release();
}

/// @brief Release views before their underlying page handles.
void uploaded_atlas::release()
{
    for (const auto& binding : bindings_)
    {
        resources_.release(binding.view);
    }

    for (const auto texture : textures_)
    {
        resources_.release(texture);
    }
}

/// @brief Poll every initialized page on the graphics coordinator.
bool uploaded_atlas::ready() const
{
    return std::ranges::all_of(textures_,
                               [this](renderer::texture_handle texture)
                               {
                                   return resources_.ready(texture);
                               });
}

/// @brief Publication is atomic across pages; callers can keep the previous atlas until ready.
std::span<const ui_renderer::texture_binding> uploaded_atlas::bindings() const
{
    if (!ready())
    {
        throw std::logic_error("UI glyph atlas uploads are still pending");
    }

    return bindings_;
}
} // namespace atlas::ui_text
