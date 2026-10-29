#include <Windows.h>
#include <oaidl.h>
#include <unknwn.h>
#include <wrl/client.h>

#include <dav1d/dav1d.h>
#include <directx-dxc/dxcapi.h>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include <mkvparser/mkvparser.h>
#include <opus/opus.h>
#include <pugixml.hpp>
#include <yaml-cpp/yaml.h>

TEST(DependencyTests, CreatesAndQueriesAnEnTTEntity)
{
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<int>(entity, 42);
    EXPECT_EQ(registry.get<int>(entity), 42);
}

TEST(DependencyTests, ParsesXmlContentAndYamlConfiguration)
{
    pugi::xml_document document;
    ASSERT_TRUE(document.load_string("<UI version=\"1\"/>"));
    EXPECT_EQ(document.child("UI").attribute("version").as_int(), 1);
    const auto config = YAML::Load("jobs: {workers: 2}");
    EXPECT_EQ(config["jobs"]["workers"].as<int>(), 2);
}

TEST(DependencyTests, OpensAndClosesThePublicAv1Decoder)
{
    Dav1dSettings settings{};
    dav1d_default_settings(&settings);
    settings.n_threads = 1;
    Dav1dContext* context = nullptr;
    ASSERT_EQ(dav1d_open(&context, &settings), 0);
    dav1d_close(&context);
    EXPECT_EQ(context, nullptr);
    int major = 0;
    int minor = 0;
    int build = 0;
    int revision = 0;
    mkvparser::GetVersion(major, minor, build, revision);
    EXPECT_GE(major, 1);
    EXPECT_NE(dav1d_version(), nullptr);
}

TEST(DependencyTests, InitializesThePublicOpusDecoder)
{
    const int size = opus_decoder_get_size(2);
    EXPECT_GT(size, 0);
    EXPECT_NE(opus_get_version_string(), nullptr);
}

TEST(DependencyTests, CreatesTheDirectXShaderCompilerUtilities)
{
    Microsoft::WRL::ComPtr<IDxcUtils> utilities;
    const auto result = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utilities));
    EXPECT_GE(result, 0);
    EXPECT_NE(utilities.Get(), nullptr);
}
