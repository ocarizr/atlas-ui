/** @file @brief Persistent materials composed through an isolated typed memory mount. */
#include <atlas/ui_content/instance.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_content::tests
{
namespace
{
/// @brief Build small immutable mounts with stable asset IDs across revisions.
struct material_assets
{
    /// @brief A fresh project catalog.
    assets::catalog_document draft{assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")};
    streaming::storage_mount backing; ///< Catalog-relative memory sources.

    /// @brief Register exact source bytes and keep identity independent of backing name.
    assets::asset_ref add(std::string name, assets::asset_type type, const std::string& bytes,
                          std::uint64_t revision = 1)
    {
        const assets::asset_ref ref{assets::allocate_id(draft), type};
        draft.records.push_back({ref, {"fixture", name}, revision, bytes.size()});
        backing.sources.emplace(
            name, streaming::memory_asset{
                      std::make_shared<const streaming::asset_bytes>(bytes.begin(), bytes.end())});
        backing.routes.push_back({ref, revision, std::move(name), 0});
        return ref;
    }

    /// @brief Retain a copy of the backing so tests can remount independently.
    auto mount(jobs::job_system& jobs) const
    {
        return std::make_unique<streaming::catalog_streamer>(jobs, assets::catalog::compile(draft),
                                                             backing);
    }

    /// @brief Replace a catalog record and backing route without changing persistent identity.
    void revise(assets::asset_ref asset, const std::string& name, const std::string& bytes)
    {
        for (auto& record : draft.records)
        {
            if (record.reference == asset)
            {
                ++record.revision;
                record.byte_size = bytes.size();
            }
        }

        for (auto& route : backing.routes)
        {
            if (route.reference == asset)
            {
                ++route.revision;
            }
        }

        backing.sources.at(name) = streaming::memory_asset{
            std::make_shared<const streaming::asset_bytes>(bytes.begin(), bytes.end())};
    }
};

/// @brief Use directly consumable template and override asset references from ordinary UI XML.
document screen(assets::asset_ref base, assets::asset_ref variant)
{
    const auto content =
        "<ui version='1'><element id='root'><paint fill='true' material-asset='" +
        assets::format_id(base.id) +
        "'/></element><element id='child' parent='root'><paint fill='true' material-asset='" +
        assets::format_id(variant.id) + "'/></element></ui>";
    return parse_xml({content});
}
} // namespace

/// @brief Share immutable template inputs, while per-element slots remain independently mutable.
TEST(ui_material_assets, InheritsWithSparseTextureAndScalarOverrides)
{
    jobs::job_system jobs{2};
    material_assets data;
    const auto first = data.add("first.bin", assets::asset_type::texture, "old");
    const auto second = data.add("second.bin", assets::asset_type::texture, "new");
    const auto shader = data.add("shared.hlsl", assets::asset_type::shader, "trusted test source");
    material_template defaults;
    defaults.texture = first;
    defaults.shader = shader;
    defaults.defaults.tint[3] = 0.25F;
    defaults.defaults.uv_scale = {0.5F, 0.75F};
    const auto base =
        data.add("template.xml", assets::asset_type::material, write_material_xml(defaults));
    const material_instance replacement{base, {{ui::property::material_alpha, 1}}, second};
    const auto variant =
        data.add("override.xml", assets::asset_type::material, write_material_xml(replacement));
    const auto storage = data.mount(jobs);
    const auto definition = screen(base, variant);
    const auto view = instance::build(parse_xml({write_xml(definition)}), {*storage});
    const auto root = view->find("root");
    const auto child = view->find("child");
    const auto base_style = view->tree().material(root).value_or(ui::material_style{});
    auto override_style = view->tree().material(child).value_or(ui::material_style{});
    EXPECT_FLOAT_EQ(base_style.tint.at(3), 0.25F);
    EXPECT_FLOAT_EQ(override_style.tint.at(3), 1);
    EXPECT_EQ(override_style.uv_scale, base_style.uv_scale);
    EXPECT_EQ(view->texture("root"), first);
    EXPECT_EQ(view->texture("child"), second);
    ASSERT_EQ(view->material_shaders().size(), 1);
    EXPECT_EQ(base_style.shader, view->material_shaders().front().key);
    EXPECT_EQ(override_style.shader, base_style.shader);
    override_style.uv_scale.at(0) = 1;
    view->tree().set_material(child, override_style);
    EXPECT_EQ(view->tree().material(root), base_style);
    EXPECT_TRUE(view->materials_current(*storage));
    defaults.defaults.uv_scale = {0.75F, 1};
    data.revise(base, "template.xml", write_material_xml(defaults));
    const auto replacement_mount = data.mount(jobs);
    EXPECT_FALSE(view->materials_current(*replacement_mount));
    const auto rebuilt = instance::build(definition, {*replacement_mount});
    ASSERT_EQ(rebuilt->material_shaders().size(), 1);
    EXPECT_NE(rebuilt->material_shaders().front().key, base_style.shader);
    const auto revised =
        rebuilt->tree().material(rebuilt->find("child")).value_or(ui::material_style{});
    EXPECT_EQ(revised.uv_scale, defaults.defaults.uv_scale);
    EXPECT_FLOAT_EQ(revised.tint.at(3), 1);
}

/// @brief Prepared descriptors use the same invariants and reject stale authored revisions.
TEST(ui_material_assets, ValidatesPreparedTemplatesBeforePublication)
{
    jobs::job_system jobs{2};
    material_assets data;
    const auto base = data.add("template.bin", assets::asset_type::material, "prepared", 2);
    const auto storage = data.mount(jobs);
    const auto definition = screen(base, base);
    material_document prepared{material_template{.defaults = {.kind = ui::material_kind::shape}}};
    std::array candidates{prepared_material{base, 1, &prepared}};
    construction options{*storage};
    options.materials = candidates;
    EXPECT_THROW((void)instance::build(definition, options), std::invalid_argument);
    candidates.front().revision = 2;
    const auto view = instance::build(definition, options);
    const auto material = view->tree().material(view->find("root")).value_or(ui::material_style{});
    EXPECT_EQ(material.kind, ui::material_kind::shape);
}

/// @brief Reject chains, incompatible slots and conflicting inline authoring transactionally.
TEST(ui_material_assets, RejectsInvalidInheritanceAndPresentation)
{
    jobs::job_system jobs{2};
    material_assets data;
    const material_template shape{.defaults = {.kind = ui::material_kind::shape}};
    const auto base =
        data.add("template.xml", assets::asset_type::material, write_material_xml(shape));
    const material_instance invalid{base, {{ui::property::material_u_scale, 1}}, {}};
    const auto variant =
        data.add("override.xml", assets::asset_type::material, write_material_xml(invalid));
    const material_instance nested{variant, {}, {}};
    const auto chain =
        data.add("chain.xml", assets::asset_type::material, write_material_xml(nested));
    const auto storage = data.mount(jobs);
    EXPECT_THROW((void)instance::build(screen(base, variant), {*storage}), std::invalid_argument);
    EXPECT_THROW((void)instance::build(screen(base, chain), {*storage}), std::invalid_argument);
    auto conflict = screen(base, base);
    conflict.elements.front().paint.material = shape.defaults;
    EXPECT_THROW((void)write_xml(conflict), std::invalid_argument);
    EXPECT_NO_THROW((void)instance::build(screen(base, base), {*storage}));
}

/// @brief XML rejects missing defaults, unsupported versions and duplicate slot names.
TEST(ui_material_assets, RejectsMalformedMaterialXml)
{
    constexpr std::array bad{
        "<material-template version='1'/>", "<material-template version='1' kind='texture'/>",
        "<material-template version='2' kind='shape'/>",
        "<material-template version='1' kind='shape' material-alpha='1' material-alpha='0'/>",
        "<material-template version='1' kind='shape' material-u-scale='1'/>"};
    for (const auto xml : bad)
    {
        EXPECT_THROW((void)parse_material_xml({xml}), std::invalid_argument) << xml;
    }
}
} // namespace atlas::ui_content::tests
