/** @file @brief Source/prepared scrollbar authoring and runtime input share typed contracts. */
#include "composition_fixture.hpp"
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui_content/prepared.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace atlas::ui_content::tests
{
namespace
{
/// @brief Test-only fixture reads do not introduce a runtime storage path.
document scrollbar_document()
{
    const auto path = std::filesystem::path{ATLAS_UI_DOCUMENT}.parent_path() / "scrollbar.xml";
    std::ifstream file{path};
    if (!file)
    {
        throw std::runtime_error("missing scrollbar XML fixture");
    }

    const std::string xml{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    return parse_xml({xml, "scrollbar.xml"});
}
} // namespace

/// @brief New chrome metadata uses v6 while source/prepared representations remain equivalent.
TEST_F(composition_fixture, ScrollbarXmlAndPreparedConstructionShareInputAndGeometry)
{
    const auto source = scrollbar_document();
    const prepared_view prepared{ui_asset, source, {{ui_asset, 1}}, {}, {}};
    const auto bytes = encode_prepared(prepared);
    EXPECT_EQ(std::to_integer<unsigned>(bytes.at(4)), 6U);
    EXPECT_EQ(write_xml(decode_prepared(bytes).view), write_xml(source));
    auto view = build_prepared(decode_prepared(bytes), {*storage});
    ui::layout_engine layout{jobs.dispatcher()};
    const auto geometry = layout.update(view->tree(), {0, 0, 100, 80});
    ASSERT_EQ(geometry.scrollbars().size(), 1U);
    EXPECT_EQ(geometry.scrollbars()[0].geometry.thumb, (ui::layout_rect{90, 0, 10, 32}));
    view->begin_models(2);
    view->input().begin_frame(2, view->tree().snapshot(), geometry);
    view->input().focus(view->find("last"));
    view->input().process({});
    view->input().tick();
    view->input().late_tick();
    view->input().end_frame();
    view->tree().apply(view->tree().commands().seal());
    EXPECT_EQ(std::get<ui::scroll_layout>(view->tree().panel(view->find("viewport"))).offset.y,
              120);
}

/// @brief Invalid links, cycles and values are rejected before runtime construction/publication.
TEST(scrollbar_authoring_test, InvalidLinksAndThumbValuesAreRejected)
{
    auto source = scrollbar_document();
    const auto saved = source.elements.back().scrollbar;
    auto link = saved.value_or(scrollbar_link{});
    link.viewport = "missing";
    source.elements.back().scrollbar = link;
    EXPECT_THROW((void)write_xml(source), std::invalid_argument);
    source.elements.back().scrollbar = saved;
    link = saved.value_or(scrollbar_link{});
    link.minimum_thumb = -1;
    source.elements.back().scrollbar = link;
    EXPECT_THROW((void)write_xml(source), std::invalid_argument);
    EXPECT_THROW((void)parse_xml({"<ui version='1'><element id='root' panel='scroll'><scrollbar "
                                  "viewport='root'/></element></ui>"}),
                 std::invalid_argument);
}
} // namespace atlas::ui_content::tests
