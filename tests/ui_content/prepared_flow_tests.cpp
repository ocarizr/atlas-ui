/** @file @brief Prepared Flow graph validation and real typed-streamer composition. */
#include <algorithm>
#include <atlas/ui_content/prepared.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::ui_content::tests
{
namespace
{
/// @brief Prepare equivalent loose and binary mounts with the same typed identities.
class prepared_flow_test : public testing::Test
{
  protected:
    /// @brief Provision two distinct views and one shared-model/script dependency chain.
    void SetUp() override
    {
        home =
            add("home.xml", assets::asset_type::ui, R"(<ui version="1"><element id="home"/></ui>)");
        model = add(
            "hud.xml", assets::asset_type::view_model,
            R"(<ViewModel version="1" name="hud"><Fields><Field name="xp" type="integer" value="7"/></Fields></ViewModel>)");
        script = add("settings.lua", assets::asset_type::script, "return {}");
        const auto xml = "<ui version='1'><model name='hud' asset='" + assets::format_id(model.id) +
                         "'/><element id='settings'><behavior model='hud' asset='" +
                         assets::format_id(script.id) + "'/></element></ui>";
        settings = add("settings.xml", assets::asset_type::ui, xml);
        flow_document graph;
        graph.regions = {{"main", "home", ""}};
        graph.screens = {{"home", "main", home}, {"settings", "main", settings}};
        graph.parameters = {{"allowed", true}};
        graph.transitions = {
            {.id = "open", .from = "home", .to = "settings", .guard = flow_guard{"allowed", true}},
            {.id = "back", .from = "settings", .operation = flow_operation::back}};
        source = add("flow.xml", assets::asset_type::flow, write_flow_xml(graph));
        loose = mount();
    }

    /// @brief Mount independent runtime payloads without changing authored revisions.
    [[nodiscard]] std::unique_ptr<streaming::catalog_streamer> mount()
    {
        return std::make_unique<streaming::catalog_streamer>(
            jobs, assets::catalog::compile(catalog), backing);
    }

    /// @brief Encode independent graph/view records, leaving Lua as streamed code.
    void package(const prepared_flow& flow)
    {
        replace(source, encode_prepared(flow));
        replace(home, encode_prepared(prepare_view(home, {*loose})));
        replace(settings, encode_prepared(prepare_view(settings, {*loose})));
        replace(model, std::array{std::byte{'!'}});
    }

    /// @brief Poll real completion with a diagnostic deadline rather than timing assumptions.
    flow_status finish(flow_manager& flow)
    {
        auto status = flow.advance(frame++);
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (status == flow_status::loading && std::chrono::steady_clock::now() < end)
        {
            std::this_thread::yield();
            status = flow.advance(frame++);
        }

        return status;
    }

    /// @brief Register a typed fixture source and its authenticated mount route.
    assets::asset_ref add(std::string name, assets::asset_type type, std::string_view data)
    {
        const assets::asset_ref ref{assets::allocate_id(catalog), type};
        catalog.records.push_back({ref, {"fixture", name}, 1, data.size()});
        backing.routes.push_back({ref, 1, name, 0});
        backing.sources.emplace(
            std::move(name), streaming::memory_asset{std::make_shared<const streaming::asset_bytes>(
                                 data.begin(), data.end())});
        return ref;
    }

    /// @brief Replace a representation at the same identity and adjust only its storage size.
    void replace(assets::asset_ref ref, std::span<const std::byte> bytes)
    {
        auto found = std::ranges::find(catalog.records, ref, &assets::asset_record::reference);
        ASSERT_NE(found, catalog.records.end());
        found->byte_size = bytes.size();
        auto payload = std::make_shared<streaming::asset_bytes>();
        payload->reserve(bytes.size());
        for (auto byte : bytes)
        {
            payload->push_back(std::to_integer<std::uint8_t>(byte));
        }

        backing.sources.at(found->source.path) = streaming::memory_asset{std::move(payload)};
    }

    jobs::job_system jobs{2}; ///< Workers retained until every typed mount shuts down.
    assets::catalog_document catalog{
        assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")}; ///< Mutable catalog fixture.
    streaming::storage_mount backing;                   ///< Loose or binary memory payloads.
    std::unique_ptr<streaming::catalog_streamer> loose; ///< Independent XML reference mount.
    assets::asset_ref source{};                         ///< Flow record.
    assets::asset_ref home{};                           ///< Initial screen.
    assets::asset_ref settings{};                       ///< Guarded screen.
    assets::asset_ref model{};  ///< Transitive dependency embedded into prepared UI.
    assets::asset_ref script{}; ///< Streamed Lua source.
    std::uint64_t frame = 2;    ///< Strictly increasing owner sync frames.
};
} // namespace

/// @brief Execute the same history on XML and binary mounts with no runtime authored UI XML.
TEST_F(prepared_flow_test, MatchesXmlNavigationOnIndependentlyPreparedScreens)
{
    const auto cooked = prepare_flow(source, {*loose});
    ASSERT_EQ(flow_dependencies(cooked).size(), 5U);
    package(cooked);
    const auto packed = mount();
    const auto loaded = load_prepared_flow(source, {*packed});
    EXPECT_EQ(encode_prepared(cooked), encode_prepared(loaded));
    auto prepared = build_prepared_flow(loaded, {*packed});
    flow_manager authored{load_flow(source, {*loose}), make_flow_loader({*loose})};
    for (auto* manager : {&authored, prepared.get()})
    {
        manager->start();
        ASSERT_EQ(finish(*manager), flow_status::committed);
        ASSERT_EQ(manager->presentation().size(), 1U);
        EXPECT_EQ(manager->presentation().front().screen, "home");
        ASSERT_TRUE(manager->request("open"));
        ASSERT_EQ(finish(*manager), flow_status::committed);
        const auto shown = manager->presentation();
        ASSERT_EQ(shown.size(), 1U);
        ASSERT_EQ(shown.front().screen, "settings");
        auto& hud = shown.front().view->model("hud");
        EXPECT_EQ(hud.integer(hud.description().field("xp")), 7);
        ASSERT_TRUE(manager->request("back"));
        ASSERT_EQ(finish(*manager), flow_status::committed);
        EXPECT_EQ(manager->presentation().front().screen, "home");
        manager->shutdown(frame++);
    }
}

/// @brief Reject revisions and disagreement between the Flow manifest and a candidate view.
TEST_F(prepared_flow_test, RejectsStaleAndMismatchedViewDataBeforePublication)
{
    auto cooked = prepare_flow(source, {*loose});
    package(cooked);
    const auto packed = mount();
    auto stale = cooked;
    ++stale.source.revision;
    EXPECT_THROW((void)build_prepared_flow(stale, {*packed}), std::invalid_argument);
    auto contract = std::ranges::find(cooked.screens, settings, &prepared_flow_screen::source);
    ASSERT_NE(contract, cooked.screens.end());
    std::erase_if(contract->dependencies,
                  [&](const auto& entry)
                  {
                      return entry.source == script;
                  });
    auto manager = build_prepared_flow(cooked, {*packed});
    manager->start();
    ASSERT_EQ(finish(*manager), flow_status::committed);
    const auto* initial = manager->presentation().front().view;
    ASSERT_TRUE(manager->request("open"));
    EXPECT_EQ(finish(*manager), flow_status::failed);
    EXPECT_EQ(manager->presentation().front().view, initial);
    EXPECT_TRUE(manager->failure());
    manager->shutdown(frame++);
}

/// @brief Corrupted bytes and invalid dependency contracts fail before runtime construction.
TEST_F(prepared_flow_test, RejectsCorruptionVersionAndIncompleteScreenTables)
{
    auto cooked = prepare_flow(source, {*loose});
    const auto bytes = encode_prepared(cooked);
    auto damaged = bytes;
    damaged.back() ^= std::byte{1};
    EXPECT_THROW((void)decode_prepared_flow(damaged), std::invalid_argument);
    damaged = bytes;
    damaged.at(4) = std::byte{255};
    EXPECT_THROW((void)decode_prepared_flow(damaged), std::invalid_argument);
    EXPECT_THROW((void)decode_prepared_flow(std::span{bytes}.first(bytes.size() - 1)),
                 std::invalid_argument);
    auto missing = cooked;
    missing.screens.pop_back();
    EXPECT_THROW((void)encode_prepared(missing), std::invalid_argument);
    cooked.screens.front().dependencies.front().revision = 0;
    EXPECT_THROW((void)encode_prepared(cooked), std::invalid_argument);
}

/// @brief Reject unresolved element bindings before producing any binary Flow record.
TEST_F(prepared_flow_test, PreparationChecksAuthoredControlBindings)
{
    auto graph = load_flow(source, {*loose});
    graph.transitions.front().control = "missing";
    const auto xml = write_flow_xml(graph);
    replace(source, std::as_bytes(std::span{xml}));
    const auto revised = mount();
    EXPECT_THROW((void)prepare_flow(source, {*revised}), std::invalid_argument);
}
} // namespace atlas::ui_content::tests
