#include <atlas/application/entry_point.hpp>
#include <gtest/gtest.h>

namespace atlas::application::tests
{
class application_entry_point_tests : public testing::TestWithParam<application_kind>
{
};

TEST_P(application_entry_point_tests, rejects_unknown_options)
{
    char executable[] = "application";
    char option[] = "--unknown";
    char* arguments[] = {executable, option};
    EXPECT_EQ(run_application(GetParam(), 2, arguments), 2);
}

TEST_P(application_entry_point_tests, starts_and_drains_the_job_system)
{
    char executable[] = "application";
    char option[] = "--smoke-test";
    char* arguments[] = {executable, option};
    EXPECT_EQ(run_application(GetParam(), 2, arguments), 0);
}

INSTANTIATE_TEST_SUITE_P(hosts, application_entry_point_tests,
                         testing::Values(application_kind::game, application_kind::editor),
                         [](const testing::TestParamInfo<application_kind>& info)
                         { return info.param == application_kind::game ? "game" : "editor"; });

class tool_entry_point_tests : public testing::TestWithParam<tool_kind>
{
};

TEST_P(tool_entry_point_tests, reports_help_without_pretending_to_process_content)
{
    char executable[] = "tool";
    char option[] = "--help";
    char* arguments[] = {executable, option};
    EXPECT_EQ(run_tool(GetParam(), 2, arguments), 0);
}

TEST_P(tool_entry_point_tests, rejects_unimplemented_content_commands)
{
    char executable[] = "tool";
    char* arguments[] = {executable};
    EXPECT_EQ(run_tool(GetParam(), 1, arguments), 2);
}

INSTANTIATE_TEST_SUITE_P(tools, tool_entry_point_tests,
                         testing::Values(tool_kind::cook, tool_kind::package),
                         [](const testing::TestParamInfo<tool_kind>& info)
                         { return info.param == tool_kind::cook ? "cook" : "package"; });
} // namespace atlas::application::tests
