/** @file @brief Material playback and authoring invariants on the UI owner. */
#include "composition_fixture.hpp"
#include <atlas/ui/layout_engine.hpp>

namespace atlas::ui_content::tests
{
using namespace std::chrono_literals;

/// @brief An explicitly default-valued replacement survives the authoring round trip.
TEST(ui_material, RetainsExplicitOverrideValues)
{
    constexpr auto authored = R"(<material-override version="1"
        template="018d0b95cf2673c6a0d7dd8ce5d29b23:0000000000000001"
        material-alpha="1"/>)";
    const auto value = parse_material_xml({authored});
    const auto round_trip = parse_material_xml({write_material_xml(value)});
    const auto& overrides = std::get<material_instance>(round_trip).parameters;
    ASSERT_EQ(overrides.size(), 1);
    EXPECT_EQ(overrides.front().slot, ui::property::material_alpha);
    EXPECT_EQ(overrides.front().value, 1);
}

/// @brief Publish through the existing absolute property stream.
static void publish(instance& view, std::uint64_t frame)
{
    auto& tree = view.tree();
    tree.commands().begin_frame(frame);
    view.publish_animation();
    tree.apply(tree.commands().seal());
}

/// @brief Material animation changes only one instance and preserves its computed layout.
TEST_F(composition_fixture, MaterialTracksPreserveLayoutAndTemplateDefaults)
{
    auto first = instance::build(load(ui_asset, {*storage}), {*storage});
    auto second = instance::build(load(ui_asset, {*storage}), {*storage});
    const auto target = first->find("sibling");
    const ui::material_style shared{.kind = ui::material_kind::shape};
    first->tree().set_material(target, shared);
    second->tree().set_material(second->find("sibling"), shared);
    ui::layout_engine layout{jobs.dispatcher()};
    const auto geometry = layout.update(first->tree(), {0, 0, 160, 100});
    (void)first->start_animation(
        {100ms,
         {{"sibling", ui::property::material_morph, {{0ms, 0.f}, {100ms, 1.f}}},
          {"sibling", ui::property::material_red, {{0ms, 1.f}, {100ms, 0.f}}}}});
    first->advance_animation(50ms);
    publish(*first, 2);
    EXPECT_FLOAT_EQ(first->tree().material(target).value_or(ui::material_style{}).morph, 0.5f);
    EXPECT_FLOAT_EQ(first->tree().material(target).value_or(ui::material_style{}).tint[0], 0.5f);
    EXPECT_EQ(second->tree().material(second->find("sibling")), shared);
    EXPECT_EQ(first->tree().snapshot().revision(), geometry.revision());
    EXPECT_THROW((void)second->start_animation(
                     {100ms, {{"sibling", ui::property::material_morph, {{0ms, 1.1f}}}}}),
                 std::invalid_argument);
}

/// @brief Sparse overrides are isolated and material values round-trip canonically.
TEST(ui_material, ResolvesOverridesAndRoundTripsPaintValues)
{
    const ui::material_style defaults{.kind = ui::material_kind::shape};
    const std::array overrides{ui::material_override{ui::property::material_radius_tl, 0.5f},
                               ui::material_override{ui::property::material_union, 0.25f}};
    const auto material = ui::resolve_material(defaults, overrides);
    EXPECT_FLOAT_EQ(defaults.radius[0], 0);
    EXPECT_FLOAT_EQ(material.radius[0], 0.5f);
    auto document = parse_xml({R"(<ui version="1"><element id="root"><paint fill="true"
        material="shape" material-radius-tl="0.5" material-union="0.25"/></element></ui>)"});
    EXPECT_EQ(document.elements.front().paint.material, material);
    EXPECT_EQ(parse_xml({write_xml(document)}).elements.front().paint.material, material);
    const std::array invalid{ui::material_override{ui::property::material_u_scale, 0.5f}};
    EXPECT_THROW((void)ui::resolve_material(defaults, invalid), std::invalid_argument);
    const std::array duplicate{overrides.front(), overrides.front()};
    EXPECT_THROW((void)ui::resolve_material(defaults, duplicate), std::invalid_argument);
}
} // namespace atlas::ui_content::tests
