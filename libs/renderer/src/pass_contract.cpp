/** @file @brief Platform-neutral shader input and immutable frame contracts. */
#include <algorithm>
#include <atlas/renderer/passes.hpp>
#include <cctype>

namespace atlas::renderer
{
namespace
{
/// @brief Restrict entry points to ordinary ASCII HLSL identifiers.
bool identifier(std::string_view value)
{
    const auto alpha = [](unsigned char character)
    {
        return std::isalpha(character) != 0 || character == '_';
    };
    return !value.empty() && value.size() <= 128 &&
           alpha(static_cast<unsigned char>(value.front())) &&
           std::all_of(value.begin(), value.end(),
                       [&](unsigned char character)
                       {
                           return alpha(character) || std::isdigit(character) != 0;
                       });
}
} // namespace

/// @brief Reject invalid policies and custom sources before native shader compilation.
void pipeline_description::validate() const
{
    if ((program != shader_program::colored && program != shader_program::textured &&
         program != shader_program::custom) ||
        (blend != blend_mode::opaque && blend != blend_mode::straight_alpha &&
         blend != blend_mode::premultiplied_alpha) ||
        (depth != depth_mode::disabled && depth != depth_mode::read_write &&
         depth != depth_mode::read_only))
    {
        throw std::invalid_argument("unsupported pipeline policy");
    }

    if (program == shader_program::custom &&
        (source.empty() || source.size() > 1024ULL * 1024 || !identifier(vertex_entry) ||
         !identifier(pixel_entry)))
    {
        throw std::invalid_argument(
            "custom shaders require bounded source and identifier entry points");
    }
}

/// @brief Deep-copy nested vectors to eliminate writable caller aliases.
frame_plan::frame_plan(std::span<const pass_description> passes)
    : passes_(std::make_shared<const std::vector<pass_description>>(passes.begin(), passes.end()))
{
    if (passes.empty())
    {
        throw std::invalid_argument("a frame requires at least one pass");
    }
}

/// @brief Borrow the immutable snapshot without exposing mutable storage.
std::span<const pass_description> frame_plan::passes() const noexcept
{
    return *passes_;
}

/// @brief Bound all capacities before creating native command storage.
void pass_limits::validate() const
{
    if (max_pipelines == 0 || max_pipelines > 4096 || max_passes == 0 || max_passes > 1024 ||
        max_draws == 0 || max_draws > 1048576 || max_frames == 0 || max_frames > 64)
    {
        throw std::invalid_argument("pass capacities must be positive and bounded");
    }
}
} // namespace atlas::renderer
