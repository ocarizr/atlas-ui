/** @file @brief Custom shader ABI, in-memory compilation and pipeline lifetime contracts. */
#include "pass_test_support.hpp"

namespace atlas::renderer::tests
{
/// @brief Resource declarations outside the documented fixed ABI must fail compilation admission.
class shader_binding_test : public pass_test, public ::testing::WithParamInterface<const char*>
{
};

/// @brief Reflection rejects incompatible register classes, spaces and constant capacity.
TEST_P(shader_binding_test, rejects_unsupported_binding)
{
    pipeline_description description;
    description.program = shader_program::custom;
    description.source = GetParam();
    EXPECT_THROW(renderer.create_pipeline(description), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(
    fixed_abi, shader_binding_test,
    ::testing::Values("Texture2D<float4> image:register(t1); float4 vertex_main(float3 "
                      "p:POSITION):SV_Position{return float4(p,1);} float4 "
                      "pixel_main():SV_Target{return image.Load(int3(0,0,0));}",
                      "Texture2D<float4> image:register(t0,space1); float4 vertex_main(float3 "
                      "p:POSITION):SV_Position{return float4(p,1);} float4 "
                      "pixel_main():SV_Target{return image.Load(int3(0,0,0));}",
                      "cbuffer large:register(b0){float4 values[6];}; float4 vertex_main(float3 "
                      "p:POSITION):SV_Position{return float4(p,1)+values[5];} float4 "
                      "pixel_main():SV_Target{return 1;}",
                      "RWTexture2D<float4> image:register(u0); float4 vertex_main(float3 "
                      "p:POSITION):SV_Position{return float4(p,1);} float4 "
                      "pixel_main():SV_Target{image[int2(0,0)]=1;return 1;}"));

/// @brief The renderer never resolves shader includes from application storage.
TEST_F(pass_test, rejects_file_include_without_include_handler)
{
    pipeline_description description;
    description.program = shader_program::custom;
    description.source = "#include \"unavailable_shader.hlsl\"\n";
    EXPECT_THROW(renderer.create_pipeline(description), std::runtime_error);
}

/// @brief Valid custom source uses the same vertex inputs and ordered pass API.
TEST_F(pass_test, renders_custom_memory_shader)
{
    triangle();
    pipeline_description description;
    description.program = shader_program::custom;
    description.source = R"(
float4 vertex_main(float3 p:POSITION):SV_Position { return float4(p,1); }
float4 pixel_main():SV_Target { return float4(0,0,1,1); }
)";
    auto command = pass();
    command.draws[0].pipeline = renderer.create_pipeline(description);
    const std::array passes{command};
    renderer.submit(frame_plan{passes});
    EXPECT_EQ(pixel(8, 8), (std::array<unsigned, 4>{0, 0, 255, 255}));
}

/// @brief Renderer lifetime retains shared storage even when the resource facade is destroyed.
TEST(pass_lifetime, retains_store_facade_and_device)
{
    auto graphics = std::make_unique<device>(device_options{adapter_preference::software});
    auto resources = std::make_unique<resource_store>(*graphics);
    auto renderer = std::make_unique<pass_renderer>(*resources);
    pass_description pass;
    pass.color.target = resources->create_texture({16, 16, texture_role::color_target});
    const std::array passes{pass};
    resources.reset();
    const auto retained = detail::device_access::retain(*graphics);
    graphics.reset();
    renderer->submit(frame_plan{passes});
    renderer.reset();
    EXPECT_EQ(retained->errors(), 0U);
}
} // namespace atlas::renderer::tests
