#include <atlas/diagnostics/logging.hpp>
#include <gtest/gtest.h>

#include <mutex>
#include <stdexcept>
#include <string>

namespace atlas::diagnostics::tests
{
class memory_output : public log_output
{
  public:
    void append(std::string_view text) override
    {
        content.append(text);
    }
    void flush() override
    {
        ++flushes;
    }
    bool supports_rotation() const noexcept override
    {
        return true;
    }
    void rotate(std::size_t retained) override
    {
        EXPECT_EQ(retained, 2U);
        ++rotations;
        content.clear();
    }
    std::string content;
    int flushes{0};
    int rotations{0};
};

class logging_tests : public testing::TestWithParam<bool>
{
};

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

TEST(logging_failure_tests, output_failures_are_counted_without_throwing_into_jobs)
{
    class failing_output final : public log_output
    {
        void append(std::string_view) override
        {
            throw std::runtime_error{"write failed"};
        }
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

TEST(logging_failure_tests, invalid_capacity_and_unsupported_rotation_are_rejected)
{
    logging_options options;
    options.queue_capacity = 0;
    EXPECT_THROW((logger{options}), std::invalid_argument);
    options.queue_capacity = 8;
    options.rotation_bytes = 32;
    EXPECT_THROW((logger{options}), std::invalid_argument);
}

TEST(logging_failure_tests, non_standard_output_exceptions_do_not_escape_the_async_backend)
{
    class failing_output final : public log_output
    {
        void append(std::string_view) override
        {
            throw 42;
        }
        void flush() override {}
    };
    logger log{{}, std::make_shared<failing_output>()};
    EXPECT_TRUE(log.write(log_level::error, "test", "failure"));
    (void)log.flush();
    EXPECT_EQ(log.statistics().errors, 1U);
    EXPECT_EQ(log.statistics().written, 0U);
}

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

TEST(logging_record_tests, structured_records_are_available_without_parsing_formatted_text)
{
    class structured_output final : public memory_output
    {
      public:
        void record(const log_record& entry) override
        {
            EXPECT_EQ(entry.category, "jobs");
            EXPECT_EQ(entry.level, log_level::error);
            message = entry.message;
            memory_output::record(entry);
        }
        std::string message;
    };
    auto output = std::make_shared<structured_output>();
    logger log{{}, output};
    EXPECT_TRUE(log.write(log_level::error, "jobs", "original"));
    EXPECT_TRUE(log.flush());
    EXPECT_EQ(output->message, "original");
}
} // namespace atlas::diagnostics::tests
