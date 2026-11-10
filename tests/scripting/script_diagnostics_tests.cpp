/** @file @brief Authored observations use injected sinks and the normal protected Lua boundary. */
#include <atlas/scripting/domain.hpp>
#include <gtest/gtest.h>
#include <sstream>

namespace atlas::scripting::tests
{
namespace
{
/// @brief Retain the authored record fields without relying on the logger's formatted output.
class log_sink final : public diagnostics::log_output
{
  public:
    std::string message, category;     ///< Latest authored record.
    diagnostics::log_level severity{}; ///< Severity selected by the script.

    void append(std::string_view) override
    {
    } ///< Structured capture uses record.

    void flush() override
    {
    } ///< The fixture uses synchronous logging.

    void record(const diagnostics::log_record& item) override
    {
        if (item.category.starts_with("lua."))
        {
            message = item.message;
            category = item.category;
            severity = item.level;
        }
    } ///< Retain only script-authored records.
};

/// @brief Read the same trace representation consumed by external profilers.
std::string trace_text(const diagnostics::profiler& profile)
{
    std::ostringstream output;
    profile.write_chrome_trace(output);
    return output.str();
}

/// @brief Build a fresh model for each isolated session or domain.
auto make_model()
{
    return std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
}
} // namespace

/// @brief Script observations use the injected logger, capture and registry with stable
/// attribution.
TEST(authored_diagnostics, UsesInjectedDestinations)
{
    const auto sink = std::make_shared<log_sink>();
    diagnostics::logging_options logging;
    logging.asynchronous = false;
    auto logger = std::make_shared<diagnostics::logger>(logging, sink);
    auto profile = std::make_shared<diagnostics::profiler>();
    auto metrics = std::make_shared<diagnostics::metrics>();
    auto model = make_model();
    domain ui{{}, {}, {logger, profile}, {true, metrics}};
    ui.bind_context("hud", model);
    ui.register_source({"menu", 1, "@project/menu.lua", R"(
        return { tick = function()
            assert(diagnostic.log('warning', 'menu', 'opened'))
            assert(diagnostic.increment('opens', 2, {screen='main'}))
            assert(diagnostic.increment('opens', 3, {screen='main'}))
            assert(diagnostic.gauge('items', -4))
            return diagnostic.scope('layout', function(a,b) return a,b end, 7, 'done')
        end }
    )"});
    const auto menu = ui.create({"menu", "hud"});
    model->begin_frame(3);
    ui.begin_frame(3);
    const auto result = ui.call(menu, "tick");
    EXPECT_EQ(result, (std::vector<mvvm::value>{std::int64_t{7}, std::string{"done"}}));
    EXPECT_EQ(sink->category, "lua.menu");
    EXPECT_EQ(sink->severity, diagnostics::log_level::warning);
    EXPECT_NE(sink->message.find("source=@project/menu.lua"), std::string::npos);
    EXPECT_NE(sink->message.find("frame=3"), std::string::npos);
    const auto points = metrics->snapshot().points;
    ASSERT_EQ(points.size(), 2U);
    EXPECT_EQ(points[0].name, "lua.items");
    EXPECT_DOUBLE_EQ(points[0].value, -4);
    EXPECT_EQ(points[1].attributes, (diagnostics::metric_attributes{{"screen", "main"}}));
    EXPECT_DOUBLE_EQ(points[1].value, 5);
    const auto trace = trace_text(*profile);
    EXPECT_EQ(trace.find("\"name\":\"layout\"") != std::string::npos,
              diagnostics::instrumentation_enabled);
    if constexpr (diagnostics::instrumentation_enabled)
    {
        EXPECT_NE(trace.find("domain=" + std::to_string(menu.domain)), std::string::npos);
        EXPECT_NE(trace.find("instance=1 generation=1 frame=3"), std::string::npos);
    }
}

/// @brief Missing capture still executes the callback with normal Lua results and errors.
TEST(authored_diagnostics, DisabledDestinationsKeepBehaviorSemantics)
{
    auto model = make_model();
    diagnostics::profiling_options options;
    options.enabled = false;
    auto profile = std::make_shared<diagnostics::profiler>(options);
    session script{model, {}, {}, {{}, profile}, {true}};
    script.load(R"(return {tick=function()
        assert(not diagnostic.log('info','ui','unused'))
        assert(not diagnostic.gauge('unused',1))
        return diagnostic.scope('disabled', function() return 9 end)
    end})");
    model->begin_frame(1);
    script.begin_frame(1);
    EXPECT_EQ(script.call("tick"), (std::vector<mvvm::value>{std::int64_t{9}}));
    EXPECT_EQ(profile->statistics().recorded, 0U);
}

/// @brief A failed region is closed during unwinding and a replacement has fresh attribution.
TEST(authored_diagnostics, ClosesFailedRegionsAndCorrelatesNativeResultsAfterReload)
{
    auto model = make_model();
    auto profile = std::make_shared<diagnostics::profiler>();
    domain ui{{}, {}, {{}, profile}, {true}};
    ui.bind_context("hud", model);
    ui.register_source({"menu", 1, "@menu.lua", R"(return {tick=function()
        diagnostic.scope('broken',function() error('scope fault') end)
    end})"});
    const auto first = ui.create({"menu", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    EXPECT_THROW(ui.call(first, "tick"), std::runtime_error);
    ui.register_source({"menu", 2, "@menu.lua", R"(return {
        tick=function() model.request('native') end,
        on_result=function() diagnostic.scope('result',function() end) end
    })"});
    const auto second = ui.reload(first);
    ui.call(second, "tick");
    auto work = ui.take_requests();
    ASSERT_EQ(work.size(), 1U);
    ASSERT_TRUE(work[0].work.complete());
    ui.deliver_results();
    const auto trace = trace_text(*profile);
    if constexpr (diagnostics::instrumentation_enabled)
    {
        const auto start = trace.find("\"name\":\"broken\"");
        ASSERT_NE(start, std::string::npos);
        EXPECT_NE(
            trace.substr(start, trace.find('\n', start) - start).find("\"result\":\"failed\""),
            std::string::npos);
        EXPECT_NE(trace.find("generation=2 frame=1 request=1"), std::string::npos);
    }
}

/// @brief Capability injection and private tables cannot cross domain or behavior boundaries.
TEST(authored_diagnostics, IsolatesIndependentDomainsAndPrivateTables)
{
    auto ui_model = make_model();
    auto app_model = make_model();
    auto ui_metrics = std::make_shared<diagnostics::metrics>();
    auto app_metrics = std::make_shared<diagnostics::metrics>();
    domain ui{{}, {}, {}, {true, ui_metrics}};
    domain application{{}, {}, {}, {true, app_metrics}};
    const source_asset source{"menu", 1, "@menu.lua", R"(return {
        tick=function(v) assert(diagnostic.increment('ticks',v)) end,
        clear=function() diagnostic.increment=nil end
    })"};
    ui.bind_context("state", ui_model);
    application.bind_context("state", app_model);
    ui.register_source(source);
    application.register_source(source);
    const auto first = ui.create({"menu", "state"});
    const auto sibling = ui.create({"menu", "state"});
    const auto app = application.create({"menu", "state"});
    ui_model->begin_frame(1);
    app_model->begin_frame(1);
    ui.begin_frame(1);
    application.begin_frame(1);
    ui.call(first, "clear");
    const mvvm::value two{std::int64_t{2}}, five{std::int64_t{5}};
    ui.call(sibling, "tick", {&two, 1});
    application.call(app, "tick", {&five, 1});
    EXPECT_DOUBLE_EQ(ui_metrics->snapshot().points.at(0).value, 2);
    EXPECT_DOUBLE_EQ(app_metrics->snapshot().points.at(0).value, 5);

    auto standalone_model = make_model();
    session standalone{standalone_model, {}, {}, {}, {false, app_metrics}};
    EXPECT_NO_THROW(standalone.load("assert(diagnostic == nil); return {}"));
}
} // namespace atlas::scripting::tests
