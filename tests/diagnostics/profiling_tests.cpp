#include <atlas/diagnostics/profiling.hpp>
#include <gtest/gtest.h>

#include <sstream>
#include <thread>
#include <vector>

namespace atlas::diagnostics::tests
{
TEST(profiling_tests, durations_and_scopes_export_chrome_events_with_escaped_text)
{
    profiler capture;
    const auto start = profiler::clock::now();
    EXPECT_TRUE(
        capture.duration("quote\"\n", "jobs", start, start + std::chrono::nanoseconds{1500}));
    {
        profile_scope scope{capture, "scope", "test"};
    }
    EXPECT_EQ(capture.statistics().recorded, 2U);
    std::ostringstream output;
    capture.write_chrome_trace(output);
    EXPECT_NE(output.str().find("\"traceEvents\":["), std::string::npos);
    EXPECT_NE(output.str().find("quote\\\"\\n"), std::string::npos);
    EXPECT_NE(output.str().find("\"dur\":1.500"), std::string::npos);
    EXPECT_NE(output.str().find("\"ph\":\"X\""), std::string::npos);
}

TEST(profiling_tests, capacity_and_invalid_intervals_are_observable)
{
    profiling_options options;
    options.max_events = 1;
    profiler capture{options};
    const auto now = profiler::clock::now();
    EXPECT_FALSE(capture.duration("invalid", "test", now, now - std::chrono::nanoseconds{1}));
    EXPECT_TRUE(capture.duration("first", "test", now, now));
    EXPECT_FALSE(capture.duration("second", "test", now, now));
    EXPECT_EQ(capture.statistics().dropped, 1U);
    EXPECT_EQ(capture.statistics().errors, 1U);
    capture.clear();
    EXPECT_EQ(capture.statistics().recorded, 0U);
}

TEST(profiling_tests, concurrent_producers_use_distinct_tracks_and_bounded_storage)
{
    profiler capture;
    std::vector<std::jthread> producers;
    producers.reserve(4);
    for (int index = 0; index < 4; ++index)
    {
        producers.emplace_back(
            [&]
            {
                for (int event = 0; event < 32; ++event)
                {
                    profile_scope scope{capture, "work", "test"};
                }
            });
    }
    producers.clear();
    EXPECT_EQ(capture.statistics().recorded, 128U);
    EXPECT_EQ(capture.statistics().dropped, 0U);
}
} // namespace atlas::diagnostics::tests
