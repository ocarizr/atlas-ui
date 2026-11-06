/** @file @brief XML/editor factory processing exposes optional diagnostic scopes. */
#include <atlas/mvvm_content/document.hpp>
#include <gtest/gtest.h>
#include <sstream>

namespace atlas::mvvm_content::tests
{
/// @brief Content processing captures CPU scopes without any implicit file/profiler destination.
TEST(mvvm_content, CapturesXmlAndCodegenScopesWhenEnabled)
{
    auto profile = std::make_shared<diagnostics::profiler>();
    const diagnostics::services services{{}, profile};
    const auto definition = parse_xml("<ViewModel version='1' name='model'/>", {}, services);
    EXPECT_FALSE(write_xml(definition, {}, services).empty());
    EXPECT_FALSE(generate_factory_cpp(definition, "make_model", {}, services).empty());
    std::ostringstream trace;
    profile->write_chrome_trace(trace);
    for (const auto name : {"mvvm.xml.read", "mvvm.xml.write", "mvvm.codegen"})
    {
        EXPECT_EQ(trace.str().find(name) != std::string::npos, diagnostics::instrumentation_enabled)
            << name;
    }

    EXPECT_EQ(profile->statistics().errors, 0);
}
} // namespace atlas::mvvm_content::tests
