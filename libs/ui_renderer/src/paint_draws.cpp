/** @file @brief Resident-key resolution, logical projection and adjacent draw preparation. */
#include "paint_draws.hpp"
#include <algorithm>
#include <cmath>

namespace atlas::ui_renderer::detail
{
namespace
{
/// @brief Reject projection overflow rather than upload nonfinite vertex values.
float project(double value)
{
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
    {
        throw std::overflow_error("UI renderer projection overflow");
    }

    return static_cast<float>(value);
}

/// @brief Logical viewport coordinates map to physical attachment pixel coverage.
renderer::pixel_rectangle scissor(ui::layout_rect clip, ui::layout_rect viewport,
                                  renderer::surface_extent extent)
{
    const auto edge =
        [](double position, double origin, double size, std::uint32_t pixels, bool end)
    {
        const auto scaled =
            std::clamp((position - origin) / size * pixels, 0.0, static_cast<double>(pixels));
        return static_cast<std::uint32_t>(end ? std::ceil(scaled) : std::floor(scaled));
    };
    return {edge(clip.x, viewport.x, viewport.width, extent.width, false),
            edge(clip.y, viewport.y, viewport.height, extent.height, false),
            edge(static_cast<double>(clip.x) + clip.width, viewport.x, viewport.width, extent.width,
                 true),
            edge(static_cast<double>(clip.y) + clip.height, viewport.y, viewport.height,
                 extent.height, true)};
}

/// @brief Merge only adjacent equal bindings/scissors; never sort transparent geometry.
bool compatible(const renderer::draw_packet& a, const renderer::draw_packet& b)
{
    const auto& x = *a.scissor;
    const auto& y = *b.scissor;
    return a.pipeline == b.pipeline && a.texture == b.texture && x.left == y.left &&
           x.top == y.top && x.right == y.right && x.bottom == y.bottom;
}
} // namespace

/// @brief Validate keys and compute all CPU vertices before any GPU resource allocation.
void prepare_draws(paint_list list, renderer::texture_handle target,
                   renderer::surface_extent extent, std::span<const texture_binding> textures,
                   renderer::pipeline_handle solid, renderer::pipeline_handle textured,
                   std::pmr::memory_resource* memory, draw_preparation& output)
{
    const auto viewport = list.viewport;
    (void)ui::intersect_clip(viewport, viewport);
    if (extent.width == 0 || extent.height == 0 || viewport.width <= 0 || viewport.height <= 0 ||
        list.quads.size() > 65536 || textures.size() > 65536)
    {
        throw std::invalid_argument("invalid UI render extent or packet budget");
    }

    std::pmr::vector<texture_binding> bindings(textures.begin(), textures.end(), memory);
    std::sort(bindings.begin(), bindings.end(),
              [](const auto& a, const auto& b)
              {
                  return a.key < b.key;
              });
    for (std::size_t i = 0; i < bindings.size(); ++i)
    {
        if (bindings[i].key == 0 || bindings[i].view.owner == 0 ||
            (i != 0 && bindings[i - 1].key == bindings[i].key))
        {
            throw std::invalid_argument("invalid or duplicate UI texture binding");
        }
    }

    auto& vertices = output.vertices;
    vertices.reserve(list.quads.size() * 6);
    auto& pass = output.pass;
    pass.color.target = target;
    pass.color.clear_color = {0.04f, 0.05f, 0.08f, 1};
    pass.draws.reserve(list.quads.size());
    for (const auto& quad : list.quads)
    {
        const auto clip = ui::intersect_clip(quad.clip, viewport);
        if (clip.width == 0 || clip.height == 0)
        {
            continue;
        }

        renderer::draw_packet draw;
        draw.pipeline = quad.texture == 0 ? solid : textured;
        if (quad.texture != 0)
        {
            const auto binding = std::lower_bound(bindings.begin(), bindings.end(), quad.texture,
                                                  [](const auto& value, std::uint64_t key)
                                                  {
                                                      return value.key < key;
                                                  });
            if (binding == bindings.end() || binding->key != quad.texture)
            {
                throw std::invalid_argument("unresolved resident UI texture key");
            }

            draw.texture = binding->view;
        }

        draw.first_vertex = static_cast<std::uint32_t>(vertices.size());
        draw.vertex_count = 6;
        draw.scissor = scissor(clip, viewport, extent);
        for (const auto& value : quad.vertices)
        {
            vertices.push_back({{project((static_cast<double>(value.position.x) - viewport.x) /
                                             viewport.width * 2 -
                                         1),
                                 project(1 - (static_cast<double>(value.position.y) - viewport.y) /
                                                 viewport.height * 2),
                                 0},
                                value.color,
                                {value.uv.x, value.uv.y}});
        }

        if (!pass.draws.empty() && compatible(pass.draws.back(), draw))
        {
            pass.draws.back().vertex_count += 6;
        }
        else
        {
            pass.draws.push_back(draw);
        }
    }
}
} // namespace atlas::ui_renderer::detail
