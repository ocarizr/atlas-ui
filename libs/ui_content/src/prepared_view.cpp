/** @file @brief Resolve the typed prepared view closure through the storage boundary. */
#include "document_validation.hpp"
#include "material_validation.hpp"
#include "prepared_binary.hpp"
#include "storage.hpp"
#include <map>
#include <set>

namespace atlas::ui_content
{
namespace
{
using dependency_set = std::set<assets::asset_ref>; ///< Sorted exact authoring references.

/// @brief Require a supported UI or material asset identity before storage admission.
void require_type(assets::asset_ref source, assets::asset_type expected)
{
    if (source.type != expected)
    {
        throw std::invalid_argument("Incompatible prepared UI asset type");
    }

    (void)assets::format_id(source.id);
}

/// @brief Collect the exact template chain and check sparse values against its shader kind.
template <class Lookup>
void collect_material(dependency_set& closure, assets::asset_ref source, const Lookup& lookup)
{
    if (!closure.insert(source).second)
    {
        return;
    }

    const auto& value = lookup(source);
    const auto* sparse = std::get_if<material_instance>(&value);
    const auto& inherited = sparse ? lookup(sparse->source) : value;
    const auto* base = std::get_if<material_template>(&inherited);
    if (!base)
    {
        throw std::invalid_argument("Prepared UI material override must target a template");
    }

    if (sparse)
    {
        closure.insert(sparse->source);
        (void)ui::resolve_material(base->defaults, sparse->parameters);
        if (sparse->texture)
        {
            if (base->defaults.kind != ui::material_kind::texture)
            {
                throw std::invalid_argument("Prepared shape material cannot override a texture");
            }

            closure.insert(*sparse->texture);
        }
    }

    if (base->texture)
    {
        closure.insert(*base->texture);
    }

    if (base->shader)
    {
        closure.insert(*base->shader);
    }
}

/// @brief Find all required typed references, preserving the model and tree authoring order.
template <class Lookup> dependency_set collect(const prepared_view& source, const Lookup& material)
{
    require_type(source.source, assets::asset_type::ui);
    detail::validate(source.view);
    dependency_set closure{source.source};
    for (const auto& model : source.view.models)
    {
        closure.insert(model.source);
    }

    for (const auto& node : source.view.elements)
    {
        if (node.script)
        {
            closure.insert(node.script->source);
        }

        if (node.texture)
        {
            closure.insert(*node.texture);
        }

        if (node.paint.material)
        {
            ui::validate_material(*node.paint.material);
            if ((node.paint.material->kind == ui::material_kind::texture) !=
                node.texture.has_value())
            {
                throw std::invalid_argument("Invalid prepared inline material and texture");
            }
        }

        if (node.material)
        {
            collect_material(closure, *node.material, material);
        }
    }

    return closure;
}

/// @brief Require the exact positive authored revision from the currently mounted catalog.
void validate_mount(const prepared_view& source, const construction& options)
{
    for (const auto& required : source.dependencies)
    {
        if (options.reads.cancellation.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        const auto stamp = options.storage.stamp(required.source);
        if (stamp.revision != required.revision || !options.storage.accepts(stamp))
        {
            throw std::invalid_argument("Stale prepared UI dependency: " +
                                        assets::format_id(required.source.id));
        }
    }
}
} // namespace

/// @brief Match the manifest against the closure implied by the decoded records.
void detail::validate_prepared(const prepared_view& source)
{
    if (source.dependencies.size() > max_prepared_dependencies ||
        source.materials.size() > max_prepared_materials)
    {
        throw std::length_error("Prepared UI dependency or material limit exceeded");
    }

    std::map<assets::asset_ref, const material_document*> materials;
    std::optional<assets::asset_ref> previous;
    for (const auto& record : source.materials)
    {
        require_type(record.source, assets::asset_type::material);
        if (previous && *previous >= record.source)
        {
            throw std::invalid_argument("Prepared UI material table must be unique and sorted");
        }

        validate_material_document(record.value);
        materials.emplace(record.source, &record.value);
        previous = record.source;
    }

    const auto closure =
        collect(source,
                [&](assets::asset_ref value) -> const material_document&
                {
                    const auto found = materials.find(value);
                    if (found == materials.end())
                    {
                        throw std::invalid_argument("Missing prepared UI material");
                    }

                    return *found->second;
                });
    if (!std::ranges::equal(source.dependencies, closure,
                            [](const auto& record, const auto& reference)
                            {
                                return record.revision != 0 && record.source == reference;
                            }) ||
        std::ranges::any_of(materials,
                            [&](const auto& item)
                            {
                                return !closure.contains(item.first);
                            }))
    {
        throw std::invalid_argument(
            "Prepared UI dependency closure does not match its descriptors");
    }
}

/// @brief Prepare XML sources once through the authorized typed catalog.
prepared_view prepare_view(assets::asset_ref source, const construction& options)
{
    const diagnostics::operation_scope scope{options.diagnostics,
                                             {"ui.prepared.cook", "ui.content"}};
    prepared_view result{};
    result.source = source;
    result.view = load(source, options);
    std::map<assets::asset_ref, material_document> materials;
    const auto closure =
        collect(result,
                [&](assets::asset_ref ref) -> const material_document&
                {
                    if (const auto found = materials.find(ref); found != materials.end())
                    {
                        return found->second;
                    }

                    const auto bytes = detail::acquire(options.storage.describe(ref),
                                                       detail::max_material_bytes, options);
                    return materials
                        .emplace(ref, parse_material_xml({bytes, assets::format_id(ref.id)},
                                                         options.diagnostics))
                        .first->second;
                });
    for (const auto& ref : closure)
    {
        result.dependencies.push_back({ref, options.storage.describe(ref).revision});
    }

    for (auto& [ref, material] : materials)
    {
        result.materials.push_back({ref, std::move(material)});
    }

    detail::validate_prepared(result);
    validate_mount(result, options);
    return result;
}

/// @brief Acquire a prepared representation using the same typed identity as its XML source.
prepared_view load_prepared(assets::asset_ref source, const construction& options)
{
    const diagnostics::operation_scope scope{options.diagnostics,
                                             {"ui.prepared.load", "ui.content"}};
    require_type(source, assets::asset_type::ui);
    const auto bytes =
        detail::acquire(options.storage.describe(source), detail::max_prepared_bytes, options);
    auto result = decode_prepared(std::as_bytes(std::span{bytes}));
    if (result.source != source)
    {
        throw std::invalid_argument("Prepared UI record identity does not match its catalog entry");
    }

    return result;
}

/// @brief Feed verified material records into the existing private composition candidate.
std::unique_ptr<instance> build_prepared(const prepared_view& source, const construction& options)
{
    const diagnostics::operation_scope scope{options.diagnostics,
                                             {"ui.prepared.build", "ui.content"}};
    detail::validate_prepared(source);
    validate_mount(source, options);
    if (!options.materials.empty())
    {
        throw std::invalid_argument("Prepared UI already owns its material descriptors");
    }

    std::vector<prepared_material> materials;
    materials.reserve(source.materials.size());
    for (const auto& record : source.materials)
    {
        const auto match = std::ranges::lower_bound(source.dependencies, record.source, {},
                                                    &prepared_dependency::source);
        materials.push_back({record.source, match->revision, &record.value});
    }

    auto resolved = options;
    resolved.materials = materials;
    return instance::build(source.view, resolved);
}
} // namespace atlas::ui_content
