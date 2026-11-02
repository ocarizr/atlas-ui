// Generic textured primitive, independent of UI layout and widget behavior.
cbuffer draw_constants : register(b0) { row_major float4x4 transform; float4 tint; };
Texture2D<float4> image : register(t0);
SamplerState image_sampler : register(s0);
struct vertex_input { float3 position : POSITION; float4 color : COLOR; float2 uv : TEXCOORD; };
struct vertex_output { float4 position : SV_Position; float4 color : COLOR; float2 uv : TEXCOORD; };
vertex_output vertex_main(vertex_input input) {
    vertex_output output;
    output.position = mul(float4(input.position, 1), transform);
    output.color = input.color * tint;
    output.uv = input.uv;
    return output;
}
float4 pixel_main(vertex_output input) : SV_Target {
    return image.Sample(image_sampler, input.uv) * input.color;
}
