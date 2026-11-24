/** @file @brief Locale fallback, immutable resolution and cleanup-safe publication. */
#include "support.hpp"
#include <sstream>
#include <thread>

namespace atlas::localization::tests
{
/// @brief Requested and translation locales stay distinct during exact/parent/default fallback.
TEST_F(context_fixture, resolves_exact_parent_and_default_with_stable_handles)
{
    auto title = source.key("title");
    auto fallback = source.key("only_default");
    EXPECT_EQ(source.resolve(title).text, "Settings");
    source.select("PT-br");
    EXPECT_EQ(source.selected_language(), "pt-br");
    EXPECT_EQ(source.resolve(title).text, "Configurações");
    EXPECT_EQ(source.resolve(title).locale, "pt");
    EXPECT_EQ(source.resolve(fallback).locale, "en");
    EXPECT_EQ(source.resolve(source.key("empty")).text, " ");
    source.select("de-DE");
    EXPECT_EQ(source.resolve(title).text, "Settings");
    EXPECT_THROW((void)source.key("missing"), std::out_of_range);
}

/// @brief Fallback plurals must use the translated language, never the requested one.
TEST_F(context_fixture, plurals_use_translation_locale_and_require_count)
{
    std::string actual;
    context translated{fixture(), [&](std::string_view tag, double count)
                       {
                           actual = tag;
                           return fake_rule(tag, count);
                       }};
    translated.select("pt-br");
    const auto key = translated.key("items");
    EXPECT_EQ(translated.resolve(key, 1).text, "One item");
    EXPECT_EQ(actual, "en");
    EXPECT_EQ(translated.resolve(key, 2).text, "Items");
    EXPECT_THROW((void)translated.resolve(key), std::invalid_argument);
}

/// @brief Subscriptions run in registration order and observe the fully committed new revision.
TEST_F(context_fixture, notifies_once_on_change_and_disallows_recursive_mutation)
{
    std::vector<int> order;
    auto first = source.observe(
        [&](auto revision)
        {
            order.push_back(1);
            EXPECT_EQ(source.revision(), revision);
            EXPECT_THROW(source.select("en"), std::logic_error);
        });
    auto second = source.observe(
        [&](auto)
        {
            order.push_back(2);
            throw std::runtime_error("observer failed");
        });
    auto third = source.observe(
        [&](auto)
        {
            order.push_back(3);
        });
    source.select("en");
    EXPECT_TRUE(order.empty());
    EXPECT_EQ(source.revision(), 1U);
    source.select("ar");
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
    EXPECT_EQ(source.resolve(source.key("title")).direction, text_direction::right_to_left);
}

/// @brief Cancellation within a captured batch suppresses later callbacks without stale references.
TEST_F(context_fixture, token_reset_and_move_cancel_the_correct_registration)
{
    unsigned calls = 0;
    subscription later;
    auto first = source.observe(
        [&](auto)
        {
            later.reset();
        });
    later = source.observe(
        [&](auto)
        {
            ++calls;
        });
    source.select("pt");
    EXPECT_EQ(calls, 0U);
    auto another = source.observe(
        [&](auto)
        {
            ++calls;
        });
    subscription moved = std::move(another);
    source.select("ar");
    EXPECT_EQ(calls, 1U);
    moved.reset();
    source.select("en");
    EXPECT_EQ(calls, 1U);
}

/// @brief A rejected schema/definition or cancelled reload leaves revisions and prior outputs
/// intact.
TEST_F(context_fixture, reload_is_atomic_and_stable_across_source_reordering)
{
    auto key = source.key("title");
    auto retained = source.resolve(key);
    auto invalid = fixture();
    invalid.languages[0].messages.pop_back();
    EXPECT_THROW(source.reload(std::move(invalid)), std::invalid_argument);
    EXPECT_EQ(source.revision(), 1U);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW(source.reload(fixture(), stop.get_token()), std::runtime_error);
    auto changed = fixture();
    std::reverse(changed.languages.begin(), changed.languages.end());
    changed.languages.back().messages[0].text = "New settings";
    source.reload(std::move(changed));
    EXPECT_EQ(source.resolve(key).text, "New settings");
    EXPECT_EQ(retained.text, "Settings");
}

/// @brief Keys cannot cross contexts and native owner state cannot be read from a worker.
TEST_F(context_fixture, rejects_foreign_keys_and_worker_access)
{
    context other{fixture(), fake_rule};
    EXPECT_THROW((void)other.resolve(source.key("title")), std::invalid_argument);
    bool rejected = false;
    std::thread worker{[&]
                       {
                           try
                           {
                               (void)source.revision();
                           }
                           catch (const std::logic_error&)
                           {
                               rejected = true;
                           }
                       }};
    worker.join();
    EXPECT_TRUE(rejected);
}

/// @brief Retained text and cancellation tokens do not borrow a destroyed context.
TEST(localized_lifetime, retains_owned_text_and_supports_owner_destruction_during_callback)
{
    auto source = std::make_unique<context>(fixture(), fake_rule);
    auto text = source->resolve(source->key("title"));
    auto token = source->observe(
        [&](auto)
        {
            source.reset();
        });
    source->select("ar");
    EXPECT_FALSE(source);
    EXPECT_EQ(text.text, "Settings");
    token.reset();
}

/// @brief Registration storage remains bounded and cancelled slots can be reused.
TEST_F(context_fixture, bounds_observers_and_reclaims_cancelled_registrations)
{
    std::vector<subscription> tokens;
    tokens.reserve(1024);
    for (unsigned i = 0; i < 1024; ++i)
    {
        tokens.push_back(source.observe(
            [](auto)
            {
            }));
    }

    EXPECT_THROW((void)source.observe(
                     [](auto)
                     {
                     }),
                 std::overflow_error);
    tokens.clear();
    EXPECT_NO_THROW((void)source.observe(
        [](auto)
        {
        }));
}

/// @brief Cancelling an idle registration releases captured resources without a locale change.
TEST_F(context_fixture, cancellation_releases_capture_immediately)
{
    auto payload = std::make_shared<int>(1);
    std::weak_ptr<int> retained = payload;
    auto token = source.observe(
        [hold = payload](auto)
        {
            (void)hold;
        });
    payload.reset();
    ASSERT_FALSE(retained.expired());
    token.reset();
    EXPECT_TRUE(retained.expired());
}

/// @brief Explicit profiling captures parsing/publication/resolution and respects compile policy.
TEST(localization_diagnostics, records_injected_operations_and_parse_failures)
{
    diagnostics::services sinks{{}, std::make_shared<diagnostics::profiler>()};
    auto data = parse_xml(
        R"(<localization fallback="en"><language tag="en" direction="ltr"><string key="title">Settings</string></language></localization>)",
        sinks);
    context source{std::move(data), fake_rule, sinks};
    source.select("en-us");
    (void)source.resolve(source.key("title"));
    EXPECT_THROW((void)parse_xml("<wrong/>", sinks), std::invalid_argument);
    std::ostringstream trace;
    sinks.profile->write_chrome_trace(trace);
    for (const char* name : {"localization.parse", "localization.construct", "localization.publish",
                             "localization.resolve"})
    {
        EXPECT_EQ(trace.str().find(name) != std::string::npos,
                  diagnostics::instrumentation_enabled);
    }
}
} // namespace atlas::localization::tests
