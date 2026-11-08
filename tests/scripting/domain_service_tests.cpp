/** @file @brief Typed inexpensive native services preserve host errors and block reentrant Lua. */
#include <atlas/scripting/domain.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief External projects provide generic capabilities without linking application logic into
/// Lua.
class increment_service final : public synchronous_service
{
  public:
    std::vector<mvvm::value> invoke(std::span<const mvvm::value> arguments) override
    {
        return {std::get<std::int64_t>(arguments[0]) + 1};
    } ///< Return one typed scalar.
};

/// @brief Record scalar visits to verify deterministic native-to-director registration order.
class recording_service final : public synchronous_service
{
  public:
    std::vector<std::int64_t> visits; ///< Observed scalar context order.

    std::vector<mvvm::value> invoke(std::span<const mvvm::value> arguments) override
    {
        visits.push_back(std::get<std::int64_t>(arguments[0]));
        return {};
    } ///< No simulation work.
};

/// @brief Removing/recreating a registration appends it without changing earlier live order.
TEST(script_domain_services, DirectorDispatchPreservesRegistrationOrder)
{
    auto first =
        std::make_shared<mvvm::view_model>(mvvm::compile({"first", {{"id", std::int64_t{1}}}}));
    auto second =
        std::make_shared<mvvm::view_model>(mvvm::compile({"second", {{"id", std::int64_t{2}}}}));
    domain ui;
    ui.bind_context("first", first);
    ui.bind_context("second", second);
    auto recorder = std::make_shared<recording_service>();
    ui.register_service({"record", {mvvm::value_type::integer}, {}, recorder});
    ui.register_source({"main", 1, "@main.lua",
                        "local id=model.field('id');return "
                        "{tick=function()native.call('record',model.get(id))end}"});
    auto a = ui.create({"main", "first"});
    ui.create({"main", "second"});
    first->begin_frame(1);
    second->begin_frame(1);
    ui.begin_frame(1);
    ui.dispatch("tick");
    EXPECT_EQ(recorder->visits, (std::vector<std::int64_t>{1, 2}));
    recorder->visits.clear();
    ui.remove(a);
    ui.create({"main", "first"});
    ui.dispatch("tick");
    EXPECT_EQ(recorder->visits, (std::vector<std::int64_t>{2, 1}));
}

/// @brief Preserve a distinct project-defined error across the Lua C++ boundary.
class project_failure : public std::runtime_error
{
  public:
    project_failure() : std::runtime_error("project capability failed")
    {
    } ///< Stable diagnostic.
};

/// @brief An external service can exercise error and reentry paths without engine-specific logic.
class failing_service final : public synchronous_service
{
  public:
    domain* runtime = nullptr; ///< Borrow only while the owning test/domain is alive.

    std::vector<mvvm::value> invoke(std::span<const mvvm::value>) override
    {
        if (runtime)
        {
            runtime->dispatch("tick");
        }

        throw project_failure{};
    } ///< Reentry must fail before reaching another Lua instance.
};

/// @brief Host failures keep their original type and cannot publish earlier staged model writes.
TEST(script_domain_services, NativeErrorsAndReentryPreserveProducerRollback)
{
    auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"state", {{"count", std::int64_t{0}}}}));
    domain ui;
    ui.bind_context("hud", model);
    auto service = std::make_shared<failing_service>();
    ui.register_service({"fail", {}, {}, service});
    ui.register_source({"main", 1, "@main.lua",
                        "local c=model.field('count');return "
                        "{tick=function()model.set(c,99);native.call('fail')end}"});
    auto instance = ui.create({"main", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    EXPECT_THROW(ui.call(instance, "tick"), project_failure);
    EXPECT_EQ(model->inspect().commands, 0);
    instance = ui.reload(instance);
    service->runtime = &ui;
    EXPECT_THROW(ui.call(instance, "tick"), std::logic_error);
    EXPECT_EQ(model->inspect().commands, 0);
}

/// @brief Domain bindings use a checked signature and remain distinct from asynchronous requests.
TEST(script_domain_services, ValidatesArgumentsAndRoutesSynchronousResults)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"state"}));
    domain ui;
    ui.bind_context("hud", model);
    ui.register_service({"increment",
                         {mvvm::value_type::integer},
                         {mvvm::value_type::integer},
                         std::make_shared<increment_service>()});
    ui.register_source({"main", 1, "@main.lua",
                        "return {tick=function() return native.call('increment',41) end, "
                        "bad=function() native.call('increment','bad') end}"});
    auto first = ui.create({"main", "hud"});
    model->begin_frame(1);
    ui.begin_frame(1);
    EXPECT_EQ(std::get<std::int64_t>(ui.call(first, "tick").at(0)), 42);
    EXPECT_THROW(ui.call(first, "bad"), std::invalid_argument);
    EXPECT_TRUE(ui.inspect(first).failed);
}
} // namespace atlas::scripting::tests
