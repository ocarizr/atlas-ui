/** @file @brief Streamer-backed preview with real UI events, Lua predicates and explicit replay. */
#include "fixture.hpp"
#include <atlas/editor_flow/preview.hpp>
#include <chrono>
#include <fstream>
#include <thread>

namespace atlas::editor_flow::tests
{
/// @brief Fixed fixture identities and memory mounts exercise production asynchronous loading.
class preview_runtime_test : public testing::Test
{
  protected:
    /// @brief Fixture file reads are test setup; production previews only receive streamed bytes.
    std::string fixture(std::string_view name)
    {
        std::ifstream file{std::string{ATLAS_FLOW_PREVIEW_DATA} + "/" + std::string{name}};
        if (!file)
        {
            throw std::runtime_error("Missing preview fixture");
        }

        return {std::istreambuf_iterator<char>{file}, {}};
    }

    assets::asset_ref add(std::string name, assets::asset_type type, std::string text)
    {
        const assets::asset_ref ref{assets::allocate_id(catalog), type};
        catalog.records.push_back({ref, {"fixture", name}, 1, text.size()});
        backing.routes.push_back({ref, 1, name, 0});
        backing.sources.emplace(
            name, streaming::memory_asset{
                      std::make_shared<const streaming::asset_bytes>(text.begin(), text.end())});
        return ref;
    } ///< Immutable typed source data with a reproducible revision.

    void SetUp() override
    {
        (void)add("state.xml", assets::asset_type::view_model, fixture("preview_state.xml"));
        const auto home = add("home.xml", assets::asset_type::menu, fixture("preview_home.xml"));
        const auto next = add("next.xml", assets::asset_type::menu,
                              "<ui version='2' kind='menu'><element id='root'/></ui>");
        source = example();
        source.graph.screens = {{"home", "viewport", home}, {"next", "viewport", next}};
        source.graph.transitions = {{"open", "home", "next"}};
        source.conditions = {{"open", {"project.allowed", "state"}}};
        mount = std::make_unique<streaming::catalog_streamer>(
            jobs, assets::catalog::compile(catalog), backing);
    } ///< Each case owns its own pool, mount and mutable model instances.

    preview_registration registration()
    {
        return [](scripting::domain& scripts)
        {
            scripts.register_source({"callables", 1, "@preview_callables.lua", R"(
                actions.register({id='project.allow', label='Allow', category='Preview',
                  execute=function(ctx) ctx.set('allowed',true) end})
                navigation_conditions.register({id='project.allowed',label='Allowed',category='Preview',
                  evaluate=function(ctx) return ctx.get('allowed') end})
                return {}
            )"});
            scripts.load_callable_module({"callables", {"project"}});
        };
    } ///< Frozen discovery snapshot installed into each actual UI/model domain.

    void settle(preview& value)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        do
        {
            (void)value.frame({0, 0, 100, 80});
            std::this_thread::yield();
        } while ((value.applying() || value.status() == ui_content::flow_status::loading) &&
                 std::chrono::steady_clock::now() < deadline);

        ASSERT_FALSE(value.applying()) << value.diagnostic();
        ASSERT_NE(value.status(), ui_content::flow_status::loading) << value.diagnostic();
    } ///< Diagnostic timeout, not an assumed I/O completion delay.

    void click(preview& value, float x)
    {
        const std::array events{ui::input_event{ui::input_kind::pointer_down, {x, 5}},
                                ui::input_event{ui::input_kind::pointer_up, {x, 5}}};
        (void)value.frame({0, 0, 100, 80}, events);
        settle(value);
    } ///< Actual semantic input replay; route requests are a separate diagnostic operation.

    jobs::job_system jobs{2}; ///< Shared pool outlives every loader and mount.
    assets::catalog_document catalog{
        example().graph.screens.front().asset.id.space}; ///< Stable fixture namespace.
    streaming::storage_mount backing;                    ///< Immutable memory source snapshots.
    std::unique_ptr<streaming::catalog_streamer> mount;  ///< Typed storage boundary.
    document source; ///< Editable source independent from applied preview state.
};

/// @brief Real Lua mutations, not mocked condition fixtures, determine replayed route admission.
TEST_F(preview_runtime_test, LuaActionUpdatesActualModelAndGuardControlsReplayedNavigation)
{
    session editor{source};
    preview runtime{jobs.dispatcher()};
    runtime.apply(editor.compile(), ui_content::make_flow_loader({*mount}), registration());
    settle(runtime);
    ASSERT_TRUE(runtime.running()) << runtime.diagnostic();
    EXPECT_EQ(runtime.plan("home", "next").conditioned, (std::vector<std::string>{"open"}));
    click(runtime, 55);
    EXPECT_EQ(runtime.status(), ui_content::flow_status::rejected);
    EXPECT_EQ(runtime.presentation().front().screen, "home");
    click(runtime, 5);
    click(runtime, 55);
    ASSERT_EQ(runtime.presentation().front().screen, "next") << runtime.diagnostic();
    EXPECT_EQ(runtime.history().front().second, (std::vector<std::string>{"home", "next"}));
    EXPECT_EQ(runtime.active_route(), "open");
    ASSERT_FALSE(runtime.conditions().empty());
    EXPECT_TRUE(runtime.conditions().back().second);
    runtime.restart();
    settle(runtime);
    EXPECT_EQ(runtime.presentation().front().screen, "home");
    click(runtime, 55);
    EXPECT_EQ(runtime.status(), ui_content::flow_status::rejected);
}

/// @brief Saveable incomplete groups cannot replace an executable preview snapshot.
TEST_F(preview_runtime_test, MissingOrEmptyPredicateCannotReplaceValidPreview)
{
    session editor{source};
    preview runtime{jobs.dispatcher()};
    runtime.apply(editor.compile(), ui_content::make_flow_loader({*mount}), registration());
    settle(runtime);
    ASSERT_TRUE(runtime.running()) << runtime.diagnostic();
    source.conditions.front().expression = {};
    editor.apply(source);
    runtime.apply(editor.compile(), ui_content::make_flow_loader({*mount}), registration());
    settle(runtime);
    EXPECT_TRUE(runtime.running());
    EXPECT_EQ(runtime.presentation().front().screen, "home");
    EXPECT_NE(runtime.diagnostic().find("empty"), std::string_view::npos);
}

/// @brief A failing predicate rejects its transition without retiring a healthy active menu.
TEST_F(preview_runtime_test, PredicateFailurePreservesActiveScreenAndReportsItsCause)
{
    session editor{source};
    preview runtime{jobs.dispatcher()};
    runtime.apply(
        editor.compile(), ui_content::make_flow_loader({*mount}),
        [](auto& scripts)
        {
            scripts.register_action(
                {"project"}, {.id = "project.allow", .label = "Allow", .category = "Preview"},
                [](scripting::action_context&, std::span<const mvvm::value>)
                {
                });
            scripts.register_condition(
                {"project"}, {.id = "project.allowed", .label = "Allowed", .category = "Preview"},
                [](const scripting::condition_context&, std::span<const mvvm::value>) -> bool
                {
                    throw std::runtime_error("fixture predicate failed");
                });
        });
    settle(runtime);
    ASSERT_TRUE(runtime.running()) << runtime.diagnostic();
    ASSERT_TRUE(runtime.request("open"));
    settle(runtime);
    EXPECT_EQ(runtime.status(), ui_content::flow_status::failed);
    EXPECT_EQ(runtime.presentation().front().screen, "home");
    EXPECT_NE(runtime.diagnostic().find("fixture predicate failed"), std::string_view::npos);
}

/// @brief Executable preflight is stricter than the saveable draft shape budget.
TEST_F(preview_runtime_test, RejectsOversizedExecutableConditionBeforeRunningAnyPredicate)
{
    const auto leaf = source.conditions.front().expression;
    source.conditions.front().expression = {};
    source.conditions.front().expression.children.assign(64, leaf);
    session editor{source};
    preview runtime{jobs.dispatcher()};
    runtime.apply(editor.compile(), ui_content::make_flow_loader({*mount}), registration());
    settle(runtime);
    EXPECT_FALSE(runtime.running());
    EXPECT_NE(runtime.diagnostic().find("64 nodes"), std::string_view::npos);
}

/// @brief Neither negation nor an unused outgoing branch conceals an unresolved implementation.
TEST_F(preview_runtime_test, RejectsUnknownNegatedPredicateBeforeReplacingPreview)
{
    source.conditions.front().expression.callable = "project.missing";
    source.conditions.front().expression.negate = true;
    session editor{source};
    preview runtime{jobs.dispatcher()};
    runtime.apply(editor.compile(), ui_content::make_flow_loader({*mount}), registration());
    settle(runtime);
    EXPECT_FALSE(runtime.running());
    EXPECT_NE(runtime.diagnostic().find("Unresolved"), std::string_view::npos);
}

/// @brief Retiring a generation during reload cannot queue a route for the replacement instance.
TEST_F(preview_runtime_test, RetiredFocusExitDoesNotNavigateTheReloadedScreen)
{
    auto authored = ui_content::parse_xml({fixture("preview_home.xml")});
    authored.elements[1].events.push_back(
        {ui_content::authored_event::focus_exit, "atlas.navigate_to", "", "home", "open"});
    const auto xml = ui_content::write_xml(authored);
    for (auto& record : catalog.records)
    {
        if (record.source.path == "home.xml")
        {
            record.byte_size = xml.size();
        }
    }

    backing.sources.at("home.xml") = streaming::memory_asset{
        std::make_shared<const streaming::asset_bytes>(xml.begin(), xml.end())};
    mount = std::make_unique<streaming::catalog_streamer>(jobs, assets::catalog::compile(catalog),
                                                          backing);
    source.graph.transitions.push_back({"reload", "home", {}, ui_content::flow_operation::reload});
    session editor{source};
    preview runtime{jobs.dispatcher()};
    runtime.apply(editor.compile(), ui_content::make_flow_loader({*mount}), registration());
    settle(runtime);
    ASSERT_TRUE(runtime.running()) << runtime.diagnostic();
    click(runtime, 5);
    ASSERT_TRUE(runtime.request("reload"));
    settle(runtime);
    settle(runtime);
    EXPECT_EQ(runtime.presentation().front().screen, "home");
    EXPECT_EQ(runtime.active_route(), "reload");
    EXPECT_EQ(runtime.status(), ui_content::flow_status::idle);
}
} // namespace atlas::editor_flow::tests
