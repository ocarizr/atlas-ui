/**
 * @file
 * @brief Behavioral coverage for logging with isolated scenario ownership and cleanup.
 */

#include <atlas/diagnostics/logging.hpp>
#include <gtest/gtest.h>

#include <mutex>
#include <stdexcept>
#include <string>

namespace atlas::diagnostics::tests
{
/// @brief Scenario state for memory output, isolated per test and released through RAII.
class memory_output : public log_output
{
  public:
    /// @brief Record or deliberately fail output as selected by this isolated logging scenario.
    void append(std::string_view text) override
    {
        content.append(text);
    }

    /// @brief Acknowledge or deliberately fail the scenario's controlled output flush.
    void flush() override
    {
        ++flushes;
    }

    /// @brief Report whether the injected output implements storage-owned archive rotation.
    bool supports_rotation() const noexcept override
    {
        return true;
    }

    /// @brief Observe the requested archive-retention contract without opening application files.
    void rotate(std::size_t retained) override
    {
        EXPECT_EQ(retained, 2U);
        ++rotations;
        content.clear();
    }

    /// @brief Scenario-owned content used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::string content;
    /// @brief Scenario-owned flushes used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    int flushes{0};
    /// @brief Scenario-owned rotations used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    int rotations{0};
};

/// @brief Scenario state for logging tests, isolated per test and released through RAII.
class logging_tests : public testing::TestWithParam<bool>
{
};

/// @brief Verify categories filter and flush drains owned messages.
TEST_P(logging_tests, categories_filter_and_flush_drains_owned_messages)
{
    auto output = std::make_shared<memory_output>();
    logging_options options;
    options.asynchronous = GetParam();
    options.category_levels.emplace_back("jobs", log_level::debug);
    logger log{options, output};
    EXPECT_TRUE(log.enabled(log_level::debug, "jobs"));
    EXPECT_FALSE(log.enabled(log_level::debug, "other"));
    EXPECT_FALSE(log.write(log_level::debug, "other", "hidden"));
    EXPECT_TRUE(log.write(log_level::debug, "jobs", std::string{"owned message"}));
    EXPECT_TRUE(log.flush());
    EXPECT_NE(output->content.find("[jobs] owned message"), std::string::npos);
    EXPECT_EQ(output->content.find("hidden"), std::string::npos);
    EXPECT_EQ(log.statistics().written, 1U);
    log.shutdown();
    EXPECT_FALSE(log.write(log_level::error, "jobs", "closed"));
}

INSTANTIATE_TEST_SUITE_P(modes, logging_tests, testing::Bool());

/// @brief Verify output failures are counted without throwing into jobs.
TEST(logging_failure_tests, output_failures_are_counted_without_throwing_into_jobs)
{
    /// @brief Scenario state for failing output, isolated per test and released through RAII.
    class failing_output final : public log_output
    {
        /// @brief Record or deliberately fail output as selected by this isolated logging scenario.
        void append(std::string_view) override
        {
            throw std::runtime_error{"write failed"};
        }

        /// @brief Acknowledge or deliberately fail the scenario's controlled output flush.
        void flush() override
        {
            throw std::runtime_error{"flush failed"};
        }
    };

    logger log{{}, std::make_shared<failing_output>()};
    EXPECT_TRUE(log.write(log_level::error, "jobs", "failure"));
    EXPECT_FALSE(log.flush());
    EXPECT_GE(log.statistics().errors, 2U);
}

/// @brief Verify invalid capacity and unsupported rotation are rejected.
TEST(logging_failure_tests, invalid_capacity_and_unsupported_rotation_are_rejected)
{
    logging_options options;
    options.queue_capacity = 0;
    EXPECT_THROW((logger{options}), std::invalid_argument);
    options.queue_capacity = 8;
    options.rotation_bytes = 32;
    EXPECT_THROW((logger{options}), std::invalid_argument);
}

/// @brief Verify non standard output exceptions do not escape the async backend.
TEST(logging_failure_tests, non_standard_output_exceptions_do_not_escape_the_async_backend)
{
    /// @brief Scenario state for failing output, isolated per test and released through RAII.
    class failing_output final : public log_output
    {
        /// @brief Record or deliberately fail output as selected by this isolated logging scenario.
        void append(std::string_view) override
        {
            throw 42;
        }

        /// @brief Acknowledge or deliberately fail the scenario's controlled output flush.
        void flush() override
        {
        }
    };

    logger log{{}, std::make_shared<failing_output>()};
    EXPECT_TRUE(log.write(log_level::error, "test", "failure"));
    (void)log.flush();
    EXPECT_EQ(log.statistics().errors, 1U);
    EXPECT_EQ(log.statistics().written, 0U);
}

/// @brief Verify rotation is delegated to storage before exceeding the budget.
TEST(logging_rotation_tests, rotation_is_delegated_to_storage_before_exceeding_the_budget)
{
    auto output = std::make_shared<memory_output>();
    logging_options options;
    options.asynchronous = false;
    options.rotation_bytes = 40;
    options.retained_files = 2;
    logger log{options, output};
    EXPECT_TRUE(log.write(log_level::info, "jobs", "first message"));
    EXPECT_TRUE(log.write(log_level::info, "jobs", "second message"));
    EXPECT_TRUE(log.flush());
    EXPECT_EQ(output->rotations, 1);
}

/// @brief Verify structured records are available without parsing formatted text.
TEST(logging_record_tests, structured_records_are_available_without_parsing_formatted_text)
{
    /// @brief Scenario state for structured output, isolated per test and released through RAII.
    class structured_output final : public memory_output
    {
      public:
        /// @brief Capture structured borrowed fields by copying the scenario's record.
        void record(const log_record& entry) override
        {
            EXPECT_EQ(entry.category, "jobs");
            EXPECT_EQ(entry.level, log_level::error);
            message = entry.message;
            memory_output::record(entry);
        }

        /// @brief Scenario-owned message used to observe behavior and cleanup without sharing
        /// mutable state between tests.
        std::string message;
    };

    auto output = std::make_shared<structured_output>();
    logger log{{}, output};
    EXPECT_TRUE(log.write(log_level::error, "jobs", "original"));
    EXPECT_TRUE(log.flush());
    EXPECT_EQ(output->message, "original");
}
} // namespace atlas::diagnostics::tests
