/** @file @brief Owner-phase menu navigation and Flow document tests. */
#include <atlas/ui_content/flow.hpp>
#include <atlas/ui_content/prepared.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_content::tests
{
/// @brief Read a stable typed graph and produce a deterministic traversal.
TEST(menu_flow, ParsesAndTraversesDeclarativeGraph)
{
    constexpr auto xml = R"(<flow version="1">
      <region id="main" initial="home"/>
      <screen id="home" region="main" asset="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000001"/>
      <screen id="settings" region="main" asset="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000002"/>
      <transition id="open-settings" from="home" to="settings" operation="push"/>
      <transition id="close-settings" from="settings" operation="back"/>
    </flow>)";
    const auto graph = parse_flow_xml({xml});
    EXPECT_EQ(plan_flow_path(graph, "home", "settings"),
              (std::vector<std::string>{"open-settings"}));
    EXPECT_EQ(write_flow_xml(parse_flow_xml({write_flow_xml(graph)})), write_flow_xml(graph));
    const auto id = [](std::uint64_t number, assets::asset_type type)
    {
        return assets::asset_ref{
            {assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23"), number}, type};
    };
    const prepared_flow cooked{
        {id(3, assets::asset_type::flow), 1},
        graph,
        {{id(1, assets::asset_type::ui), {{id(1, assets::asset_type::ui), 1}}},
         {id(2, assets::asset_type::ui), {{id(2, assets::asset_type::ui), 1}}}}};
    const auto bytes = encode_prepared(cooked);
    EXPECT_EQ(std::to_integer<unsigned>(bytes.at(4)), 1U);
    EXPECT_EQ(write_flow_xml(decode_prepared_flow(bytes).graph), write_flow_xml(graph));
    auto broken = graph;
    broken.transitions.front().to = "missing";
    EXPECT_THROW(validate_flow(broken), std::invalid_argument);
    broken = graph;
    broken.transitions.erase(broken.transitions.begin());
    EXPECT_THROW(validate_flow(broken), std::invalid_argument);
    broken = graph;
    broken.regions.front().owner = "settings";
    EXPECT_THROW(validate_flow(broken), std::invalid_argument);
    broken = graph;
    broken.parameters.push_back({"allowed", false});
    broken.transitions.front().guard = flow_guard{"allowed", true};
    EXPECT_THROW((void)plan_flow_path(broken, "home", "settings"), std::out_of_range);
    broken.transitions.front().guard = flow_guard{"allowed", std::int64_t{1}};
    EXPECT_THROW(validate_flow(broken), std::invalid_argument);
}
} // namespace atlas::ui_content::tests
