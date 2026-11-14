/** @file @brief Built-in pixel material source for the fixed quad and b1 ABI. */
#pragma once
#include <string_view>

namespace atlas::ui_renderer::detail
{
/// @brief Link the fixed vertex signature to a trusted authored pixel function.
inline constexpr std::string_view authored_pixel_entry = R"(
struct atlas_material_input {
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
};
float4 pixel_main(atlas_material_input input) : SV_Target {
    return atlas_material(input.color, input.uv);
}
)";

/// @brief Evaluate bounded shapes and cosmetic phase within an unchanged quad.
inline constexpr std::string_view material_shader = R"(
cbuffer draw_constants : register(b0) { row_major float4x4 transform; float4 tint; };
cbuffer material_constants : register(b1) {
    float4 corners;
    float4 shape;
    float4 effect;
    float4 composition;
};
Texture2D<float4> image : register(t0);
SamplerState image_sampler : register(s0);
struct output_vertex { float4 position : SV_Position; float4 color : COLOR; float2 uv : TEXCOORD; };
output_vertex vertex_main(float3 position : POSITION, float4 color : COLOR, float2 uv : TEXCOORD) {
    output_vertex output;
    output.position = mul(float4(position, 1), transform);
    output.color = color * tint;
    output.uv = uv;
    return output;
}
float4 texture_main(output_vertex input) : SV_Target {
    return image.Sample(image_sampler, input.uv * corners.zw + corners.xy) * input.color;
}
float4 shape_main(output_vertex input) : SV_Target {
    float2 p = input.uv - 0.5;
    float half_size = 0.375 * shape.y;
    float radius = (p.y < 0 ? (p.x < 0 ? corners.x : corners.y)
                            : (p.x < 0 ? corners.z : corners.w)) * half_size;
    float2 q = abs(p) - half_size + radius;
    float round_distance = length(max(q, 0)) + min(max(q.x, q.y), 0) - radius;
    float2 b = abs(p) - half_size;
    float bevel_distance = max(max(b.x, b.y), (b.x + b.y + radius) * 0.70710678);
    float rectangle = lerp(round_distance, bevel_distance, shape.x);
    float ellipse = length(p) - half_size;
    float companion = length(p - float2(half_size * 0.6, 0)) - half_size * 0.72;
    float d = lerp(lerp(rectangle, ellipse, shape.z), min(rectangle, companion), composition.x);
    float aa = max(fwidth(d), 0.0001);
    float inside = saturate(0.5 - d / aa);
    float stroke = saturate(0.5 - (d + shape.w * half_size) / aa);
    float reveal = effect.y >= 1 ? 1 : saturate(0.5 + (effect.y - input.uv.x) /
                                                      max(fwidth(input.uv.x), 0.0001));
    float pulse = 0.75 + 0.25 * cos(effect.w * 6.28318530718);
    float glow = effect.x * pulse * exp(-18 * max(d, 0));
    float3 fill = input.color.rgb * (1 - saturate(input.uv.y) * effect.z);
    float3 rgb = lerp(fill, saturate(input.color.rgb * 1.5 + 0.15), inside - stroke);
    float alpha = (inside + (1 - inside) * glow) * reveal * input.color.a;
    return float4(rgb, shape.y > 0 && effect.y > 0 ? alpha : 0);
}
)";
} // namespace atlas::ui_renderer::detail
