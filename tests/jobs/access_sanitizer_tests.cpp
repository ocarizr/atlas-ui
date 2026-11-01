/**
 * @file
 * @brief Behavioral coverage for access sanitizer with isolated scenario ownership and cleanup.
 */

#include "job_system_fixture.hpp"
#include <atlas/jobs/job_sanitizer.hpp>

#include <array>
#include <optional>

namespace atlas::jobs::tests
{
/// @brief Scenario state for access case, isolated per test and released through RAII.
struct access_case
{
    /// @brief Scenario-owned first used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    access_mode first;
    /// @brief Scenario-owned second used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    access_mode second;
    /// @brief Scenario-owned offset used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::size_t offset;
    /// @brief Scenario-owned conflict used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    bool conflict;
    /// @brief Scenario-owned warning used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    bool warning;
};

/// @brief Print a readable parameter-case description for GoogleTest failure diagnostics.
std::ostream& operator<<(std::ostream& output, const access_case& value)
{
    return output << (value.first == access_mode::read ? "read" : "write") << "/"
                  << (value.second == access_mode::read ? "read" : "write")
                  << ", offset=" << value.offset;
}

/// @brief Scenario state for access sanitizer tests, isolated per test and released through RAII.
class access_sanitizer_tests : public job_system_fixture,
                               public testing::WithParamInterface<access_case>
{
};

/// @brief Verify reports live conflicts and cache line risks before access.
TEST_P(access_sanitizer_tests, reports_live_conflicts_and_cache_line_risks_before_access)
{
    alignas(128) std::array<int, 64> values{};
    const auto scenario = GetParam();

    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned region used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        memory_access region;
        /// @brief Scenario-owned gate used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        signal_gate* gate;
        /// @brief Scenario-owned started used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::shared_ptr<std::promise<void>> started;
    };

    auto started = std::make_shared<std::promise<void>>();
    auto ready = started->get_future();
    auto first = system.dispatcher().run(
        "first", input{{values.data(), sizeof(int), scenario.first}, &gate, started},
        [](job_context& context, input& data)
        {
            auto guard = context.access(data.region);
            data.started->set_value();
            data.gate->wait();
        });
    EXPECT_EQ(ready.wait_for(std::chrono::seconds{3}), std::future_status::ready);
    auto second = system.dispatcher().run(
        "second", memory_access{values.data() + scenario.offset, sizeof(int), scenario.second},
        [](job_context& context, memory_access& region)
        {
            auto guard = context.access(region);
        });
    const auto enabled = system.sanitizer_report().enabled;
    if (enabled && scenario.conflict)
    {
        EXPECT_THROW(second.wait(), job_error);
        EXPECT_THROW(std::rethrow_exception(second.exception()), job_sanitizer_error);
    }
    else
    {
        EXPECT_NO_THROW(second.wait());
    }

    gate.open();
    first.wait();
    const auto report = system.sanitizer_report();
    const auto expected = enabled && (scenario.conflict || scenario.warning);
    ASSERT_EQ(report.diagnostics.size(), expected ? 1U : 0U);
    if (expected)
    {
        const auto& event = report.diagnostics.front();
        EXPECT_EQ(event.job_name, "second");
        EXPECT_EQ(event.other_job_name, "first");
        EXPECT_EQ(event.kind, scenario.conflict ? diagnostic_kind::conflicting_access
                                                : diagnostic_kind::possible_false_sharing);
    }
}

INSTANTIATE_TEST_SUITE_P(
    access_pairs, access_sanitizer_tests,
    testing::Values(access_case{access_mode::read, access_mode::read, 0, false, false},
                    access_case{access_mode::read, access_mode::write, 0, true, false},
                    access_case{access_mode::write, access_mode::read, 0, true, false},
                    access_case{access_mode::write, access_mode::write, 0, true, false},
                    access_case{access_mode::write, access_mode::write, 1, false, true},
                    access_case{access_mode::read, access_mode::read, 1, false, false},
                    access_case{access_mode::write, access_mode::write, 32, false, false}));

/// @brief Verify access guards release on exceptions and allow sequenced writes.
TEST_F(job_system_fixture, access_guards_release_on_exceptions_and_allow_sequenced_writes)
{
    std::array<int, 1> value{};
    auto region = write_access(std::span{value});
    auto first = system.dispatcher().run("throw", region,
                                         [](job_context& context, memory_access& data)
                                         {
                                             auto guard = context.access(data);
                                             throw 1;
                                         });
    EXPECT_THROW(first.wait(), job_error);
    auto next = system.dispatcher().run("reuse", region,
                                        [](job_context& context, memory_access& data)
                                        {
                                            auto guard = context.access({data});
                                        });
    next.wait();
    EXPECT_TRUE(system.sanitizer_report().diagnostics.empty());
}

/// @brief Verify escaped guards fail the task and are retired before successors.
TEST_F(job_system_fixture, escaped_guards_fail_the_task_and_are_retired_before_successors)
{
    /// @brief Scenario state for input, isolated per test and released through RAII.
    struct input
    {
        /// @brief Scenario-owned values used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::span<int> values;
        /// @brief Scenario-owned saved used to observe behavior and cleanup without sharing mutable
        /// state between tests.
        std::shared_ptr<std::optional<access_guard>> saved;
    };

    std::array<int, 1> value{};
    auto saved = std::make_shared<std::optional<access_guard>>();
    auto root =
        system.dispatcher().run("escape", input{value, saved},
                                [](job_context& context, input& data)
                                {
                                    data.saved->emplace(context.access(write_access(data.values)));
                                });
    if (system.sanitizer_report().enabled)
    {
        EXPECT_THROW(root.wait(), job_error);
    }
    else
    {
        root.wait();
    }

    saved->reset();
    system.dispatcher()
        .run("reuse", std::span{value},
             [](job_context& context, auto& data)
             {
                 auto guard = context.access(write_access(data));
             })
        .wait();
    const auto report = system.sanitizer_report();
    ASSERT_EQ(report.diagnostics.size(), report.enabled ? 1U : 0U);
    if (report.enabled)
    {
        EXPECT_EQ(report.diagnostics.front().kind, diagnostic_kind::escaped_access);
    }
}

/// @brief Verify empty accesses are valid and invalid regions do not publish guards.
TEST_F(job_system_fixture, empty_accesses_are_valid_and_invalid_regions_do_not_publish_guards)
{
    auto root = system.dispatcher().run(
        "invalid",
        [](job_context& context)
        {
            auto empty = context.access(read_access(std::span<const int>{}));
            EXPECT_THROW((void)context.access(memory_access{nullptr, 1, access_mode::write}),
                         std::invalid_argument);
            auto again = context.access({});
        });
    root.wait();
}
} // namespace atlas::jobs::tests
