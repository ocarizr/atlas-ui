/** @file @brief Compile and execute an XML-generated native factory with no runtime parser call. */
#include <atlas/mvvm_content/document.hpp>
#include <bit>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>

namespace atlas::generated
{
/// @brief Native factory generated and compiled from the authored fixture.
std::unique_ptr<mvvm::view_model>
make_fixture(mvvm::runtime_limits limits = {},
             std::shared_ptr<std::pmr::memory_resource> memory = {},
             const diagnostics::services& destinations = {});
///< Test declaration for the native function generated from data/hud.xml at build time.
/// @brief Native factory containing a text default beyond the MSVC single-literal limit.
std::unique_ptr<mvvm::view_model>
make_large_fixture(mvvm::runtime_limits limits = {},
                   std::shared_ptr<std::pmr::memory_resource> memory = {},
                   const diagnostics::services& destinations = {});
} // namespace atlas::generated

static_assert(std::is_same_v<decltype(atlas::generated::make_fixture()),
                             std::unique_ptr<atlas::mvvm::view_model>>,
              "Generated factories must return transferable model ownership without copying the "
              "mutable facade");

namespace atlas::mvvm_content::tests
{
/// @brief Legal long XML defaults must remain compilable and byte-exact in native factories.
TEST(mvvm_codegen, CompiledFactoryPreservesLongTextDefault)
{
    auto generated = atlas::generated::make_large_fixture();
    EXPECT_EQ(generated->text(generated->description().field("text")), std::string(65536, 'x'));
}

/// @brief Generated and parsed construction share types/defaults and action/event protocols.
TEST(mvvm_codegen, CompiledFactoryMatchesInterpretedModelAndSharesItsSchema)
{
    std::ifstream stream{ATLAS_MVVM_XML_FIXTURE, std::ios::binary};
    ASSERT_TRUE(stream);
    const std::string xml{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    mvvm::view_model interpreted{mvvm::compile(parse_xml(xml))};
    auto generated = atlas::generated::make_fixture();
    std::shared_ptr<mvvm::view_model> another = atlas::generated::make_fixture();
    for (const auto& field : interpreted.description().describe().fields)
    {
        EXPECT_EQ(interpreted.read(interpreted.description().field(field.name)),
                  generated->read(generated->description().field(field.name)));
    }

    EXPECT_EQ(generated->description().identity(), another->description().identity());
    EXPECT_EQ(std::bit_cast<std::uint64_t>(
                  generated->number(generated->description().field("negative_zero"))),
              std::bit_cast<std::uint64_t>(-0.0));
    generated->begin_frame(1);
    generated->set(generated->description().field("xp"), std::int64_t{11});
    generated->flush();
    EXPECT_EQ(another->integer(another->description().field("xp")), 7);
}

/// @brief Source escaping and function identifiers cannot inject executable statements.
TEST(mvvm_codegen, RejectsSourceInjectionThroughFactoryNames)
{
    EXPECT_THROW(generate_factory_cpp({"hud"}, "make_x();evil()"), std::invalid_argument);
    EXPECT_THROW(generate_factory_cpp({"hud"}, "make__reserved"), std::invalid_argument);
}
} // namespace atlas::mvvm_content::tests
