/** @file @brief Builtin in-memory shaders exercise the foundation graphics pipeline. */
#include "native_renderer.hpp"
#include <cstring>
#include <dxcapi.h>

namespace atlas::renderer::detail
{
namespace
{
/// @brief Compile one startup shader without accessing application storage.
com_ptr<IDxcBlob> compile(IDxcCompiler3& compiler, const char* source, const wchar_t* entry,
                          const wchar_t* profile)
{
    DxcBuffer input{source, std::strlen(source), DXC_CP_UTF8};
    const wchar_t* arguments[]{L"-E", entry, L"-T", profile, L"-Ges", L"-WX"};
    com_ptr<IDxcResult> result;
    check(compiler.Compile(&input, arguments, static_cast<UINT32>(std::size(arguments)), nullptr,
                           IID_PPV_ARGS(&result)),
          "compile builtin shader");
    HRESULT status = E_FAIL;
    check(result->GetStatus(&status), "query shader status");
    if (FAILED(status))
    {
        com_ptr<IDxcBlobUtf8> errors;
        check(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr),
              "read shader diagnostics");
        throw std::runtime_error(errors ? errors->GetStringPointer()
                                        : "builtin shader compilation failed");
    }

    com_ptr<IDxcBlob> code;
    check(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&code), nullptr), "read shader object");
    return code;
}
} // namespace

/// @brief Throw an operation-specific diagnostic before continuing after a native failure.
void create_triangle_pipeline(native_device& context)
{
    static constexpr const char* shader = R"(
struct output_vertex { float4 position : SV_Position; float4 color : COLOR; };
output_vertex vertex_main(uint index : SV_VertexID) {
    const float2 positions[3] = {float2(0,0.7),float2(0.7,-0.7),float2(-0.7,-0.7)};
    const float4 colors[3] = {float4(1,0,0,1),float4(0,1,0,1),float4(0,0,1,1)};
    output_vertex output; output.position = float4(positions[index],0,1); output.color = colors[index]; return output;
}
float4 pixel_main(output_vertex input) : SV_Target { return input.color; }
)";
    com_ptr<IDxcCompiler3> compiler;
    check(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)), "create shader compiler");
    const auto vertex = compile(*compiler.Get(), shader, L"vertex_main", L"vs_6_0");
    const auto pixel = compile(*compiler.Get(), shader, L"pixel_main", L"ps_6_0");
    D3D12_ROOT_SIGNATURE_DESC root_description{};
    root_description.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    com_ptr<ID3DBlob> root_blob;
    com_ptr<ID3DBlob> errors;
    check(D3D12SerializeRootSignature(&root_description, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob,
                                      &errors),
          "serialize root signature");
    check(context.gpu->CreateRootSignature(0, root_blob->GetBufferPointer(),
                                           root_blob->GetBufferSize(), IID_PPV_ARGS(&context.root)),
          "create root signature");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = context.root.Get();
    pipeline.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    pipeline.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    pipeline.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    pipeline.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pipeline.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pipeline.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    pipeline.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pipeline.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    pipeline.SampleMask = UINT_MAX;
    pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable = TRUE;
    pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipeline.SampleDesc.Count = 1;
    check(context.gpu->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&context.pipeline)),
          "create triangle pipeline");
}
} // namespace atlas::renderer::detail
