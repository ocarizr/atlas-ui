/** @file @brief Fixed-ABI colored/textured graphics pipelines and explicit blend/depth state. */
#include "native_passes.hpp"

namespace atlas::renderer::detail
{
namespace
{
/// @brief Generic vertex/color/UV program shared by 2D and 3D primitive packets.
constexpr std::string_view builtin_shader = R"(
cbuffer draw_constants : register(b0) { row_major float4x4 transform; float4 tint; };
Texture2D<float4> image : register(t0);
SamplerState image_sampler : register(s0);
struct vertex_input { float3 position : POSITION; float4 color : COLOR; float2 uv : TEXCOORD; };
struct vertex_output { float4 position : SV_Position; float4 color : COLOR; float2 uv : TEXCOORD; };
vertex_output vertex_main(vertex_input input) {
    vertex_output output;
    output.position = mul(float4(input.position,1),transform);
    output.color = input.color*tint; output.uv = input.uv; return output;
}
float4 pixel_main(vertex_output input) : SV_Target { return input.color; }
float4 textured_main(vertex_output input) : SV_Target { return image.Sample(image_sampler,input.uv)*input.color; }
)";

/// @brief Declare a stable root ABI instead of exposing native descriptors in public headers.
com_ptr<ID3D12RootSignature> root_signature(native_device& context, bool linear)
{
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants = {0, 0, 20};
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable = {1, &range};
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = linear ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy = 1;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC description{
        2, parameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
    com_ptr<ID3DBlob> bytes, errors;
    check(D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, &bytes, &errors),
          "serialize pass root signature");
    com_ptr<ID3D12RootSignature> root;
    check(context.gpu->CreateRootSignature(0, bytes->GetBufferPointer(), bytes->GetBufferSize(),
                                           IID_PPV_ARGS(&root)),
          "create pass root signature");
    return root;
}
} // namespace

/// @brief Compile and create a complete PSO before admitting it to the public registry.
std::shared_ptr<gpu_pipeline> create_pass_pipeline(native_device& context,
                                                   const pipeline_description& description)
{
    const bool custom = description.program == shader_program::custom;
    const std::string_view source = custom ? std::string_view{description.source} : builtin_shader;
    const auto vertex_shader =
        compile_pass_shader({source, custom ? description.vertex_entry : "vertex_main", false});
    const auto pixel_shader = compile_pass_shader(
        {source,
         custom
             ? description.pixel_entry
             : (description.program == shader_program::textured ? "textured_main" : "pixel_main"),
         true});
    auto output = std::make_shared<native_pipeline>();
    output->textured = pixel_shader.textured;
    output->root = root_signature(context, description.linear_sampling);
    const D3D12_INPUT_ELEMENT_DESC elements[]{{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                                               D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                                              {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
                                               D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                                              {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28,
                                               D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    static_assert(sizeof(vertex) == 36 && offsetof(vertex, color) == 12 &&
                  offsetof(vertex, uv) == 28);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = output->root.Get();
    pipeline.VS = {vertex_shader.code->GetBufferPointer(), vertex_shader.code->GetBufferSize()};
    pipeline.PS = {pixel_shader.code->GetBufferPointer(), pixel_shader.code->GetBufferSize()};
    pipeline.InputLayout = {elements, static_cast<UINT>(std::size(elements))};
    auto& blend = pipeline.BlendState.RenderTarget[0];
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blend.BlendEnable = description.blend != blend_mode::opaque;
    blend.SrcBlend =
        description.blend == blend_mode::straight_alpha ? D3D12_BLEND_SRC_ALPHA : D3D12_BLEND_ONE;
    blend.DestBlend =
        description.blend == blend_mode::opaque ? D3D12_BLEND_ZERO : D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = blend.DestBlend;
    blend.LogicOp = D3D12_LOGIC_OP_NOOP;
    pipeline.SampleMask = UINT_MAX;
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.DepthStencilState.DepthEnable = description.depth != depth_mode::disabled;
    pipeline.DepthStencilState.DepthWriteMask = description.depth == depth_mode::read_write
                                                    ? D3D12_DEPTH_WRITE_MASK_ALL
                                                    : D3D12_DEPTH_WRITE_MASK_ZERO;
    pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pipeline.DSVFormat =
        description.depth == depth_mode::disabled ? DXGI_FORMAT_UNKNOWN : DXGI_FORMAT_D32_FLOAT;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipeline.SampleDesc.Count = 1;
    check(context.gpu->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&output->state)),
          "create pass graphics pipeline");
    return output;
}
} // namespace atlas::renderer::detail
