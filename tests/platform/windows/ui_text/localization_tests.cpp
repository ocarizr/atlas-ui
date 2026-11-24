/** @file @brief Localized label metrics and native shaping invalidate retained UI correctly. */
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui_text/localized_label.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_text::tests
{
/// @brief A language publication refreshes retained glyphs and owner-applied layout metrics.
TEST(localized_label_test, updates_metrics_without_reconstructing_the_tree)
{
    localization::context source{localization::parse_xml(
        R"(<localization fallback="en"><language tag="en" direction="ltr"><string key="title">Hi</string></language><language tag="de" direction="ltr"><string key="title">A substantially wider label</string></language><language tag="ar" direction="rtl"><string key="title">إعدادات HUD</string></language></localization>)")};
    localized_label label{source, source.key("title"), {.text = {}, .family = "Consolas"}};
    ui::element_tree tree;
    const auto element = tree.create();
    label.apply_metrics(tree, element);
    const auto before = tree.layout(element).intrinsic;
    tree.acknowledge(element);
    label.apply_metrics(tree, element);
    EXPECT_FALSE(tree.dirty(element).layout);
    auto retained = label.snapshot();
    source.select("de");
    EXPECT_FALSE(tree.dirty(element).layout);
    label.apply_metrics(tree, element);
    EXPECT_TRUE(tree.dirty(element).layout);
    EXPECT_GT(tree.layout(element).intrinsic.width, before.width);
    EXPECT_EQ(tree.size(), 1U);
    EXPECT_EQ(retained->content.text, "Hi");
    source.select("ar");
    EXPECT_EQ(label.snapshot()->content.direction, localization::text_direction::right_to_left);
    EXPECT_EQ(label.snapshot()->layout.missing_glyphs, 0U);
    EXPECT_GT(label.snapshot()->layout.faces, 1U);
}

/// @brief Failed refresh keeps the previous label, and owner/token teardown is independent.
TEST(localized_label_test, preserves_layout_on_bad_count_and_outlives_context)
{
    auto source = std::make_unique<localization::context>(localization::parse_xml(
        R"(<localization fallback="en"><language tag="en" direction="ltr"><plural key="items"><form category="one">One item</form><form category="other">Many items</form></plural></language></localization>)"));
    localized_label label{*source, source->key("items"), {}, 1};
    const auto previous = label.snapshot();
    EXPECT_THROW(label.set_count(*source, {}), std::invalid_argument);
    EXPECT_EQ(label.snapshot(), previous);
    label.set_count(*source, 2);
    EXPECT_EQ(label.snapshot()->content.text, "Many items");
    source.reset();
    EXPECT_EQ(label.snapshot()->content.text, "Many items");
}
} // namespace atlas::ui_text::tests
