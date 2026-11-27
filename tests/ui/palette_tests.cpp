/** @file @brief Complete semantic palettes preserve context compatibility and owner lifetime. */
#include <atlas/ui/palette.hpp>
#include <gtest/gtest.h>
#include <thread>

namespace atlas::ui::tests
{
/// @brief Resolve stable keys once; reordered declarations do not alter slot identities.
TEST(palette, CompatibleSelectionPreservesSlotsAndIndependentContexts)
{
    const auto light =
        palette::compile({"editor", {{"text", {0, 0, 0, 1}}, {"surface", {1, 1, 1, 1}}}});
    const auto dark =
        palette::compile({"editor", {{"surface", {0, 0, 0, 1}}, {"text", {1, 1, 1, 1}}}});
    palette_context chrome{light};
    palette_context preview{light};
    const auto slot = light->find("surface");
    chrome.select(dark);
    EXPECT_EQ(dark->find("surface"), slot);
    EXPECT_EQ(chrome.snapshot()->color(slot), (rgba{0, 0, 0, 1}));
    EXPECT_EQ(preview.snapshot(), light);
    EXPECT_EQ(chrome.revision(), 2);
    chrome.select(dark);
    EXPECT_EQ(chrome.revision(), 2);
}

/// @brief Failed or partial replacements keep the last complete snapshot and revision.
TEST(palette, RejectsIncompatibleReplacementWithoutMutation)
{
    const auto valid = palette::compile({"editor", {{"surface", {1, 1, 1, 1}}}});
    palette_context context{valid};
    EXPECT_THROW(context.select(palette::compile({"editor", {{"other", {0, 0, 0, 1}}}})),
                 std::invalid_argument);
    EXPECT_THROW(context.select(palette::compile({"preview", {{"surface", {0, 0, 0, 1}}}})),
                 std::invalid_argument);
    EXPECT_THROW(context.select({}), std::invalid_argument);
    EXPECT_EQ(context.snapshot(), valid);
    EXPECT_EQ(context.revision(), 1);
    EXPECT_THROW((void)valid->find("missing"), std::out_of_range);
    EXPECT_THROW((void)valid->color(1), std::out_of_range);
}

/// @brief Invalid definitions cannot enter any context, including nonfinite channel values.
TEST(palette, RejectsInvalidDefinitionsAndBudgetOverflow)
{
    EXPECT_THROW((void)palette::compile({"editor", {}}), std::invalid_argument);
    EXPECT_THROW((void)palette::compile({"", {{"surface", {1, 1, 1, 1}}}}), std::invalid_argument);
    EXPECT_THROW((void)palette::compile({"editor", {{"bad key", {1, 1, 1, 1}}}}),
                 std::invalid_argument);
    EXPECT_THROW((void)palette::compile({"editor", {{"surface", {2, 1, 1, 1}}}}),
                 std::invalid_argument);
    EXPECT_THROW(
        (void)palette::compile({"editor", {{"surface", {1, 1, 1, 1}}, {"surface", {0, 0, 0, 1}}}}),
        std::invalid_argument);
    palette_definition many{"editor", {}};
    for (unsigned i = 0; i <= max_palette_colors; ++i)
    {
        many.colors.push_back({"color" + std::to_string(i), {1, 1, 1, 1}});
    }

    EXPECT_THROW((void)palette::compile(many), std::invalid_argument);
}

/// @brief A reader can retain immutable colors but cannot mutate the UI owner's context.
TEST(palette, RejectsForeignOwnerSelection)
{
    const auto value = palette::compile({"editor", {{"surface", {1, 1, 1, 1}}}});
    palette_context context{value};
    bool rejected = false;
    std::thread worker{[&]
                       {
                           try
                           {
                               context.select(value);
                           }
                           catch (const std::logic_error&)
                           {
                               rejected = true;
                           }
                       }};
    worker.join();
    EXPECT_TRUE(rejected);
    EXPECT_EQ(context.snapshot(), value);
}
} // namespace atlas::ui::tests
