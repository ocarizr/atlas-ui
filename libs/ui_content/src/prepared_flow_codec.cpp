/** @file @brief Bounded binary serialization for authored viewport Flow. */
#include "flow_versions.hpp"
#include "prepared_material_fields.hpp"
#include "prepared_versions.hpp"
#include <atlas/mvvm_content/detail/prepared_definition.hpp>
#include <atlas/mvvm_content/detail/prepared_envelope.hpp>

namespace atlas::ui_content
{
namespace
{
/// @brief Distinguish Flow records from independent prepared UI and MVVM records.
constexpr mvvm_content::detail::signature magic{std::byte{'A'}, std::byte{'F'}, std::byte{'L'},
                                                std::byte{'W'}};
constexpr std::uint16_t version = 1; ///< Flow wire schema v1.

/// @brief Preserve catalog identity and authored revision together.
inline constexpr auto dependency_fields = [](auto& archive, prepared_dependency& value)
{
    detail::reference(archive, value.source);
    archive.field(value.revision);
};

/// @brief Persist only stable graph vocabulary and typed guard values.
template <class Archive> void graph_fields(Archive& archive, flow_document& graph, bool extended)
{
    archive.sequence(graph.regions, 64,
                     [extended](auto& data, flow_region& region)
                     {
                         data.fields(region.id, region.initial, region.owner);
                         if (extended)
                         {
                             data.fields(region.host, region.remember);
                         }
                     });
    archive.sequence(graph.screens, 256,
                     [extended](auto& data, flow_screen& screen)
                     {
                         data.fields(screen.id, screen.region, screen.focus);
                         detail::reference(data, screen.asset);
                         if (extended)
                         {
                             const auto field = [](auto& format, bool& value)
                             {
                                 format.field(value);
                             };
                             data.optional(screen.popup.modal, field);
                             data.optional(screen.popup.dismiss_outside, field);
                             data.optional(screen.popup.dismiss_back, field);
                         }
                     });
    archive.sequence(graph.parameters, 64,
                     [](auto& data, flow_parameter& parameter)
                     {
                         data.field(parameter.id);
                         mvvm_content::detail::prepared_scalar(data, parameter.value);
                     });
    archive.sequence(graph.transitions, 1024,
                     [extended](auto& data, flow_transition& transition)
                     {
                         data.fields(transition.id, transition.from, transition.to,
                                     transition.enabled, transition.control, transition.action);
                         data.choice(transition.operation, extended ? flow_operation::close_popup
                                                                    : flow_operation::reload);
                         data.optional(transition.guard,
                                       [](auto& guard_archive, flow_guard& guard)
                                       {
                                           guard_archive.field(guard.parameter);
                                           mvvm_content::detail::prepared_scalar(guard_archive,
                                                                                 guard.equals);
                                       });
                     });
}

/// @brief Carry the dependency contract alongside the graph without duplicating UI payloads.
template <class Archive> void fields(Archive& archive, prepared_flow& source, bool extended)
{
    dependency_fields(archive, source.source);
    graph_fields(archive, source.graph, extended);
    archive.sequence(source.screens, 256,
                     [](auto& data, prepared_flow_screen& screen)
                     {
                         detail::reference(data, screen.source);
                         data.sequence(screen.dependencies, detail::max_prepared_dependencies,
                                       dependency_fields);
                     });
}
} // namespace

/// @brief Write a deterministic Flow record using the shared prepared-content envelope.
std::vector<std::byte> encode_prepared(const prepared_flow& source)
{
    (void)flow_dependencies(source);
    auto copy = source;
    detail::binary<false> archive;
    const bool extended = detail::flow_v2(source.graph);
    fields(archive, copy, extended);
    return mvvm_content::detail::seal_prepared(archive, magic,
                                               extended ? std::uint16_t{2} : version);
}

/// @brief Reject incompatible or damaged records before publishing typed Flow data.
prepared_flow decode_prepared_flow(std::span<const std::byte> bytes)
{
    const auto encoded_version = detail::prepared_version(bytes);
    if (encoded_version != version && encoded_version != 2)
    {
        throw std::invalid_argument("Unsupported prepared Flow version");
    }

    detail::binary<true> archive{
        mvvm_content::detail::open_prepared(bytes, magic, encoded_version)};
    prepared_flow result;
    fields(archive, result, encoded_version == 2);
    if (!archive.exhausted())
    {
        throw std::invalid_argument("Trailing prepared Flow payload");
    }

    (void)flow_dependencies(result);
    return result;
}
} // namespace atlas::ui_content
