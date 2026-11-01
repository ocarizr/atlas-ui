/**
 * @file
 * @brief Behavioral coverage for dependency with isolated scenario ownership and cleanup.
 */

#include <dav1d/dav1d.h>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include <mkvparser/mkvparser.h>
#include <opus/opus.h>
#include <pugixml.hpp>
#include <yaml-cpp/yaml.h>

/// @brief Verify creates and queries an en tt entity.
TEST(dependency_tests, creates_and_queries_an_en_tt_entity)
{
    entt::registry registry;
    const auto entity = registry.create();
    registry.emplace<int>(entity, 42);
    EXPECT_EQ(registry.get<int>(entity), 42);
}

/// @brief Verify parses xml content and yaml configuration.
TEST(dependency_tests, parses_xml_content_and_yaml_configuration)
{
    pugi::xml_document document;
    ASSERT_TRUE(document.load_string("<UI version=\"1\"/>"));
    EXPECT_EQ(document.child("UI").attribute("version").as_int(), 1);
    const auto config = YAML::Load("jobs: {workers: 2}");
    EXPECT_EQ(config["jobs"]["workers"].as<int>(), 2);
}

/// @brief Verify opens and closes the public av1 decoder.
TEST(dependency_tests, opens_and_closes_the_public_av1_decoder)
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

/// @brief Verify initializes the public opus decoder.
TEST(dependency_tests, initializes_the_public_opus_decoder)
{
    const int size = opus_decoder_get_size(2);
    EXPECT_GT(size, 0);
    EXPECT_NE(opus_get_version_string(), nullptr);
}
