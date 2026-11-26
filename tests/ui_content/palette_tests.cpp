/** @file @brief Semantic tint XML, prepared parity and paint-only owner publication. */
#include "../ui/resource_test_support.hpp"
#include <algorithm>
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui_content/palette.hpp>
#include <atlas/ui_content/prepared.hpp>
#include <chrono>
#include <gtest/gtest.h>

namespace atlas::ui_content::tests
{
/// @brief Integrity failures reject corrupted or truncated prepared colors before compilation.
TEST(palette_content, PreparedEnvelopeRejectsCorruptionAndTruncation)
{
    const auto value = ui::palette::compile({"menu", {{"text", {1, 1, 1, 1}}}});
    auto bytes = encode_palette(*value);
    bytes.back() ^= std::byte{1};
    EXPECT_THROW((void)decode_palette(bytes), std::invalid_argument);
    bytes.pop_back();
    EXPECT_THROW((void)decode_palette(bytes), std::invalid_argument);
}

/// @brief XML sRGB is converted once; alpha stays linear and canonical bytes round-trip.
TEST(palette_content, XmlCompilesLinearColorsWithoutQuantizingNativeValues)
{
    const auto parsed = parse_palette_xml(
        {R"(<palette version="1" contract="editor"><color key="surface" value="#80808080"/></palette>)"});
    EXPECT_NEAR(parsed->color(0)[0], 0.2158605F, 0.000001F);
    EXPECT_NEAR(parsed->color(0)[3], 128.F / 255.F, 0.000001F);
    EXPECT_EQ(parse_palette_xml({write_palette_xml(*parsed)})->color(0), parsed->color(0));
    const auto native = ui::palette::compile({"editor", {{"surface", {.1234567F, .2F, .3F, .4F}}}});
    EXPECT_EQ(decode_palette(encode_palette(*native))->color(0), native->color(0));
}

/// @brief Strict bounded source rejects ignored data, duplicates, DTDs and malformed hex colors.
class palette_xml_failure : public testing::TestWithParam<const char*>
{
};

/// @brief Every invalid source is rejected before any shared context can see it.
TEST_P(palette_xml_failure, RejectsInvalidSource)
{
    EXPECT_THROW((void)parse_palette_xml({GetParam()}), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(
    StrictSchema, palette_xml_failure,
    testing::Values(
        "<palette version='2' contract='editor'/>",
        "<palette version='1' contract='editor' unknown='x'/>",
        "<!DOCTYPE palette><palette version='1' contract='editor'/>",
        "<palette version='1' contract='editor'><color key='x' value='#xyzxyzff'/></palette>",
        "<palette version='1' contract='editor'><color key='x' value='#ffffffff'/><color key='x' "
        "value='#000000ff'/></palette>"));

/// @brief Small independent memory-only composition; no project assets or native window required.
class palette_instance_test : public testing::Test
{
  protected:
    jobs::job_system jobs{1}; ///< Shared worker pool outlives the catalog and composed trees.
    streaming::catalog_streamer storage{
        jobs,
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")}),
        {}}; ///< Empty typed mount.
    std::shared_ptr<const ui::palette> light =
        ui::palette::compile({"editor", {{"surface", {1, 1, 1, 1}}}}); ///< Initial complete theme.
    std::shared_ptr<ui::palette_context> context =
        std::make_shared<ui::palette_context>(light); ///< Owner context.
    document source = parse_xml(
        {R"(<ui version="1"><element id="root" width="100" height="40"><paint fill="true" color-key="surface"/></element></ui>)"}); ///< Single referenced tint.
    construction options{storage}; ///< Explicit dependencies remain application-owned.

    void SetUp() override
    {
        options.palette = context;
    } ///< Share only the chosen context.
};

/// @brief A warmed color change preserves computed geometry and does not dirty measure/arrange.
TEST_F(palette_instance_test, PublishesOnlyPaintAndPreservesLiteralColors)
{
    element literal;
    literal.id = "literal";
    literal.parent = "root";
    literal.paint.fill = true;
    literal.paint.color = {.5F, .5F, .5F, 1};
    source.elements.push_back(std::move(literal));
    const auto view = instance::build(source, options);
    ui::layout_engine layout{jobs.dispatcher()};
    (void)layout.update(view->tree(), {0, 0, 100, 40});
    const auto root = view->find("root");
    const auto geometry = view->tree().computed(root);
    if (!geometry)
    {
        FAIL() << "Missing initial layout";
        return;
    }

    view->tree().acknowledge(root);
    context->select(ui::palette::compile({"editor", {{"surface", {0, 0, 0, 1}}}}));
    view->synchronize();
    EXPECT_EQ(view->tree().paint(root).color, (ui::rgba{0, 0, 0, 1}));
    const auto after = view->tree().computed(root);
    if (!after)
    {
        FAIL() << "Theme invalidated layout";
        return;
    }

    EXPECT_EQ(after->bounds, geometry->bounds);
    EXPECT_FALSE(view->tree().dirty(root).layout);
    EXPECT_EQ(view->tree().paint(view->find("literal")).color, (ui::rgba{.5F, .5F, .5F, 1}));
}

/// @brief Prepared construction retains semantic sources instead of baking one selected theme.
TEST_F(palette_instance_test, PreparedSourceRetainsBindingsAndRejectsMissingContext)
{
    EXPECT_THROW((void)instance::build(source, {storage}), std::invalid_argument);
    prepared_view candidate;
    candidate.source = {assets::parse_id("018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000001"),
                        assets::asset_type::ui};
    candidate.dependencies.push_back({candidate.source, 1});
    candidate.view = source;
    const auto cooked = decode_prepared(encode_prepared(candidate));
    EXPECT_EQ(write_xml(cooked.view), write_xml(source));
    const auto view = instance::build(cooked.view, options);
    EXPECT_EQ(view->tree().paint(view->find("root")).color, light->color(0));
    source.elements.front().color_key = "missing";
    EXPECT_THROW((void)instance::build(source, options), std::out_of_range);
}

/// @brief Animated material channels keep their visible sample while other tint channels update.
TEST_F(palette_instance_test, MaterialAnimationRetainsOwnershipAcrossThemeSelection)
{
    using namespace std::chrono_literals;
    source.elements.front().paint.material = ui::material_style{.kind = ui::material_kind::shape};
    source.elements.front().material_color_key = "surface";
    const auto view = instance::build(source, options);
    const auto root = view->find("root");
    view->play_animation({1s, {{"root", ui::property::material_red, {{0ns, .25F}, {1s, .75F}}}}},
                         animation_repeat::once);
    view->tree().commands().begin_frame(2);
    view->publish_animation();
    view->tree().apply(view->tree().commands().seal());
    const auto initial = view->tree().material(root);
    if (!initial)
    {
        FAIL() << "Missing initial material";
        return;
    }

    ASSERT_FLOAT_EQ(initial->tint[0], .25F);
    context->select(ui::palette::compile({"editor", {{"surface", {.1F, .2F, .3F, 1}}}}));
    view->refresh_palette();
    const auto material = view->tree().material(root);
    if (!material)
    {
        FAIL() << "Theme removed material";
        return;
    }

    EXPECT_FLOAT_EQ(material->tint[0], .25F);
    EXPECT_FLOAT_EQ(material->tint[1], .2F);
    EXPECT_FLOAT_EQ(material->tint[2], .3F);
}

/// @brief Warm color publication uses pre-existing tree storage and never invalidates geometry.
TEST_F(palette_instance_test, DenseThemePublicationDoesNotAllocateTreePmrOrInvalidateLayout)
{
    const auto memory = std::make_shared<ui::tests::failing_resource>();
    options.memory = memory;
    for (unsigned i = 0; i < 512; ++i)
    {
        element child;
        child.id = "child" + std::to_string(i);
        child.parent = "root";
        child.color_key = "surface";
        child.paint.fill = true;
        source.elements.push_back(std::move(child));
    }

    const auto started = std::chrono::steady_clock::now();
    const auto view = instance::build(source, options);
    const auto construction_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count();
    ui::layout_engine layout{jobs.dispatcher()};
    (void)layout.update(view->tree(), {0, 0, 100, 40});
    const auto before = view->tree().snapshot();
    const auto calls = memory->calls;
    const auto bytes = memory->live_bytes;
    const auto alternate = ui::palette::compile({"editor", {{"surface", {0, 0, 0, 1}}}});
    std::array<std::int64_t, 20> elapsed{};
    for (unsigned i = 0; i < 20; ++i)
    {
        const auto begin = std::chrono::steady_clock::now();
        context->select(i % 2 == 0 ? alternate : light);
        view->refresh_palette();
        elapsed[i] = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now() - begin)
                         .count();
    }

    std::ranges::sort(elapsed);
    RecordProperty("construction_ns", std::to_string(construction_ns));
    RecordProperty("publication_p50_ns", std::to_string(elapsed[10]));
    RecordProperty("publication_p95_ns", std::to_string(elapsed[18]));
    RecordProperty("tree_pmr_live_bytes", std::to_string(bytes));
    RecordProperty("publication_pmr_allocations", std::to_string(memory->calls - calls));
    EXPECT_EQ(memory->calls, calls);
    EXPECT_EQ(memory->live_bytes, bytes);
    EXPECT_EQ(view->tree().snapshot().revision(), before.revision());
}
} // namespace atlas::ui_content::tests
