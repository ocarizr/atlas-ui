/** @file @brief Version and integrity envelope for typed UI binary content. */
#include "document_validation.hpp"
#include "prepared_ui_fields.hpp"
#include "prepared_versions.hpp"
#include <algorithm>
#include <atlas/mvvm_content/detail/prepared_definition.hpp>
#include <atlas/mvvm_content/detail/prepared_envelope.hpp>

namespace atlas::ui_content
{
namespace
{
/// @brief Atlas UI prepared signature.
constexpr mvvm_content::detail::signature magic{std::byte{'A'}, std::byte{'U'}, std::byte{'I'},
                                                std::byte{'P'}};
constexpr std::uint16_t wire_version = 2; ///< Add the embedded ViewModel definition table.

/// @brief Encode the typed UI and its exact catalog closure in v2 field order.
template <bool Reading>
void serialize(detail::binary<Reading>& format, prepared_view& source, bool presentation,
               bool events, bool scrollbars, bool grids)
{
    detail::reference(format, source.source);
    format.sequence(source.dependencies, detail::max_prepared_dependencies,
                    [](auto& archive, prepared_dependency& entry)
                    {
                        detail::reference(archive, entry.source);
                        archive.field(entry.revision);
                    });
    format.sequence(source.materials, detail::max_prepared_materials,
                    detail::material_record_fields);
    format.sequence(source.models, detail::max_models,
                    [](auto& archive, prepared_model_record& record)
                    {
                        detail::reference(archive, record.source);
                        mvvm::definition definition;
                        if constexpr (!Reading)
                        {
                            if (!record.value)
                            {
                                throw std::invalid_argument("Missing prepared ViewModel schema");
                            }

                            definition = record.value->describe();
                        }

                        mvvm_content::detail::prepared_definition(archive, definition);
                        if constexpr (Reading)
                        {
                            record.value = mvvm::compile(definition);
                        }
                    });
    format.sequence(source.view.models, detail::max_models,
                    [](auto& archive, model_context& model)
                    {
                        archive.field(model.name);
                        detail::reference(archive, model.source);
                    });
    if (grids)
    {
        format.sequence(source.view.elements, detail::max_elements,
                        detail::prepared_grid_element_fields);
    }
    else
    {
        format.sequence(source.view.elements, detail::max_elements,
                        detail::prepared_element_fields);
    }

    if (presentation)
    {
        format.choice(source.view.presentation.type, assets::asset_type::widget);
        auto& policy = source.view.presentation.popup;
        format.fields(policy.modal, policy.dismiss_outside, policy.dismiss_back);
    }

    if (events)
    {
        for (auto& node : source.view.elements)
        {
            format.sequence(node.events, 64,
                            [](auto& archive, event_action& event)
                            {
                                archive.choice(event.event, authored_event::focus_exit);
                                archive.fields(event.action, event.context, event.screen,
                                               event.route, event.input_action, event.region);
                                archive.sequence(event.arguments, 32,
                                                 mvvm_content::detail::prepared_scalar);
                            });
        }
    }

    if (scrollbars)
    {
        for (auto& node : source.view.elements)
        {
            format.optional(node.scrollbar,
                            [](auto& archive, scrollbar_link& link)
                            {
                                archive.field(link.viewport);
                                archive.choice(link.axis, ui::scroll_axis::vertical);
                                archive.fields(link.minimum_thumb, link.hide_when_zero);
                                for (auto& color : link.color)
                                {
                                    archive.field(color);
                                }
                            });
        }
    }
}
} // namespace

/// @brief Write a deterministic envelope around the validated prepared descriptors.
std::vector<std::byte> encode_prepared(const prepared_view& source)
{
    detail::validate_prepared(source);
    auto copy = source;
    detail::binary<false> payload;
    const bool presentation = source.view.presentation.type != assets::asset_type::ui;
    const bool events = std::ranges::any_of(source.view.elements,
                                            [](const auto& v)
                                            {
                                                return !v.events.empty();
                                            });
    const bool scroll =
        std::ranges::any_of(source.view.elements,
                            [](const auto& node)
                            {
                                return std::holds_alternative<ui::scroll_layout>(node.panel);
                            });
    const bool scrollbars = std::ranges::any_of(
        source.view.elements,
        [](const auto& node)
        {
            return node.scrollbar.has_value() ||
                   std::ranges::any_of(node.actions,
                                       [](const auto& action)
                                       {
                                           return action.binding.event ==
                                                  ui::event_kind::pointer_scroll;
                                       });
        });
    const bool grids = std::ranges::any_of(
        source.view.elements,
        [](const auto& node)
        {
            return std::holds_alternative<ui::grid_layout>(node.panel) || node.grid.has_value();
        });
    serialize(payload, copy, presentation || events || scroll || scrollbars || grids,
              events || scroll || scrollbars || grids, scrollbars || grids, grids);
    return mvvm_content::detail::seal_prepared(payload, magic,
                                               grids          ? std::uint16_t{7}
                                               : scrollbars   ? std::uint16_t{6}
                                               : scroll       ? std::uint16_t{5}
                                               : events       ? std::uint16_t{4}
                                               : presentation ? std::uint16_t{3}
                                                              : wire_version);
}

/// @brief Reject incorrect extent, version and checksum before decoding typed records.
prepared_view decode_prepared(std::span<const std::byte> bytes)
{
    const auto version = detail::prepared_version(bytes);
    if (version != wire_version && version != 3 && version != 4 && version != 5 && version != 6 &&
        version != 7)
    {
        throw std::invalid_argument("Unsupported prepared UI version");
    }

    detail::binary<true> archive{mvvm_content::detail::open_prepared(bytes, magic, version)};
    prepared_view result;
    serialize(archive, result, version >= 3, version >= 4, version >= 6, version >= 7);
    if (version < 5 && std::ranges::any_of(
                           result.view.elements,
                           [](const auto& node)
                           {
                               return std::holds_alternative<ui::scroll_layout>(node.panel);
                           }))
    {
        throw std::invalid_argument("Scroll panel requires prepared UI version 5");
    }

    if (!archive.exhausted())
    {
        throw std::invalid_argument("Trailing prepared UI payload");
    }

    detail::validate_prepared(result);
    return result;
}
} // namespace atlas::ui_content
