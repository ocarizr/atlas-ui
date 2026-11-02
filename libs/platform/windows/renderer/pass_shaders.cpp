/** @file @brief In-memory DXC compilation and fixed resource-binding ABI validation. */
#include "native_passes.hpp"
#include <d3d12shader.h>

namespace atlas::renderer::detail
{
namespace
{
/// @brief Reject shader resources outside b0 constants and pixel t0/s0 bindings.
bool reflect(IDxcResult& result, bool pixel)
{
    com_ptr<IDxcBlob> metadata;
    check(result.GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&metadata), nullptr),
          "read shader reflection");
    com_ptr<IDxcUtils> utilities;
    check(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utilities)),
          "create reflection utilities");
    DxcBuffer input{metadata->GetBufferPointer(), metadata->GetBufferSize(), 0};
    com_ptr<ID3D12ShaderReflection> reflection;
    check(utilities->CreateReflection(&input, IID_PPV_ARGS(&reflection)), "reflect shader ABI");
    D3D12_SHADER_DESC description{};
    check(reflection->GetDesc(&description), "describe shader bindings");
    bool textured = false;
    for (UINT index = 0; index < description.BoundResources; ++index)
    {
        D3D12_SHADER_INPUT_BIND_DESC binding{};
        check(reflection->GetResourceBindingDesc(index, &binding), "describe shader binding");
        bool allowed = false;
        if (binding.Space == 0 && binding.BindPoint == 0 && binding.BindCount == 1)
        {
            if (binding.Type == D3D_SIT_CBUFFER)
            {
                D3D12_SHADER_BUFFER_DESC buffer{};
                check(reflection->GetConstantBufferByName(binding.Name)->GetDesc(&buffer),
                      "describe draw constants");
                allowed = buffer.Size <= 80;
            }
            else if (pixel && binding.Type == D3D_SIT_TEXTURE &&
                     binding.Dimension == D3D_SRV_DIMENSION_TEXTURE2D &&
                     binding.ReturnType == D3D_RETURN_TYPE_FLOAT)
            {
                allowed = true;
                textured = true;
            }
            else if (pixel && binding.Type == D3D_SIT_SAMPLER &&
                     (binding.uFlags & D3D_SIF_COMPARISON_SAMPLER) == 0)
            {
                allowed = true;
            }
        }

        if (!allowed)
        {
            throw std::invalid_argument("shader binding exceeds the fixed b0/t0/s0 ABI");
        }
    }

    return textured;
}
} // namespace

/// @brief Compile trusted UTF-8 HLSL without a file include handler, then validate resource
/// bindings.
compiled_shader compile_pass_shader(shader_input shader)
{
    com_ptr<IDxcCompiler3> compiler;
    check(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)),
          "create pass shader compiler");
    const std::wstring wide_entry(shader.entry.begin(), shader.entry.end());
    const wchar_t* arguments[]{
        L"-E",  wide_entry.c_str(), L"-T", shader.pixel ? L"ps_6_0" : L"vs_6_0", L"-Ges", L"-WX",
        L"-Zpr"};
    DxcBuffer input{shader.source.data(), shader.source.size(), DXC_CP_UTF8};
    com_ptr<IDxcResult> result;
    check(compiler->Compile(&input, arguments, static_cast<UINT32>(std::size(arguments)), nullptr,
                            IID_PPV_ARGS(&result)),
          "compile pass shader");
    HRESULT status = E_FAIL;
    check(result->GetStatus(&status), "query pass shader status");
    if (FAILED(status))
    {
        com_ptr<IDxcBlobUtf8> errors;
        check(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr),
              "read pass shader errors");
        throw std::runtime_error(errors ? errors->GetStringPointer() : "shader compilation failed");
    }

    compiled_shader output;
    output.textured = reflect(*result.Get(), shader.pixel);
    check(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&output.code), nullptr),
          "read pass shader object");
    return output;
}
} // namespace atlas::renderer::detail
