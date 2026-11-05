/** @file @brief Scalar defaults, schema identity, validation and thread ownership invariants. */
#include <array>
#include <atlas/mvvm/view_model.hpp>
#include <future>
#include <gtest/gtest.h>
#include <limits>

namespace atlas::mvvm::tests
{
/// @brief Schema handles can be reused across instances while mutable columns remain isolated.
TEST(mvvm_schema, TypedDefaultsAndSharedSlotsKeepInstancesIndependent)
{
    const auto schema = compile({"hud",
                                 {{"enabled", true},
                                  {"count", std::int64_t{-9}},
                                  {"health", 0.25},
                                  {"title", std::string{"status"}}}});
    view_model first{schema}, second{schema};
    const auto count = schema.field("count");
    EXPECT_TRUE(first.boolean(schema.field("enabled")));
    EXPECT_EQ(first.integer(count), -9);
    EXPECT_DOUBLE_EQ(first.number(schema.field("health")), 0.25);
    EXPECT_EQ(first.text(schema.field("title")), "status");
    first.begin_frame(1);
    first.set(count, std::int64_t{10});
    first.flush();
    EXPECT_EQ(second.integer(count), -9);
    EXPECT_THROW(first.number(count), std::invalid_argument);
    const auto foreign = compile(schema.describe());
    EXPECT_THROW(first.integer(foreign.field("count")), std::invalid_argument);
    EXPECT_THROW(schema.field("missing"), std::invalid_argument);
    EXPECT_THROW(schema.type({schema.identity(), 999}), std::invalid_argument);
}

/// @brief Authoring metadata never silently accepts duplicate names or malformed defaults.
TEST(mvvm_schema, RejectsDuplicateNamesInvalidTypesAndUnboundedDefinitions)
{
    EXPECT_THROW(compile({"bad name"}), std::invalid_argument);
    EXPECT_THROW(compile({"model", {{"x", false}, {"x", true}}}), std::invalid_argument);
    EXPECT_THROW(
        compile({"model", {}, {{"go", {{"p", value_type::integer}, {"p", value_type::number}}}}}),
        std::invalid_argument);
    EXPECT_THROW(compile({"model", {{"x", std::numeric_limits<double>::infinity()}}}),
                 std::invalid_argument);
    EXPECT_THROW(compile({"model", {{"x", std::string{"long"}}}}, {.max_text_bytes = 3}),
                 std::overflow_error);
    EXPECT_THROW(compile({"model", {{"x", false}, {"y", true}}}, {.max_fields = 1}),
                 std::overflow_error);
    EXPECT_THROW(compile({"model"}, {.max_fields = 0}), std::invalid_argument);
}

/// @brief Invalid demands leave admission counters and committed state untouched.
TEST(mvvm_schema, RejectsBadDemandsBeforeQueuePublication)
{
    const auto schema =
        compile({"model", {{"x", 0.0}}, {{"go", {{"n", value_type::number}}}}, {{"done"}}});
    view_model model{schema};
    const auto x = schema.field("x");
    EXPECT_THROW(model.set(x, 1.0), std::logic_error);
    model.begin_frame(1);
    EXPECT_THROW(model.set(x, true), std::invalid_argument);
    EXPECT_THROW(model.set(x, std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
    EXPECT_THROW(model.invoke(schema.action("go")), std::invalid_argument);
    EXPECT_THROW(model.invoke({schema.identity(), 999}), std::invalid_argument);
    EXPECT_THROW(model.emit({0, 0}), std::invalid_argument);
    EXPECT_EQ(model.inspect().commands, 0);
    model.flush();
    EXPECT_DOUBLE_EQ(model.number(x), 0);
    EXPECT_THROW(model.begin_frame(1), std::invalid_argument);
}

/// @brief Only immutable schemas may be observed by unrelated worker threads.
TEST(mvvm_schema, ForeignThreadAccessRejectsBeforeMutatingOwnerState)
{
    const auto schema = compile({"model", {{"x", false}}});
    view_model model{schema};
    auto task = std::async(std::launch::async,
                           [&]
                           {
                               EXPECT_EQ(schema.type(schema.field("x")), value_type::boolean);
                               EXPECT_THROW(model.begin_frame(1), std::logic_error);
                               EXPECT_THROW(model.boolean(schema.field("x")), std::logic_error);
                           });
    task.get();
    EXPECT_EQ(model.inspect().frame, 0);
}

/// @brief Invalid Unicode sequences cannot round-trip through XML without changing data.
class invalid_text : public ::testing::TestWithParam<std::string>
{
};

/// @brief Reject malformed, overlong, surrogate and XML-forbidden scalar data.
TEST_P(invalid_text, RejectsInvalidUtf8)
{
    EXPECT_THROW(compile({"model", {{"text", GetParam()}}}), std::invalid_argument);
}

/// @brief Stable readable case names keep XML and malformed bytes out of test identifiers.
std::string encoding_name(const ::testing::TestParamInfo<std::string>& info)
{
    static constexpr std::array names{"Nul",        "Overlong",  "Surrogate",
                                      "OutOfRange", "Truncated", "XmlNoncharacter"};
    return names.at(info.index);
}

INSTANTIATE_TEST_SUITE_P(encoding, invalid_text,
                         ::testing::Values(std::string{"\0", 1}, std::string{"\xc0\x80", 2},
                                           std::string{"\xed\xa0\x80", 3},
                                           std::string{"\xf4\x90\x80\x80", 4},
                                           std::string{"\xe2\x82", 2},
                                           std::string{"\xef\xbf\xbf", 3}),
                         encoding_name);
} // namespace atlas::mvvm::tests
