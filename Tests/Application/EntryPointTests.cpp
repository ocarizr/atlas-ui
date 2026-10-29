#include <atlas/application/EntryPoint.hpp>
#include <gtest/gtest.h>

namespace atlas::application::tests
{
class ApplicationEntryPointTests : public testing::TestWithParam<ApplicationKind>
{
};

TEST_P(ApplicationEntryPointTests, RejectsUnknownOptions)
{
    char executable[] = "application";
    char option[] = "--unknown";
    char* arguments[] = {executable, option};
    EXPECT_EQ(run_application(GetParam(), 2, arguments), 2);
}

TEST_P(ApplicationEntryPointTests, StartsAndDrainsTheJobSystem)
{
    char executable[] = "application";
    char option[] = "--smoke-test";
    char* arguments[] = {executable, option};
    EXPECT_EQ(run_application(GetParam(), 2, arguments), 0);
}

INSTANTIATE_TEST_SUITE_P(Hosts, ApplicationEntryPointTests,
                         testing::Values(ApplicationKind::Game, ApplicationKind::Editor),
                         [](const testing::TestParamInfo<ApplicationKind>& info)
                         { return info.param == ApplicationKind::Game ? "Game" : "Editor"; });

class ToolEntryPointTests : public testing::TestWithParam<ToolKind>
{
};

TEST_P(ToolEntryPointTests, ReportsHelpWithoutPretendingToProcessContent)
{
    char executable[] = "tool";
    char option[] = "--help";
    char* arguments[] = {executable, option};
    EXPECT_EQ(run_tool(GetParam(), 2, arguments), 0);
}

TEST_P(ToolEntryPointTests, RejectsUnimplementedContentCommands)
{
    char executable[] = "tool";
    char* arguments[] = {executable};
    EXPECT_EQ(run_tool(GetParam(), 1, arguments), 2);
}

INSTANTIATE_TEST_SUITE_P(Tools, ToolEntryPointTests,
                         testing::Values(ToolKind::Cook, ToolKind::Package),
                         [](const testing::TestParamInfo<ToolKind>& info)
                         { return info.param == ToolKind::Cook ? "Cook" : "Package"; });
} // namespace atlas::application::tests
