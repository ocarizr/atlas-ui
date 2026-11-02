/** @file @brief Whole-frame validation and prospective resource-state simulation. */
#include "pass_renderer_state.hpp"
#include <cmath>

namespace atlas::renderer
{
namespace
{
/// @brief Check finite floating-point arrays before crossing the native boundary.
template <std::size_t Size> bool finite(const std::array<float, Size>& values)
{
    return std::all_of(values.begin(), values.end(),
                       [](float value)
                       {
                           return std::isfinite(value);
                       });
}

/// @brief Validate explicit load/store enum values and initialization.
void attachment(load_action load, store_action store, bool initialized)
{
    if ((load != load_action::clear && load != load_action::load) ||
        (store != store_action::store && store != store_action::discard))
    {
        throw std::invalid_argument("unsupported attachment action");
    }

    if (load == load_action::load && !initialized)
    {
        throw std::logic_error("attachment load requires initialized contents");
    }
}

/// @brief Reject nonfinite and out-of-range clear colors.
void clear_color(const std::array<float, 4>& color)
{
    if (!finite(color) || std::any_of(color.begin(), color.end(),
                                      [](float value)
                                      {
                                          return value < 0 || value > 1;
                                      }))
    {
        throw std::invalid_argument("clear color must be finite normalized RGBA");
    }
}

/// @brief Validate optional draw rectangles against attachment bounds.
void rectangles(const draw_packet& draw, texture_description extent)
{
    if (!finite(draw.transform) || !finite(draw.tint))
    {
        throw std::invalid_argument("draw constants must be finite");
    }

    if (draw.scissor)
    {
        const auto& rectangle = *draw.scissor;
        if (rectangle.left > rectangle.right || rectangle.top > rectangle.bottom ||
            rectangle.right > extent.width || rectangle.bottom > extent.height)
        {
            throw std::invalid_argument("scissor exceeds attachment bounds");
        }
    }

    if (draw.viewport)
    {
        const auto& viewport = *draw.viewport;
        if (!finite(std::array{viewport.x, viewport.y, viewport.width, viewport.height}) ||
            viewport.x < 0 || viewport.y < 0 || viewport.width <= 0 || viewport.height <= 0 ||
            viewport.x + viewport.width > static_cast<float>(extent.width) ||
            viewport.y + viewport.height > static_cast<float>(extent.height))
        {
            throw std::invalid_argument("viewport exceeds attachment bounds");
        }
    }
}
} // namespace

/// @brief Resolve one draw against prospective attachment and input states.
detail::resolved_draw pass_renderer::impl::resolve_draw(const draw_packet& draw,
                                                        const detail::resolved_pass& pass,
                                                        std::vector<scheduled_resource>& states,
                                                        std::vector<std::uint32_t>& descriptors)
{
    auto& pipeline = get(draw.pipeline);
    if ((pipeline.depth != depth_mode::disabled) != pass.depth.has_value())
    {
        throw std::invalid_argument("pipeline and depth attachment are incompatible");
    }

    auto& vertices = resources->get(draw.vertices);
    auto& vertex_state = states[draw.vertices.index];
    const auto available = vertices.payload / sizeof(vertex);
    if (!vertex_state.initialized || vertices.payload % sizeof(vertex) != 0 ||
        draw.vertex_count == 0 || draw.vertex_count % 3 != 0 || draw.first_vertex > available ||
        draw.vertex_count > available - draw.first_vertex ||
        vertices.payload > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::invalid_argument("draw requires initialized in-bounds triangle vertices");
    }

    rectangles(draw, pass.extent);
    detail::resolved_draw command{
        draw, pipeline.allocation, {vertices.allocation, vertex_state.state}, {}};
    vertex_state.state = resource_state::shader_read;
    vertex_state.touched = true;
    if (pipeline.allocation->textured)
    {
        auto& descriptor = resources->get(draw.texture);
        auto& texture = resources->entries[descriptor.resource];
        auto& texture_state = states[descriptor.resource];
        if (descriptor.resource == pass.description.color.target.index)
        {
            throw std::invalid_argument("attachment feedback is forbidden");
        }

        if (!texture_state.initialized)
        {
            throw std::logic_error("texture input requires initialized stored contents");
        }

        command.texture = detail::pass_resource{texture.allocation, texture_state.state};
        texture_state.state = resource_state::shader_read;
        texture_state.touched = true;
        descriptors.push_back(draw.texture.index);
    }
    else if (draw.texture.owner != 0)
    {
        throw std::invalid_argument("pipeline does not consume a texture binding");
    }

    return command;
}

/// @brief Simulate ordered writes and reads without modifying live registry state.
std::vector<detail::resolved_pass>
pass_renderer::impl::resolve(const frame_plan& frame, std::vector<scheduled_resource>& states,
                             std::vector<std::uint32_t>& descriptors)
{
    const diagnostics::operation_scope scope{resources->destinations,
                                             {"pass.validate", "renderer.cpu"}};
    if (frame.passes().size() > limits.max_passes)
    {
        throw std::length_error("frame pass limit exceeded");
    }

    states.reserve(resources->entries.size());
    for (const auto& entry : resources->entries)
    {
        states.push_back({entry.state, entry.initialized});
    }

    std::vector<detail::resolved_pass> result;
    result.reserve(frame.passes().size());
    std::size_t draws = 0;
    for (const auto& pass : frame.passes())
    {
        if (pass.draws.size() > limits.max_draws - draws)
        {
            throw std::length_error("frame draw limit exceeded");
        }

        draws += pass.draws.size();
        auto& color = resources->get(pass.color.target);
        auto& color_state = states[pass.color.target.index];
        if (color.description.role != texture_role::color_target)
        {
            throw std::invalid_argument("color attachment requires color_target allocation");
        }

        attachment(pass.color.load, pass.color.store, color_state.initialized);
        clear_color(pass.color.clear_color);
        detail::resolved_pass resolved;
        resolved.description = pass;
        resolved.extent = color.description;
        resolved.color = {color.allocation, color_state.state};
        color_state.state = resource_state::render_target;
        color_state.touched = true;
        color_state.written = true;
        if (pass.depth)
        {
            auto& depth = resources->get(pass.depth->target);
            auto& depth_state = states[pass.depth->target.index];
            if (depth.description.role != texture_role::depth_target ||
                depth.description.width != color.description.width ||
                depth.description.height != color.description.height)
            {
                throw std::invalid_argument("depth attachment role or extent mismatch");
            }

            attachment(pass.depth->load, pass.depth->store, depth_state.initialized);
            if (!std::isfinite(pass.depth->clear_depth) || pass.depth->clear_depth < 0 ||
                pass.depth->clear_depth > 1)
            {
                throw std::invalid_argument("depth clear must be finite and normalized");
            }

            resolved.depth = detail::pass_resource{depth.allocation, depth_state.state};
            depth_state.state = resource_state::depth_write;
            depth_state.touched = true;
            depth_state.written = true;
            depth_state.initialized = pass.depth->store == store_action::store;
        }

        resolved.draws.reserve(pass.draws.size());
        for (const auto& draw : pass.draws)
        {
            resolved.draws.push_back(resolve_draw(draw, resolved, states, descriptors));
        }

        color_state.initialized = pass.color.store == store_action::store;
        result.push_back(std::move(resolved));
    }

    return result;
}
} // namespace atlas::renderer
