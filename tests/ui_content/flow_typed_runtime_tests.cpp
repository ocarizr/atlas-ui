/** @file @brief Real streamed typed menu/region composition and prepared parity. */
#include <algorithm>
#include <atlas/ui_content/prepared.hpp>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::ui_content::tests
{
/// @brief Own immutable fixture sources until every composed runtime has retired.
class typed_flow_test : public testing::Test
{
  protected:
    /// @brief Supply a typed root panel with one focusable child.
    assets::asset_ref add(std::string name, assets::asset_type type, std::string_view extra = {})
    {
        const auto xml =
            "<ui version='2' kind='" + std::string{assets::format_type(type)} + "' " +
            std::string{extra} +
            "><element id='content'/><element id='focus' parent='content'>"
            "<interaction enabled='true' hit-test='true' focusable='true'/></element></ui>";
        const assets::asset_ref ref{assets::allocate_id(catalog), type};
        catalog.records.push_back({ref, {"fixture", name}, 1, xml.size()});
        backing.sources.emplace(
            name, streaming::memory_asset{
                      std::make_shared<const streaming::asset_bytes>(xml.begin(), xml.end())});
        backing.routes.push_back({ref, 1, std::move(name), 0});
        return ref;
    }

    /// @brief Initialize a deterministic Settings-style tree using real typed source acquisition.
    void SetUp() override
    {
        home = add("home.xml", assets::asset_type::menu);
        settings = add("settings.xml", assets::asset_type::menu);
        audio = add("audio.xml", assets::asset_type::submenu);
        video = add("video.xml", assets::asset_type::submenu);
        dialog =
            add("popup.xml", assets::asset_type::popup, "modal='false' dismiss-outside='true'");
        graph.regions = {{"main", "home", ""}, {"tabs", "audio", "settings", "content", true}};
        graph.screens = {{"home", "main", home},
                         {"settings", "main", settings},
                         {"audio", "tabs", audio},
                         {"video", "tabs", video},
                         {"dialog", "main", dialog}};
        graph.screens.back().popup.dismiss_back = false;
        graph.transitions = {
            {.id = "open", .from = "home", .to = "settings"},
            {.id = "tab", .from = "audio", .to = "video", .operation = flow_operation::replace},
            {.id = "show",
             .from = "settings",
             .to = "dialog",
             .operation = flow_operation::overlay}};
        const auto xml = write_flow_xml(graph);
        source = {assets::allocate_id(catalog), assets::asset_type::flow};
        catalog.records.push_back({source, {"fixture", "flow.xml"}, 1, xml.size()});
        backing.sources.emplace(
            "flow.xml", streaming::memory_asset{std::make_shared<const streaming::asset_bytes>(
                            xml.begin(), xml.end())});
        backing.routes.push_back({source, 1, "flow.xml", 0});
        storage = mount();
    }

    /// @brief Never depend on a worker finishing within an arbitrary sleep interval.
    flow_status finish(flow_manager& flow)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        auto status = flow.advance(frame++);
        while (status == flow_status::loading && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
            status = flow.advance(frame++);
        }

        return status;
    }

    /// @brief Switch backing representation without changing authored IDs or revisions.
    void replace(assets::asset_ref ref, std::span<const std::byte> bytes)
    {
        auto& record = *std::ranges::find(catalog.records, ref, &assets::asset_record::reference);
        record.byte_size = bytes.size();
        auto payload = std::make_shared<streaming::asset_bytes>();
        payload->assign(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                        reinterpret_cast<const std::uint8_t*>(bytes.data()) + bytes.size());
        backing.sources.at(record.source.path) = streaming::memory_asset{std::move(payload)};
    }

    /// @brief Each immutable mount retains independent copies of the registered sources.
    std::unique_ptr<streaming::catalog_streamer> mount()
    {
        return std::make_unique<streaming::catalog_streamer>(
            workers, assets::catalog::compile(catalog), backing);
    }

    jobs::job_system workers{2}; ///< Shared pool outlives mounts and callbacks.
    assets::catalog_document catalog{
        assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")}; ///< Stable namespace.
    streaming::storage_mount backing;                     ///< Reproducible in-memory fixture bytes.
    std::unique_ptr<streaming::catalog_streamer> storage; ///< Source acquisition boundary.
    flow_document graph; ///< Typed menus, submenu region and popup.
    assets::asset_ref home{}, settings{}, audio{}, video{}, dialog{},
        source{};            ///< Fixed identities.
    std::uint64_t frame = 2; ///< Increasing owner sync points.
};

/// @brief A child presentation identifies its parent host; popup overrides inherit asset defaults.
TEST_F(typed_flow_test, ComposesTypedRegionsAndClosesPopupWithoutRetiringTheMenu)
{
    flow_manager flow{graph, make_flow_loader({*storage})};
    flow.start();
    ASSERT_EQ(finish(flow), flow_status::committed);
    ASSERT_TRUE(flow.request("open"));
    ASSERT_EQ(finish(flow), flow_status::committed);
    const auto views = flow.presentation();
    ASSERT_EQ(views.size(), 2U);
    EXPECT_EQ(views.back().host, "content");
    EXPECT_EQ(views.back().parent, views.front().view);
    EXPECT_EQ(views.back().view->presentation().type, assets::asset_type::submenu);
    ASSERT_TRUE(flow.request("show"));
    ASSERT_EQ(finish(flow), flow_status::committed);
    const auto popup = flow.presentation().back();
    if (!popup.popup)
    {
        FAIL() << "Typed popup did not expose an effective policy";
        return;
    }

    EXPECT_FALSE(popup.popup->modal);
    EXPECT_TRUE(popup.popup->dismiss_outside);
    EXPECT_FALSE(popup.popup->dismiss_back);
    ASSERT_TRUE(flow.close_popup("dialog"));
    ASSERT_EQ(finish(flow), flow_status::committed);
    ASSERT_EQ(flow.presentation().size(), 2U);
    EXPECT_EQ(flow.presentation().back().screen, "audio");
    EXPECT_EQ(flow.presentation().front().view, views.front().view);
}

/// @brief The new Flow/UI wire versions carry exactly the same typed presentation contracts.
TEST_F(typed_flow_test, BuildsPreparedTypedFlowThroughTheRealStreamer)
{
    const auto prepared = prepare_flow(source, {*storage});
    for (const auto ref : {home, settings, audio, video, dialog})
    {
        replace(ref, encode_prepared(prepare_view(ref, {*storage})));
    }

    replace(source, encode_prepared(prepared));
    const auto binary = mount();
    auto flow = build_prepared_flow(load_prepared_flow(source, {*binary}), {*binary});
    flow->start();
    ASSERT_EQ(finish(*flow), flow_status::committed);
    ASSERT_TRUE(flow->request("open"));
    ASSERT_EQ(finish(*flow), flow_status::committed);
    ASSERT_TRUE(flow->request("tab"));
    ASSERT_EQ(finish(*flow), flow_status::committed);
    EXPECT_EQ(flow->presentation().back().screen, "video");
    ASSERT_TRUE(flow->exit_menu("video"));
    ASSERT_EQ(finish(*flow), flow_status::committed);
    EXPECT_EQ(flow->presentation().front().screen, "home");
}
} // namespace atlas::ui_content::tests
